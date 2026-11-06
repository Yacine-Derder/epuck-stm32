#include "esp_link.h"
#include "app_config.h"
#include "epuck_link.h"
#include "main.h"
#include "usb_console.h"
#include <stdio.h>
#include <string.h>

#if APP_ESP_ENABLED
extern UART_HandleTypeDef huart3;
#define ESP_RING 1024U
#define ESP_QUEUE 16U
#define ESP_LINE 320U
#define ESP_FAULT_UART 1U
#define ESP_FAULT_OVERFLOW 2U
#define ESP_FAULT_TX 4U
static uint8_t esp_requested;
static char esp_telemetry_line[310];
static uint8_t esp_telemetry_pending;
static uint32_t esp_telemetry_tick,esp_telemetry_drops;
static volatile uint8_t esp_active, esp_fault, esp_tx_fault, esp_busy;
#if APP_ESP_DMA_RX
#define ESP_DMA_SIZE 2048U
/* SRAM .bss, never CCM; F405 has no D-cache. DMA owns writes. */
static uint8_t esp_dma_buffer[ESP_DMA_SIZE];
static volatile uint32_t esp_dma_wraps, esp_dma_events;
static uint32_t esp_dma_read, esp_dma_previous, esp_dma_poll_tick, esp_dma_oldest_tick;
static uint32_t esp_dma_errors, esp_dma_restarts, esp_dma_start_failures;
static uint8_t esp_dma_restart_needed;
#else
static uint8_t esp_byte;
#endif
static char esp_sensor_line[ESP_LINE];
static uint8_t esp_sensor_pending, esp_uwb_streak;
static uint32_t esp_sensor_coalesced, esp_sensor_sent, esp_tx_bytes;

static volatile uint8_t esp_ring[ESP_RING];
static volatile uint16_t esp_head, esp_tail;
static uint32_t esp_received_tick[ESP_RING];
static char esp_line[128], esp_queue[ESP_QUEUE][ESP_LINE], esp_tx[ESP_LINE];
static uint8_t esp_queue_control[ESP_QUEUE];
static uint16_t esp_length;
static uint8_t esp_discard, esp_cr, esp_qhead, esp_qtail, esp_count;
static uint8_t esp_valid, esp_hb_seen;
static char esp_session_id[17];
static uint32_t esp_hb_tick, esp_sample_sequence, esp_log_drops, esp_sample_tick;
static uint8_t esp_sample_sent;
/* ISR callbacks capture flags/counters only. All diagnostic formatting/IO is
 * deferred to the main loop, and reporting never creates another queue fault.
 */
static volatile uint32_t esp_uart_errors, esp_rx_overflows, esp_control_full;
static volatile uint32_t esp_uart_error_bits;
static uint32_t esp_invalidations, esp_last_hb_age;
static uint8_t esp_last_fault_mask, esp_last_fault_q;
static const char *esp_last_reason="none";

static uint8_t esp_enqueue(const char *line, uint8_t control)
{
  uint32_t mask = __get_PRIMASK();
  uint8_t ok = 0U;
  __disable_irq();
  if (esp_active && esp_count < (control ? ESP_QUEUE : ESP_QUEUE - 4U)) {
    snprintf(esp_queue[esp_qhead], ESP_LINE, "%s\n", line);
    esp_queue_control[esp_qhead]=control!=0U;
    esp_qhead = (uint8_t)((esp_qhead + 1U) % ESP_QUEUE);
    ++esp_count;
    ok = 1U;
  } else if (esp_active) {
    if (control == 1U) { esp_tx_fault = 1U; ++esp_control_full; }
    else if (control == 0U) ++esp_log_drops;
    /* control=2 is best-effort diagnostics/LOST: no recursive fault. */
  }
  __set_PRIMASK(mask);
  return ok;
}

static void esp_invalidate(const char *reason);
uint32_t esp_link_uart_errors(void) {return esp_uart_errors;}
uint32_t esp_link_telemetry_dropped(void) {return esp_telemetry_drops;}
void esp_link_reset_diagnostics(void) {esp_uart_errors=esp_rx_overflows=esp_control_full=esp_telemetry_drops=0;}
uint8_t esp_link_telemetry(const char *line) {
  if(!esp_link_owner_alive(esp_session_id)||esp_telemetry_pending)return 0;
  if(strlen(line)!=309U||(strncmp(line,"UWB2 ",5)&&strncmp(line,"UWB3 ",5)))return 0;
  memcpy(esp_telemetry_line,line,310);esp_telemetry_line[309]='\n';
  esp_telemetry_tick=HAL_GetTick();esp_telemetry_pending=1;return 1;
}
void esp_link_set_enabled(uint8_t enabled) {
#if MUL_UNIVERSAL
  if(!enabled && esp_active) {
    esp_invalidate("telemetry_disabled");HAL_NVIC_DisableIRQ(USART3_IRQn);
    (void)HAL_UART_Abort(&huart3);esp_active=esp_busy=esp_valid=esp_hb_seen=0;
    esp_qhead=esp_qtail=esp_count=0;esp_tail=esp_head;esp_telemetry_pending=esp_sensor_pending=0;
    esp_length=0;esp_cr=esp_discard=esp_fault=esp_tx_fault=0;
  }
  esp_requested=enabled;if(enabled)esp_link_init();
#else
  esp_requested=enabled;
#endif
}
static void esp_fault_status(void)
{
  char text[192];
  uint32_t mask=__get_PRIMASK();
  __disable_irq();
  uint32_t uart=esp_uart_errors, bits=esp_uart_error_bits;
  uint32_t overflow=esp_rx_overflows, full=esp_control_full;
  uint8_t depth=esp_count;
  __set_PRIMASK(mask);
  snprintf(text,sizeof(text),
      "[ESP] faults uart=%lu bits=0x%lx rx_ovf=%lu tx_full=%lu q=%u fault_q=%u mask=0x%x invalidations=%lu",
      (unsigned long)uart,(unsigned long)bits,(unsigned long)overflow,
      (unsigned long)full,depth,esp_last_fault_q,esp_last_fault_mask,(unsigned long)esp_invalidations);
  usb_console_write(text);
}
void esp_link_usb_status(void)
{
  char text[192];
  snprintf(text,sizeof(text),"[ESP] sid=%s active=%u valid=%u hb_seen=%u hb_ms=%lu last=%s lost_hb_ms=%lu",
      esp_session_id[0]?esp_session_id:"-",esp_active,esp_valid,esp_hb_seen,
      (unsigned long)(esp_hb_seen?(uint32_t)(HAL_GetTick()-esp_hb_tick):0U),
      esp_last_reason,(unsigned long)esp_last_hb_age);
  usb_console_write(text);
  esp_fault_status();
  snprintf(text,sizeof(text),"[ESP] baud=%lu dma=%u sensor_sent=%lu sensor_coalesced=%lu tx_bytes=%lu",
    (unsigned long)APP_ESP_BAUD,APP_ESP_DMA_RX,(unsigned long)esp_sensor_sent,(unsigned long)esp_sensor_coalesced,(unsigned long)esp_tx_bytes);
  usb_console_write(text);
#if APP_ESP_DMA_RX
  snprintf(text,sizeof(text),"[ESP] dma_events=%lu dma_errors=%lu dma_restarts=%lu dma_start_failures=%lu ndtr=%lu",
    (unsigned long)esp_dma_events,(unsigned long)esp_dma_errors,(unsigned long)esp_dma_restarts,(unsigned long)esp_dma_start_failures,(unsigned long)__HAL_DMA_GET_COUNTER(huart3.hdmarx));
  usb_console_write(text);
#endif
}
static void esp_invalidate(const char *reason)
{
  if (!esp_valid) return; /* Already torn down; no repeated logs/STOP/queue faults. */
  char text[ESP_LINE];
  uint32_t age=esp_hb_seen?(uint32_t)(HAL_GetTick()-esp_hb_tick):0U;
  uint8_t seen=esp_hb_seen;
  esp_valid=esp_hb_seen=0U;
  esp_last_reason=reason; esp_last_hb_age=age; ++esp_invalidations;
  snprintf(text,sizeof(text),"[ESP] lost sid=%s reason=%s hb_ms=%lu seen=%u",
           esp_session_id,reason,(unsigned long)age,seen);
  usb_console_write(text);
  esp_fault_status();
  snprintf(text,sizeof(text),
      "DIAG %s reason=%s hb_ms=%lu seen=%u uart=%lu bits=0x%lx rx_ovf=%lu tx_full=%lu q=%u fault_q=%u mask=0x%x",
      esp_session_id,reason,(unsigned long)age,seen,(unsigned long)esp_uart_errors,
      (unsigned long)esp_uart_error_bits,(unsigned long)esp_rx_overflows,
      (unsigned long)esp_control_full,esp_count,esp_last_fault_q,esp_last_fault_mask);
  epuck_link_source_lost(EPUCK_SOURCE_WIFI,reason);
  char lost[64];
  snprintf(lost,sizeof(lost),"SESSION %s LOST",esp_session_id);
  uint32_t mask=__get_PRIMASK();
  __disable_irq();
  /* Optional diagnostics cannot consume the only slot left for LOST or
   * create queue pressure for the existing motor-loss response above.
   */
  if (esp_count < ESP_QUEUE-1U) (void)esp_enqueue(text,2U);
  (void)esp_enqueue(lost,2U);
  __set_PRIMASK(mask);
}

void esp_link_session(char out[17]) { memcpy(out, esp_session_id, 17U); }
uint8_t esp_link_owner_alive(const char *session)
{
  return esp_active && !esp_fault && !esp_tx_fault && esp_valid &&
      strcmp(session, esp_session_id) == 0 &&
      (MUL_ROBOT_EXPERIMENT || (esp_hb_seen && (uint32_t)(HAL_GetTick() - esp_hb_tick) < 500U));
}
void esp_link_reply_to(const char *session, const char *text)
{
  char record[ESP_LINE];
  if (!esp_active || strlen(session) != 16U) return;
  snprintf(record, sizeof(record), "RSP %s %.270s", session, text);
  (void)esp_enqueue(record, 1U);
}
void esp_link_log(const char *text)
{
  /* May be called by Qorvo IRQ: only copy into the protected queue. */
  char record[ESP_LINE];
  uint16_t n = 4U;
  memcpy(record, "LOG ", 4U);
  for (; *text; ++text) {
    if (*text == '\r') continue;
    if (*text == '\n' || n == ESP_LINE - 2U) {
      record[n] = 0;
      (void)esp_enqueue(record, 0U);
      n = 4U;
      if (*text == '\n') continue;
    }
    record[n++] = (*text >= 32 && *text <= 126) ? *text : '?';
  }
  if (n > 4U) { record[n] = 0; (void)esp_enqueue(record, 0U); }
}
#if APP_ESP_DMA_RX
void esp_link_rx_event(void *uart, uint16_t size)
{
  (void)size;
  if (uart != &huart3) return;
  ++esp_dma_events;
  if (HAL_UARTEx_GetRxEventType(&huart3)==HAL_UART_RXEVENT_TC) ++esp_dma_wraps;
}
static uint32_t esp_start_dma(void)
{
  esp_dma_wraps=esp_dma_read=esp_dma_previous=0;
  esp_dma_poll_tick=esp_dma_oldest_tick=HAL_GetTick();
  uint32_t result=HAL_UARTEx_ReceiveToIdle_DMA(&huart3,esp_dma_buffer,ESP_DMA_SIZE);
  if(result!=HAL_OK) ++esp_dma_start_failures;
  return result;
}
static void esp_dma_poll(void)
{
  uint32_t now=HAL_GetTick(), producer=esp_dma_previous;
  uint32_t mask=__get_PRIMASK();__disable_irq();
  /* TC can be pending while NVIC is masked. Count that wrap once, without
   * clearing the flag: the HAL callback will later commit it to wraps. */
  for(unsigned attempt=0;attempt<3;attempt++) {
    uint32_t pending=__HAL_DMA_GET_FLAG(huart3.hdmarx,DMA_FLAG_TCIF1_5)!=0;
    uint32_t remaining=__HAL_DMA_GET_COUNTER(huart3.hdmarx);
    if(remaining && remaining<=ESP_DMA_SIZE && pending==(__HAL_DMA_GET_FLAG(huart3.hdmarx,DMA_FLAG_TCIF1_5)!=0)) {
      producer=(esp_dma_wraps+pending)*ESP_DMA_SIZE+ESP_DMA_SIZE-remaining;
      break;
    }
  }
  uint32_t unread=producer-esp_dma_read;
  /* Half-buffer headroom prevents DMA overwriting bytes during the bounded
   * copy. A long blackout with progress makes lost full wraps ambiguous. */
  if(unread>ESP_DMA_SIZE/2U || (producer!=esp_dma_previous &&
      (uint32_t)(now-esp_dma_poll_tick)>=(ESP_DMA_SIZE*10000ULL/APP_ESP_BAUD))) {
    ++esp_rx_overflows;esp_fault|=ESP_FAULT_OVERFLOW;esp_dma_read=producer;
  } else {
    if(esp_dma_read==esp_dma_previous)esp_dma_oldest_tick=esp_dma_poll_tick;
    for(unsigned budget=0;budget<128U && esp_dma_read!=producer;budget++) {
      uint16_t next=(uint16_t)((esp_head+1U)%ESP_RING);
      if(next==esp_tail) {++esp_rx_overflows;esp_fault|=ESP_FAULT_OVERFLOW;esp_dma_read=producer;break;}
      esp_ring[esp_head]=esp_dma_buffer[esp_dma_read%ESP_DMA_SIZE];
      esp_received_tick[esp_head]=esp_dma_oldest_tick;
      esp_head=next;++esp_dma_read;
    }
  }
  esp_dma_previous=producer;esp_dma_poll_tick=now;
  __set_PRIMASK(mask);
}
#else
void esp_link_rx_event(void *uart, uint16_t size) {(void)uart;(void)size;}
#endif
static void esp_restart(void)
{
#if APP_ESP_DMA_RX
  if(esp_dma_restart_needed) {
    /* Recovery only in task context; never abort/rearm DMA inside an IRQ. */
    (void)HAL_UART_AbortReceive(&huart3);esp_dma_restart_needed=0;++esp_dma_restarts;
  }
  if(huart3.RxState==HAL_UART_STATE_READY)(void)esp_start_dma();
#else
  if (huart3.RxState == HAL_UART_STATE_READY)
    (void)HAL_UART_Receive_IT(&huart3, &esp_byte, 1U);
#endif
}
void esp_link_init(void)
{
#if MUL_UNIVERSAL
  if(!esp_requested) return;
#endif
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  if (!esp_active) {
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    HAL_NVIC_ClearPendingIRQ(USART3_IRQn);
#if APP_ESP_DMA_RX
    if (esp_start_dma() != HAL_OK) {
#else
    if (HAL_UART_Receive_IT(&huart3, &esp_byte, 1U) != HAL_OK) {
#endif
      __set_PRIMASK(mask);
      return;
    }
    esp_active = 1U;
    HAL_NVIC_SetPriority(USART3_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
  }
  __set_PRIMASK(mask);
}
uint8_t esp_link_rx_callback(void *uart)
{
  if (uart != &huart3) return 0U;
  if (!esp_active) return 1U;
#if !APP_ESP_DMA_RX
  uint16_t next = (uint16_t)((esp_head + 1U) % ESP_RING);
  if (next == esp_tail) { ++esp_rx_overflows; esp_fault |= ESP_FAULT_OVERFLOW; }
  if (huart3.ErrorCode != HAL_UART_ERROR_NONE) {
    esp_fault |= ESP_FAULT_UART; esp_uart_error_bits=huart3.ErrorCode;
  }
  if (next != esp_tail && huart3.ErrorCode == HAL_UART_ERROR_NONE) { esp_ring[esp_head] = esp_byte; esp_received_tick[esp_head]=HAL_GetTick(); esp_head = next; }
  if (huart3.ErrorCode == HAL_UART_ERROR_NONE) esp_restart();
#endif
  return 1U;
}
uint8_t esp_link_tx_callback(void *uart)
{
  if (uart != &huart3) return 0U;
  esp_busy = 0U;
  return 1U;
}
uint8_t esp_link_error_callback(void *uart)
{
  if (uart != &huart3) return 0U;
  if (esp_active) {
    ++esp_uart_errors; esp_uart_error_bits=huart3.ErrorCode;
    esp_fault |= ESP_FAULT_UART;
#if APP_ESP_DMA_RX
    if(huart3.ErrorCode & HAL_UART_ERROR_DMA) ++esp_dma_errors;
    esp_dma_restart_needed=1;
#else
    __HAL_UART_CLEAR_OREFLAG(&huart3); esp_restart();
#endif
  }
  return 1U;
}
static uint8_t esp_id_valid(const char *p)
{
  for (unsigned i = 0U; i < 16U; ++i)
    if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f'))) return 0U;
  return 1U;
}
static void esp_record(char *line, uint32_t received_at)
{
  char id[17], reply[64];
  size_t length = strlen(line);
  if ((uint32_t)(HAL_GetTick()-received_at) >= 500U) { esp_invalidate("stale_input"); return; }
  if (length == 21U && strncmp(line, "OPEN ", 5U) == 0 && esp_id_valid(line + 5)) {
    /* A duplicate cannot reopen an expired session or replay old commands. */
    uint8_t duplicate = strncmp(esp_session_id, line + 5, 16U) == 0;
    esp_invalidate("session_replaced");
    if (duplicate) return;
    memcpy(esp_session_id, line + 5, 16U); esp_session_id[16] = 0;
    esp_valid = 1U;
    snprintf(reply, sizeof(reply), "SESSION %s READY", esp_session_id);
    (void)esp_enqueue(reply, 1U);
    return;
  }
  if (length < 20U ||
      (strncmp(line, "CMD ", 4U) != 0 && strncmp(line, "CLOSE ", 6U) != 0)) {
    esp_invalidate("framing_invalid");
    return;
  }
  unsigned offset = strncmp(line, "CMD ", 4U) == 0 ? 4U : 6U;
  if (length < offset + 16U || !esp_id_valid(line + offset)) {
    esp_invalidate("session_invalid"); return;
  }
  memcpy(id, line + offset, 16U); id[16] = 0;
  if (!esp_valid || strcmp(id, esp_session_id) != 0) return; /* stale session */
  if (offset == 6U) {
    if (length != 22U) { esp_invalidate("close_malformed"); return; }
    esp_invalidate("explicit_close"); return;
  }
  if (length < 22U || line[20] != ' ') { esp_invalidate("cmd_malformed"); return; }
  const char *command = line + 21;
  if (strcmp(command, "CONTROL_HB") == 0) {
    esp_hb_seen = 1U; esp_hb_tick = received_at;
  } else {
    epuck_link_command(EPUCK_SOURCE_WIFI, command);
  }
}
static void esp_parse(uint8_t byte, uint32_t received_at)
{
  if (byte == '\n') {
    if (esp_discard) esp_invalidate("line_malformed");
    else if (esp_length) { esp_line[esp_length] = 0; esp_record(esp_line, received_at); }
    esp_length = 0U; esp_cr = esp_discard = 0U;
  } else if (!esp_discard) {
    if (esp_cr) esp_discard = 1U;
    else if (byte == '\r') esp_cr = 1U;
    else if (byte < 32U || byte > 126U || esp_length >= sizeof(esp_line)-1U) esp_discard = 1U;
    else esp_line[esp_length++] = (char)byte;
  }
}
void esp_link_publish_sensor(void)
{
  epuck_sensor_data_t sample;
  char record[ESP_LINE];
  if (!esp_active) return;
  uint32_t now=HAL_GetTick();
  if (esp_sample_sent && (uint32_t)(now-esp_sample_tick)<APP_WIFI_SENSOR_PERIOD_MS) return;
  if (epuck_link_get_sensor_data(&sample) && sample.accepted_records != esp_sample_sequence) {
    int n = snprintf(record, sizeof(record),
      "DATA2 %lu %lu %u %d %d %d %d %d %d %d %d %d %d %d %ld %ld",
      (unsigned long)sample.accepted_records, (unsigned long)(HAL_GetTick()-sample.received_at_ms),
      sample.time_ms, sample.prox[0],sample.prox[1],sample.prox[2],sample.prox[3],sample.prox[4],sample.prox[5],sample.prox[6],sample.prox[7],
      sample.acc[0],sample.acc[1],sample.acc[2],(long)sample.steps_left,(long)sample.steps_right);
    if (n > 0 && n < (int)sizeof(record)-1) {
      if(esp_sensor_pending)++esp_sensor_coalesced;
      memcpy(esp_sensor_line,record,(size_t)n);esp_sensor_line[n]='\n';esp_sensor_line[n+1]=0;esp_sensor_pending=1;
      esp_sample_sequence=sample.accepted_records; esp_sample_tick=now; esp_sample_sent=1U;
    }
  }
}
void esp_link_task(void)
{
  uint8_t control_sent=0;
  char record[64];
  uint32_t mask;
  if (!esp_active) return;
  if (!MUL_ROBOT_EXPERIMENT && esp_valid && esp_hb_seen && (uint32_t)(HAL_GetTick()-esp_hb_tick) >= 500U) {
    esp_invalidate("control_hb_timeout");
  }
  esp_restart();
#if APP_ESP_DMA_RX
  esp_dma_poll();
#endif
  for (unsigned budget = 0U; budget < 128U; ++budget) {
    uint8_t byte, fault;
    uint32_t received_at;
    mask = __get_PRIMASK(); __disable_irq();
    fault = esp_fault | (esp_tx_fault?ESP_FAULT_TX:0U);
    if (fault) {
      esp_last_fault_mask=fault; esp_last_fault_q=esp_count;
      esp_tail = esp_head; esp_fault = esp_tx_fault = 0U;
      esp_qhead = esp_qtail = esp_count = 0U;
      esp_length = 0U; esp_discard = 1U; esp_cr = 0U;
      if(esp_telemetry_pending || esp_sensor_pending) {esp_telemetry_drops++;esp_telemetry_pending=0;}
      if(esp_sensor_pending){++esp_sensor_coalesced;esp_sensor_pending=0;}
    }
    if (fault || esp_tail == esp_head) {
      __set_PRIMASK(mask);
      if (fault) esp_invalidate(fault & ESP_FAULT_UART ? "uart_fault" :
                                (fault & ESP_FAULT_OVERFLOW ? "rx_overflow" : "control_tx_full"));
      break;
    }
    byte = esp_ring[esp_tail]; received_at=esp_received_tick[esp_tail]; esp_tail = (uint16_t)((esp_tail+1U)%ESP_RING);
    __set_PRIMASK(mask); esp_parse(byte, received_at);
  }
  esp_link_publish_sensor();
  mask=__get_PRIMASK(); __disable_irq();
  /* Select actual control responses before telemetry, then optional LOG/DATA.
   * The legacy FIFO mixed those classes, allowing continuous debug output to
   * starve the independent telemetry slot despite ample UART capacity. */
  unsigned selected=0;
  if(esp_telemetry_pending || esp_sensor_pending) {
    while(selected<esp_count&&!esp_queue_control[(esp_qtail+selected)%ESP_QUEUE])selected++;
  }
  if (!esp_busy && selected<esp_count) {
    unsigned slot=(esp_qtail+selected)%ESP_QUEUE;
    memcpy(esp_tx,esp_queue[slot],ESP_LINE); esp_busy=1U;
    if (HAL_UART_Transmit_IT(&huart3,(uint8_t *)esp_tx,(uint16_t)strlen(esp_tx))==HAL_OK) {
      if(selected==0U)esp_qtail=(uint8_t)((esp_qtail+1U)%ESP_QUEUE);
      else {
      for(unsigned i=selected;i+1U<esp_count;i++) {
        unsigned dst=(esp_qtail+i)%ESP_QUEUE,src=(esp_qtail+i+1U)%ESP_QUEUE;
        memcpy(esp_queue[dst],esp_queue[src],ESP_LINE);esp_queue_control[dst]=esp_queue_control[src];
      }
      esp_qhead=(uint8_t)((esp_qhead+ESP_QUEUE-1U)%ESP_QUEUE);
      }
      esp_tx_bytes+=(uint32_t)strlen(esp_tx);
      --esp_count;control_sent=1;
    } else esp_busy=0U;
  }
  if(esp_telemetry_pending && (!esp_link_owner_alive(esp_session_id)||(uint32_t)(HAL_GetTick()-esp_telemetry_tick)>1000U)) {
    esp_telemetry_pending=0;esp_telemetry_drops++;
  }
  /* At most three UWB records before a pending DATA2. Control still wins;
   * LOG is lower priority than both. Each record completes without splicing. */
  if(!control_sent && !esp_busy && (esp_telemetry_pending || esp_sensor_pending)) {
    uint8_t sensor=esp_sensor_pending && (!esp_telemetry_pending || esp_uwb_streak>=3U);
    if(sensor)snprintf(esp_tx,sizeof(esp_tx),"%s",esp_sensor_line);
    else {memcpy(esp_tx,esp_telemetry_line,310);esp_tx[310]=0;}
    esp_busy=1;uint16_t length=(uint16_t)strlen(esp_tx);
    if(HAL_UART_Transmit_IT(&huart3,(uint8_t *)esp_tx,length)==HAL_OK) {
      esp_tx_bytes+=length;
      if(sensor){esp_sensor_pending=0;esp_uwb_streak=0;++esp_sensor_sent;}
      else {esp_telemetry_pending=0;if(esp_uwb_streak<3U)++esp_uwb_streak;}
    } else esp_busy=0;
  }
  uint32_t dropped=esp_log_drops;
  if (esp_count < ESP_QUEUE-4U) esp_log_drops=0U; else dropped=0U;
  __set_PRIMASK(mask);
  if (dropped) { snprintf(record,sizeof(record),"LOG dropped_records=%lu",(unsigned long)dropped); (void)esp_enqueue(record,0U); }
}
#else
void esp_link_rx_event(void *uart,uint16_t size) {(void)uart;(void)size;}
void esp_link_init(void) {}
void esp_link_set_enabled(uint8_t enabled) {(void)enabled;}
uint8_t esp_link_telemetry(const char *line) {(void)line;return 0;}
uint32_t esp_link_uart_errors(void) {return 0;}
uint32_t esp_link_telemetry_dropped(void) {return 0;}
void esp_link_reset_diagnostics(void) {}
void esp_link_task(void) {}
void esp_link_publish_sensor(void) {}
void esp_link_usb_status(void) { usb_console_write("[ESP] disabled (APP_ESP_ENABLED=0)"); }
uint8_t esp_link_rx_callback(void *uart) { (void)uart; return 0U; }
uint8_t esp_link_tx_callback(void *uart) { (void)uart; return 0U; }
uint8_t esp_link_error_callback(void *uart) { (void)uart; return 0U; }
void esp_link_log(const char *text) { (void)text; }
void esp_link_reply_to(const char *s,const char *text) { (void)s; (void)text; }
void esp_link_session(char out[17]) { out[0]=0; }
uint8_t esp_link_owner_alive(const char *s) { (void)s; return 0U; }
#endif

#if APP_ESP_DMA_RX
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart,uint16_t size)
{
  esp_link_rx_event(uart,size);
}
#endif

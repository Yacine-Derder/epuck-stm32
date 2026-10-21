#include "epuck_link.h"
#include "usb_console.h"
#include "esp_link.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static epuck_source_t output_source = EPUCK_SOURCE_USB;
static char output_session[17];
static void command_reply(const char *message)
{
  if (output_source == EPUCK_SOURCE_WIFI) esp_link_reply_to(output_session, message);
  else if (APP_DEBUG_TELEMETRY || strcmp(message,"[MOTOR] OK HB")!=0) usb_console_write(message);
}
static void command_context(epuck_source_t source)
{
  output_source = source;
  if (source == EPUCK_SOURCE_WIFI) esp_link_session(output_session);
  else output_session[0] = 0;
}
void epuck_link_console_command(const char *command) { epuck_link_command(EPUCK_SOURCE_USB, command); }
void epuck_link_console_lost(const char *reason) { epuck_link_source_lost(EPUCK_SOURCE_USB, reason); }

#if APP_EPUCK_ENABLED
#include "usbd_cdc_if.h"

extern UART_HandleTypeDef huart1;
extern USBD_HandleTypeDef hUsbDeviceFS;

#define RX_RING_SIZE 1024U
#define RX_TASK_BUDGET 64U

static const uint8_t ping[] = "PING\n";
enum { TX_IDLE, TX_PING, TX_STREAM, TX_ARM, TX_MOVE, TX_HB, TX_STOP };
#define PENDING_PING 1U
static volatile uint8_t tx_command;
static uint8_t tx_pending, tx_retry;
static uint32_t last_tx_attempt;
static uint8_t incoming;
/* ISR produces, task consumes. Critical sections protect consumption, fault
 * flushing and snapshots. One empty slot distinguishes full from empty.
 */
static volatile uint8_t ring[RX_RING_SIZE];
static volatile uint16_t head, tail;
static volatile uint8_t active, rx_fault;
static volatile uint32_t rx_bytes, rx_dropped, uart_errors, uart_error_bits;
static volatile uint32_t ping_started, ping_sent, stream_requests;
static uint32_t tx_failures, pong_count, bad_lines, malformed_data;
static uint32_t last_ping_tick, last_report_tick;
static char line[256], last_reply[256];
#if APP_DEBUG_TELEMETRY
static char usb_report[1280];
#endif
static uint32_t line_length;
static uint8_t discard_line, saw_cr, discarded_data;
static epuck_sensor_data_t latest_sample;

#if APP_EPUCK_STREAM_PERIOD_MS != 0 && (APP_EPUCK_STREAM_PERIOD_MS < 50 || APP_EPUCK_STREAM_PERIOD_MS > 10000)
#error "APP_EPUCK_STREAM_PERIOD_MS must be 0 or 50..10000"
#endif
#define STREAM_ACK_MS 500U
#define STREAM_RETRY_MS 5000U
#define STREAM_ATTEMPTS 3U
static uint16_t stream_desired = APP_EPUCK_STREAM_PERIOD_MS;
static uint16_t stream_inflight, stream_acknowledged;
static uint8_t stream_wait, stream_ack_seen, stream_uncertain, stream_known;
static uint8_t stream_need = 1U, stream_attempts, stream_peer_seen;
static uint32_t stream_sent_tick, stream_ack_tick, stream_pong_tick, stream_retry_tick;
static uint32_t stream_fence, stream_late_acks;
static char stream_tx_buffer[24];
static epuck_source_t stream_source = EPUCK_SOURCE_USB, stream_ack_source;
static char stream_session[17], stream_ack_session[17];

#define MOTOR_QUEUE_SIZE 4U
#define MOTOR_HEARTBEAT_MS 100U
#define MOTOR_ACK_TIMEOUT_MS 150U
#define MOTOR_SESSION_TIMEOUT_MS 200U
/* Only main-loop code changes session state; ISR only releases UART ownership. */
enum { MOTOR_DISARMED, MOTOR_ARMING, MOTOR_ARMED, MOTOR_STOPPING, MOTOR_LOST };
typedef struct { int16_t left, right; } motor_move_t;
static motor_move_t motor_moves[MOTOR_QUEUE_SIZE];
static uint8_t motor_head, motor_tail, motor_count;
static uint8_t motor_state, motor_known, motor_arm_pending, motor_stop_pending;
static uint8_t motor_wait;
static int16_t requested_left, requested_right, acknowledged_left, acknowledged_right;
static int16_t motor_wait_left, motor_wait_right;
static uint32_t motor_wait_tick, motor_last_ack, motor_last_keepalive;
static char motor_tx_buffer[32], motor_latest_ack[64];
static epuck_source_t motor_owner = EPUCK_SOURCE_NONE, motor_ack_source, motor_stop_source;
static char motor_owner_session[17], motor_ack_session[17], motor_stop_session[17];
static uint8_t motor_stop_subscribers;
static char motor_stop_wifi_session[17];
static void owner_context(void)
{
  output_source = motor_owner == EPUCK_SOURCE_NONE ? EPUCK_SOURCE_USB : motor_owner;
  memcpy(output_session, motor_owner_session, 17U);
}
static uint8_t owner_available(void)
{
  return motor_owner == EPUCK_SOURCE_WIFI ? esp_link_owner_alive(motor_owner_session) : usb_console_available();
}
static void motor_reply(const char *reply);
static void motor_receive_lost(void);

static void restart_rx(void)
{
  /* Called with IRQs masked or from USART1 IRQ, never waits for a byte.
   * HAL_BUSY means the existing receive is still armed. Task retries later.
   */
  if (huart1.RxState == HAL_UART_STATE_READY) {
    (void)HAL_UART_Receive_IT(&huart1, &incoming, 1U);
  }
}

void epuck_link_init(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (!active) {
    last_ping_tick = last_report_tick = HAL_GetTick();
    /* Discard stale hardware data/errors before the first receive. No
     * peripheral configuration or MSP/NVIC CubeMX changes are needed.
     */
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
    active = 1U;
    restart_rx();
    HAL_NVIC_SetPriority(USART1_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  }
  __set_PRIMASK(primask);
}

static void epuck_RxCplt_callback(UART_HandleTypeDef *uart)
{
  uint16_t next;
  if (!active || uart != &huart1) {
    return;
  }
  ++rx_bytes;
  next = (uint16_t)((head + 1U) % RX_RING_SIZE);
  /* HAL may deliver RX completion before ErrorCallback for a damaged byte.
   * Do not rearm here on error: HAL still has error cleanup to perform.
   */
  if (uart->ErrorCode != HAL_UART_ERROR_NONE || next == tail) {
    ++rx_dropped;
    rx_fault = 1U;
  } else {
    ring[head] = incoming;
    head = next;
  }
  if (uart->ErrorCode == HAL_UART_ERROR_NONE) {
    restart_rx();
  }
}

static void epuck_TxCplt_callback(UART_HandleTypeDef *uart)
{
  if (active && uart == &huart1) {
    if (tx_command == TX_PING) {
      ++ping_sent;
    } else if (tx_command == TX_STREAM) {
      ++stream_requests;
    }
    tx_command = TX_IDLE;
  }
}

static void epuck_Error_callback(UART_HandleTypeDef *uart)
{
  if (!active || uart != &huart1) {
    return;
  }
  ++uart_errors;
  uart_error_bits |= uart->ErrorCode;
  rx_fault = 1U;
  /* F4 clears PE/FE/NE/ORE by reading SR then DR. HAL has already consumed
   * any error-affected RX byte; also clear errors without RXNE to avoid an
   * IRQ storm. Overrun ends the HAL receive, which we rearm here.
   */
  __HAL_UART_CLEAR_OREFLAG(uart);
  restart_rx();
}

uint8_t epuck_link_get_sensor_data(epuck_sensor_data_t *sample)
{
  uint32_t primask;
  if (sample == NULL) {
    return 0U;
  }
  primask = __get_PRIMASK();
  __disable_irq();
  *sample = latest_sample;
  __set_PRIMASK(primask);
  return sample->valid;
}

/* Decimal tokens only. Check magnitude before every multiply/add, avoiding
 * overflow even for arbitrarily long numbers. Timestamp forbids negatives;
 * signed tokens accept +/- and the full INT32_MIN magnitude safely.
 */
static uint8_t parse_number(const char **cursor, uint32_t positive_limit,
                            uint32_t negative_limit, int64_t *value)
{
  const char *p = *cursor;
  uint8_t negative = 0U;
  uint32_t magnitude = 0U, limit;
  while (*p == ' ') {
    ++p;
  }
  if (*p == '+' || *p == '-') {
    negative = (*p == '-');
    ++p;
  }
  if (negative && negative_limit == 0U) {
    return 0U;
  }
  limit = negative ? negative_limit : positive_limit;
  if (*p < '0' || *p > '9') {
    return 0U;
  }
  do {
    uint32_t digit = (uint32_t)(*p - '0');
    if (magnitude > limit / 10U ||
        (magnitude == limit / 10U && digit > limit % 10U)) {
      return 0U;
    }
    magnitude = magnitude * 10U + digit;
    ++p;
  } while (*p >= '0' && *p <= '9');
  if (*p != ' ' && *p != '\0') {
    return 0U;
  }
  *cursor = p;
  *value = negative ? -(int64_t)magnitude : (int64_t)magnitude;
  return 1U;
}

static uint8_t stream_stale(uint32_t now)
{
  if (!stream_desired) return 0U;
  uint32_t age = (uint32_t)(now - stream_ack_tick);
  if (latest_sample.valid && (uint32_t)(now-latest_sample.received_at_ms) < age)
    age = (uint32_t)(now-latest_sample.received_at_ms);
  return (!latest_sample.valid && !stream_known) || age > (uint32_t)stream_desired*3U+500U;
}

void epuck_link_get_stream_status(epuck_stream_status_t *status)
{
  if (!status) return;
  uint32_t now = HAL_GetTick();
  *status = (epuck_stream_status_t){
    .desired_ms=stream_desired, .in_flight_ms=stream_wait ? stream_inflight : 0U,
    .acknowledged_ms=stream_acknowledged, .pending=stream_wait || stream_need,
    .waiting_ack=stream_wait && !stream_ack_seen && !stream_uncertain, .fence_pending=stream_wait,
    .acknowledged_valid=stream_known,
    .confirmed=stream_known && stream_acknowledged==stream_desired,
    .stale=stream_stale(now), .attempts=stream_attempts,
    .recovering=stream_uncertain || !stream_known,
    .sample_age_ms=latest_sample.valid ? now-latest_sample.received_at_ms : UINT32_MAX,
    .received_records=latest_sample.accepted_records
  };
}

static void stream_status(void)
{
  epuck_stream_status_t s;
  char message[224];
  epuck_link_get_stream_status(&s);
  snprintf(message,sizeof(message),
    "[STREAM] desired=%u pending=%u in_flight=%u acknowledged=%u ack_valid=%u confirmed=%u attempts=%u recovering=%u waiting_ack=%u fence_pending=%u",
    s.desired_ms,s.pending,s.in_flight_ms,s.acknowledged_ms,s.acknowledged_valid,
    s.confirmed,s.attempts,s.recovering,s.waiting_ack,s.fence_pending);
  command_reply(message);
  snprintf(message,sizeof(message),"[STREAM] age_ms=%lu records=%lu stale=%u late_ack=%lu (age=4294967295 means no sample)",
    (unsigned long)s.sample_age_ms,(unsigned long)s.received_records,s.stale,(unsigned long)stream_late_acks);
  command_reply(message);
}

static void stream_reply_context(void)
{
  output_source=stream_ack_source;
  memcpy(output_session,stream_ack_session,17U);
}

static void stream_recover(void)
{
  stream_known=0U;
  stream_peer_seen=0U; /* Wait for a fresh PONG before reconfiguration. */
  stream_need=MUL_UWB_ONLY || stream_desired!=0U;
  stream_attempts=0U;
  stream_retry_tick=HAL_GetTick();
  if (stream_wait) { stream_uncertain=1U; stream_ack_seen=0U; }
  /* Never reset PING/PONG fence counters: missing replies can delay a fence,
   * but must not let an old OK STREAM confirm a newer request. */
}

static void stream_receive(const char *reply)
{
  uint32_t now=HAL_GetTick();
  if (strcmp(reply,"PONG")==0) { stream_peer_seen=1U; stream_pong_tick=now; }
  else if (strncmp(reply,"BOOT ",5U)==0) stream_recover();
  else if (strcmp(reply,"OK STREAM")==0) {
    if (!stream_wait || stream_ack_seen || stream_uncertain ||
        (uint32_t)(now-stream_sent_tick)>=STREAM_ACK_MS) { ++stream_late_acks; return; }
    stream_ack_seen=stream_known=1U;
    stream_acknowledged=stream_inflight;
    stream_ack_tick=now;
    stream_reply_context();
    char message[128];
    snprintf(message,sizeof(message),"[STREAM] OK STREAM acknowledged=%u ms desired=%u ms%s",
      stream_acknowledged,stream_desired,stream_acknowledged==stream_desired?"":" (new setting still pending)");
    command_reply(message);
  }
}

static void stream_task(uint32_t now)
{
#if MUL_UWB_ONLY
  /* The only permitted value is zero: idempotent stop retries cannot confirm
   * a different period. Normal mode retains its strict ordered ACK fence. */
  if (stream_wait && !stream_ack_seen && (uint32_t)(now-stream_sent_tick)>=STREAM_RETRY_MS) {
    stream_wait=0U; stream_need=1U; stream_attempts=0U; stream_peer_seen=0U;
  }
  if (!stream_wait && !stream_known && !stream_need &&
      (uint32_t)(now-stream_sent_tick)>=STREAM_RETRY_MS) {
    /* A PONG fence may finish after a lost OK STREAM. Zero is still
     * unconfirmed: retry stopping, even though that old fence completed. */
    stream_need=1U; stream_attempts=0U; stream_peer_seen=0U;
  }
#endif
  if (stream_wait) {
    if (!stream_ack_seen && !stream_uncertain && (uint32_t)(now-stream_sent_tick)>=STREAM_ACK_MS) {
      stream_uncertain=1U; stream_known=0U;
      stream_reply_context();
      command_reply("[STREAM] ACK timeout; unconfirmed, draining ordered PONG fence");
    }
    /* The peer queues whole replies in command order. Count *all* transmitted
     * PINGs; a dropped PONG delays this fence rather than completing it early.
     * No subsequent STREAM can transmit until all older ACKs are behind it.
     */
    if ((stream_ack_seen || stream_uncertain) && (int32_t)(pong_count-stream_fence)>=0) {
      stream_wait=0U;
      if (stream_inflight!=stream_desired) { stream_need=1U; stream_attempts=0U; }
      else if (stream_ack_seen) { stream_need=0U; stream_attempts=0U; }
      else if (stream_desired==0U) stream_need=0U; /* One explicit stop, no automatic retry. */
      else {
        stream_need=1U;
        stream_retry_tick=now+(stream_attempts>=STREAM_ATTEMPTS ? STREAM_RETRY_MS : 100U);
      }
    }
  }
  if (!stream_wait && stream_need && stream_attempts>=STREAM_ATTEMPTS &&
      (int32_t)(now-stream_retry_tick)>=0) stream_attempts=0U;
  if (!stream_wait && !stream_need && stream_known && stream_desired && stream_stale(now)) {
    stream_known=0U; stream_need=1U; stream_attempts=0U; stream_retry_tick=now;
    output_source=stream_source; memcpy(output_session,stream_session,17U);
    command_reply("[STREAM] DATA stale; re-establishing desired period (motors unchanged)");
  }
}

static uint8_t stream_ready(uint32_t now)
{
  return stream_need && !stream_wait && stream_peer_seen &&
    (uint32_t)(now-stream_pong_tick)<2500U && stream_attempts<STREAM_ATTEMPTS &&
    (int32_t)(now-stream_retry_tick)>=0;
}

static void stream_begin(uint32_t now)
{
  stream_inflight=stream_desired;
  stream_wait=1U; stream_ack_seen=stream_uncertain=stream_known=stream_need=0U;
  stream_sent_tick=now; ++stream_attempts;
  stream_ack_source=stream_source; memcpy(stream_ack_session,stream_session,17U);
  stream_fence=ping_started+1U;
  tx_pending|=PENDING_PING; /* Next PING follows this STREAM in the shared UART. */
}

static uint8_t accept_data(const char *record)
{
  epuck_sensor_data_t candidate = {0};
  int64_t fields[22];
  const char *p = record + 4;
  uint32_t primask;
  if (*p != ' ') {
    return 0U;
  }
  for (uint32_t i = 0U; i < 22U; ++i) {
    uint32_t positive = i == 0U ? UINT16_MAX :
                        (i < 20U ? INT16_MAX : INT32_MAX);
    uint32_t negative = i == 0U ? 0U :
                        (i < 20U ? 32768U : 2147483648U);
    if (!parse_number(&p, positive, negative, &fields[i])) {
      return 0U;
    }
  }
  while (*p == ' ') {
    ++p;
  }
  if (*p != '\0') {
    return 0U; /* Exactly 22 fields; no extra tokens or trailing garbage. */
  }
  candidate.time_ms = (uint16_t)fields[0];
  for (uint32_t i = 0U; i < 8U; ++i) {
    candidate.prox[i] = (int16_t)fields[1U + i];
    candidate.light[i] = (int16_t)fields[9U + i];
  }
  for (uint32_t i = 0U; i < 3U; ++i) {
    candidate.acc[i] = (int16_t)fields[17U + i];
  }
  candidate.steps_left = (int32_t)fields[20];
  candidate.steps_right = (int32_t)fields[21];
  candidate.received_at_ms = HAL_GetTick();
  candidate.valid = 1U;
  /* Publish only after complete validation; getter can also be used in IRQs. */
  primask = __get_PRIMASK();
  __disable_irq();
  candidate.accepted_records = latest_sample.accepted_records + 1U;
  latest_sample = candidate;
  __set_PRIMASK(primask);
  esp_link_publish_sensor();
  return 1U;
}

const char *epuck_link_flash_error(void)
{
#if MUL_UWB_ONLY
  if (motor_state == MOTOR_ARMED || motor_state == MOTOR_ARMING || motor_arm_pending || motor_count)
    return "ERR configuration rejected: motors armed/pending; issue STOP and wait for OK STOP";
  if (HAL_GetTick() < 2000U)
    return "ERR configuration rejected: startup safety grace; wait 2 seconds and retry";
  if (rx_bytes == 0U) return NULL; /* Preserve silent standalone-board policy. */
  if (!motor_known || motor_state != MOTOR_DISARMED || motor_stop_pending || motor_wait != TX_IDLE)
    return "ERR configuration rejected: STOP unconfirmed; issue STOP, wait OK STOP; inspect MOTOR_STATUS/LINK_STATUS";
  epuck_stream_status_t status;
  epuck_link_get_stream_status(&status);
  if (!status.confirmed || status.acknowledged_ms != 0U)
    return "ERR configuration rejected: STREAM 0 unconfirmed; wait/check STREAM_STATUS and UART1 peer power/cabling";
  return NULL;
#else
  return "ERR persistent configuration requires the UWB-only safety build";
#endif
}
uint8_t epuck_link_flash_safe(void) { return epuck_link_flash_error() == NULL; }

static void motor_stop(const char *reason, uint8_t lost)
{
  motor_stop_subscribers |= output_source == EPUCK_SOURCE_WIFI ? 2U : 1U;
  if (output_source == EPUCK_SOURCE_WIFI) memcpy(motor_stop_wifi_session, output_session, 17U);
  if (motor_owner == EPUCK_SOURCE_USB) motor_stop_subscribers |= 1U;
  else if (motor_owner == EPUCK_SOURCE_WIFI && output_source != EPUCK_SOURCE_WIFI) {
    motor_stop_subscribers |= 2U;
    memcpy(motor_stop_wifi_session, motor_owner_session, 17U);
  }
  motor_stop_source = output_source;
  memcpy(motor_stop_session, output_session, 17U);
  motor_arm_pending = 0U;
  motor_head = motor_tail = motor_count = 0U;
  requested_left = requested_right = 0;
  /* A transmitted command cannot be recalled; STOP is the very next UART
   * command. Ignore its late ACK and require a STOP ACK before another ARM.
   */
  if (motor_wait != TX_STOP) {
    motor_wait = TX_IDLE;
    motor_stop_pending = 1U;
  } else {
    /* Coalesced STOP still owes completion to its latest requesting source. */
    motor_ack_source = output_source;
    memcpy(motor_ack_session, output_session, 17U);
  }
  motor_state = lost ? MOTOR_LOST : MOTOR_STOPPING;
  motor_known = 0U;
  tx_retry = 0U;
  command_reply(reason);
}

void epuck_link_source_lost(epuck_source_t source, const char *reason)
{
  if (active && motor_owner == source) {
    owner_context();
    motor_stop(reason, 1U);
  }
}

static void motor_receive_lost(void)
{
  owner_context();
  if (motor_state == MOTOR_ARMED || motor_state == MOTOR_ARMING) {
    motor_stop("[MOTOR] session lost: UART receive loss; explicit ARM required", 1U);
  }
}

static void motor_check_deadlines(uint32_t now)
{
  owner_context();
  if (motor_wait == TX_STOP) {
    output_source = motor_ack_source;
    memcpy(output_session, motor_ack_session, 17U);
  }
  if ((motor_state == MOTOR_ARMED || motor_state == MOTOR_ARMING) && !owner_available()) {
    motor_stop("[MOTOR] owner connection/CONTROL_HB lost; explicit ARM required", 1U);
  }
  if (motor_wait != TX_IDLE &&
      (uint32_t)(now - motor_wait_tick) >= MOTOR_ACK_TIMEOUT_MS) {
    uint8_t stopping = (motor_wait == TX_STOP);
    motor_wait = TX_IDLE;
    if (!stopping || motor_state != MOTOR_LOST) {
      motor_stop("[MOTOR] session lost: ACK timeout; explicit ARM required", 1U);
    } else {
      motor_stop_pending = 1U; /* Retry STOP, never ARM or movement. */
    }
  } else if (motor_state == MOTOR_ARMED &&
             ((uint32_t)(now - motor_last_ack) >= MOTOR_SESSION_TIMEOUT_MS ||
              (uint32_t)(now - motor_last_keepalive) >= MOTOR_SESSION_TIMEOUT_MS)) {
    motor_stop("[MOTOR] session lost: heartbeat deadline; explicit ARM required", 1U);
  }
}

static void stop_notify(const char *message)
{
  if (motor_stop_subscribers & 1U) usb_console_write(message);
  if (motor_stop_subscribers & 2U) esp_link_reply_to(motor_stop_wifi_session, message);
}

static void motor_reply(const char *reply)
{
  uint8_t command = TX_IDLE;
  char message[128];
  uint32_t now = HAL_GetTick();
  output_source = motor_ack_source;
  memcpy(output_session, motor_ack_session, 17U);
  if (strcmp(reply, "OK ARM") == 0) command = TX_ARM;
  else if (strcmp(reply, "OK M") == 0) command = TX_MOVE;
  else if (strcmp(reply, "OK HB") == 0) command = TX_HB;
  else if (strcmp(reply, "OK STOP") == 0) command = TX_STOP;
  else if (strncmp(reply, "ERR ", 4U) != 0) return;

  snprintf(motor_latest_ack, sizeof(motor_latest_ack), "%.63s", reply);
  if (strncmp(reply, "ERR ", 4U) == 0) {
    snprintf(message, sizeof(message), "[MOTOR] %.100s", reply);
    if (motor_wait == TX_STOP) stop_notify(message); else command_reply(message);
    motor_stop("[MOTOR] peer error; explicit ARM required", 1U);
    return;
  }
  if (command != motor_wait) {
    snprintf(message, sizeof(message), "[MOTOR] ignored late/unexpected %s", reply);
    command_reply(message);
    return;
  }
  motor_wait = TX_IDLE;
  motor_last_ack = now;
  motor_known = 1U;
  if (command == TX_ARM) {
    motor_state = MOTOR_ARMED;
    acknowledged_left = acknowledged_right = 0;
  } else if (command == TX_MOVE) {
    acknowledged_left = motor_wait_left;
    acknowledged_right = motor_wait_right;
  } else if (command == TX_STOP) {
    motor_state = MOTOR_DISARMED;
    motor_owner = EPUCK_SOURCE_NONE;
    motor_stop_pending = 0U;
    acknowledged_left = acknowledged_right = 0;
  }
  snprintf(message, sizeof(message), "[MOTOR] %s", reply);
  if (command == TX_STOP) {
    stop_notify(message);
    motor_stop_subscribers = 0U;
  } else command_reply(message);
}

void epuck_link_command(epuck_source_t source, const char *command)
{
  command_context(source);
  char text[64], message[192];
  const char *p;
  size_t length = strlen(command);
  int64_t left, right;
  if (length >= sizeof(text)) {
    command_reply("ERR oversized command");
    return;
  }
  memcpy(text, command, length + 1U);
  while (length && text[length - 1U] == ' ') text[--length] = '\0';
  p = text;
  while (*p == ' ') ++p;
#if MUL_UWB_ONLY
  if (strcmp(p,"STOP") && strcmp(p,"HELP") && strcmp(p,"MOTOR_STATUS") && strcmp(p,"STREAM_STATUS") && strcmp(p,"LINK_STATUS")) {
    command_reply("ERR UWB-only: ARM, movement and stream changes disabled; STOP remains available");
    return;
  }
#endif
  if (strcmp(p, "HELP") == 0) {
#if MUL_UWB_ONLY
    command_reply("STOP | MOTOR_STATUS | STREAM_STATUS | LINK_STATUS | HELP; UWB-only movement/stream changes disabled");
    return;
#endif
    command_reply("ARM | M <left> <right> | STOP | HELP | MOTOR_STATUS | STREAM <ms> | STREAM_STATUS | LINK_STATUS");
    command_reply("M: integer commanded steps/s, -1000..1000; wait for OK ARM. HB every 100 ms; STOP disarms.");
    command_reply("STREAM: 0 stops automatic DATA; otherwise 50..10000 ms. Queued is not acknowledged.");
    return;
  }
  if (strcmp(p,"STREAM_STATUS")==0) { stream_status(); return; }
  if (strcmp(p,"LINK_STATUS")==0) {
    uint32_t mask=__get_PRIMASK(); __disable_irq();
    uint32_t errors=uart_errors,bits=uart_error_bits,drops=rx_dropped,received=rx_bytes;
    __set_PRIMASK(mask);
    snprintf(message,sizeof(message),"[LINK] rx=%lu uart=%lu bits=0x%lx drops=%lu data=%lu bad=%lu motor=%u stop_pending=%u",
      (unsigned long)received,(unsigned long)errors,(unsigned long)bits,(unsigned long)drops,
      (unsigned long)latest_sample.accepted_records,(unsigned long)malformed_data,motor_state,motor_stop_pending);
    command_reply(message); return;
  }
  if (strcmp(p, "MOTOR_STATUS") == 0) {
    static const char *const states[] = { "DISARMED", "ARMING", "ARMED", "STOPPING", "LOST" };
    snprintf(message, sizeof(message),
        "[MOTOR] %s requested=[%d,%d] acknowledged=[%d,%d] known=%u queued=%u heartbeat=%u owner=%s (commanded steps/s)",
        states[motor_state], requested_left, requested_right,
        acknowledged_left, acknowledged_right, motor_known, motor_count,
        motor_state == MOTOR_ARMED,
        motor_owner == EPUCK_SOURCE_WIFI ? "WIFI" : (motor_owner == EPUCK_SOURCE_USB ? "USB" : "NONE"));
    command_reply(message);
    snprintf(message, sizeof(message), "[MOTOR] latest_ack=\"%s\"", motor_latest_ack);
    command_reply(message);
    return;
  }
  if (!active) {
    command_reply("ERR e-puck link not initialized");
    return;
  }
  if (source == EPUCK_SOURCE_USB && !usb_console_available()) {
    command_reply("ERR USB connection changed; command rejected");
    return;
  }
  if (strncmp(p,"STREAM",6U)==0 && (p[6]==' ' || p[6]=='\0')) {
    p+=6;
    int64_t period;
    if (!parse_number(&p,10000U,0U,&period) || (period!=0 && period<50)) {
      command_reply("ERR STREAM requires exactly one integer: 0 or 50..10000 ms"); return;
    }
    while (*p==' ') ++p;
    if (*p) { command_reply("ERR STREAM requires exactly one integer: 0 or 50..10000 ms"); return; }
    stream_desired=(uint16_t)period;
    stream_source=source; memcpy(stream_session,output_session,17U);
    stream_need=1U; stream_attempts=0U; stream_retry_tick=HAL_GetTick();
    snprintf(message,sizeof(message),"[STREAM] desired=%u ms queued locally; not acknowledged",stream_desired);
    command_reply(message);
    return; /* Independent of motor owner; never refreshes motor watchdogs. */
  }
  if (strcmp(p, "STOP") == 0) {
    motor_stop("[MOTOR] STOP queued; heartbeats disabled", 0U);
  } else if (strcmp(p, "ARM") == 0) {
    if (motor_owner != EPUCK_SOURCE_NONE && motor_owner != source) {
      command_reply("ERR motor session owned by another source"); return;
    }
    if (source == EPUCK_SOURCE_WIFI && !esp_link_owner_alive(output_session)) {
      command_reply("ERR recent CONTROL_HB required before Wi-Fi ARM"); return;
    }
    if ((motor_state != MOTOR_DISARMED && motor_state != MOTOR_ARMED) || motor_stop_pending) {
      command_reply("ERR ARM rejected: wait for OK STOP / current session; STOP first");
      return;
    }
    motor_owner = source;
    memcpy(motor_owner_session, output_session, 17U);
    motor_wait = TX_IDLE;
    motor_head = motor_tail = motor_count = 0U;
    motor_arm_pending = 1U;
    motor_state = MOTOR_ARMING;
    requested_left = requested_right = 0;
    motor_known = 0U;
    command_reply("[MOTOR] ARM queued; wait for OK ARM");
  } else if (p[0] == 'M' && p[1] == ' ') {
    p += 2;
    if (!parse_number(&p, 1000U, 1000U, &left) ||
        !parse_number(&p, 1000U, 1000U, &right)) {
      command_reply("ERR M requires exactly two integer speeds in -1000..1000");
      return;
    }
    while (*p == ' ') ++p;
    if (*p != '\0') {
      command_reply("ERR M requires exactly two integer speeds in -1000..1000");
      return;
    }
    if (motor_owner != source || (source == EPUCK_SOURCE_WIFI && !esp_link_owner_alive(motor_owner_session))) {
      command_reply("ERR motor session owned by another source or lost"); return;
    }
    if (motor_state != MOTOR_ARMED) {
      command_reply("ERR NOT_ARMED: explicit ARM and OK ARM required");
      return;
    }
    if (motor_count == MOTOR_QUEUE_SIZE) {
      command_reply("ERR motor queue full; command rejected");
      return;
    }
    requested_left = (int16_t)left;
    requested_right = (int16_t)right;
    motor_moves[motor_head] = (motor_move_t){(int16_t)left, (int16_t)right};
    motor_head = (uint8_t)((motor_head + 1U) % MOTOR_QUEUE_SIZE);
    ++motor_count;
    snprintf(message, sizeof(message), "[MOTOR] M %d %d queued (commanded steps/s)",
             requested_left, requested_right);
    command_reply(message);
  } else {
    command_reply("ERR unknown/malformed command; type HELP");
  }
}

static uint8_t has_data_prefix(void)
{
  return line_length >= 4U && memcmp(line, "DATA", 4U) == 0;
}

static void parse_byte(uint8_t byte)
{
  if (byte == '\n') {
    if (discard_line) {
      ++bad_lines;
      if (discarded_data || has_data_prefix()) {
        ++malformed_data;
      }
    } else if (line_length != 0U) {
      line[line_length] = '\0';
      if (has_data_prefix() && !accept_data(line)) {
        ++malformed_data;
        ++bad_lines;
      } else {
        memcpy(last_reply, line, line_length + 1U);
        if (strcmp(line, "PONG") == 0) {
          ++pong_count;
        }
        stream_receive(line);
        motor_reply(line);
      }
    }
    line_length = 0U;
    discard_line = saw_cr = discarded_data = 0U;
  } else if (!discard_line) {
    if (saw_cr) {
      discard_line = 1U; /* CR is valid only immediately before LF. */
    } else if (byte == '\r') {
      saw_cr = 1U;
    } else if (byte < 32U || byte > 126U ||
               line_length >= sizeof(line) - 1U) {
      discard_line = 1U;
    } else {
      line[line_length++] = (char)byte;
    }
  }
}

static void consume_rx(void)
{
  for (uint32_t budget = 0U; budget < RX_TASK_BUDGET; ++budget) {
    uint32_t primask = __get_PRIMASK();
    uint8_t byte, motor_fault = 0U;
    __disable_irq();
    if (rx_fault) {
      /* Queued bytes may straddle a loss. Flush in O(1) and discard through
       * a subsequent LF: never combine fragments into a false PONG.
       * Count intentionally flushed bytes as dropped as well.
       */
      rx_dropped += (head + RX_RING_SIZE - tail) % RX_RING_SIZE;
      tail = head;
      rx_fault = 0U;
      motor_fault = 1U;
      discarded_data |= has_data_prefix();
      line_length = 0U;
      saw_cr = 0U;
      discard_line = 1U;
    }
    if (tail == head) {
      __set_PRIMASK(primask);
      if (motor_fault) {
        stream_recover();
        motor_receive_lost();
      }
      break;
    }
    byte = ring[tail];
    tail = (uint16_t)((tail + 1U) % RX_RING_SIZE);
    __set_PRIMASK(primask);
    parse_byte(byte);
  }
}

#if APP_DEBUG_TELEMETRY
static void report_status(uint32_t now)
{
  uint32_t primask, sent, started, requested, received, dropped, errors;
  epuck_sensor_data_t sample;
  epuck_stream_status_t stream;
  uint8_t ready;
  USBD_CDC_HandleTypeDef *cdc;
  int length;
  if ((uint32_t)(now - last_report_tick) < 2000U) {
    return;
  }
  /* USB IRQ may deconfigure/free pClassData; take an atomic readiness check
   * and counter snapshot. CDC_Transmit_FS rechecks readiness on submission.
   */
  primask = __get_PRIMASK();
  __disable_irq();
  cdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
  ready = (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED &&
           cdc != NULL && cdc->TxState == 0U);
  sent = ping_sent;
  started = ping_started;
  requested = stream_requests;
  received = rx_bytes;
  dropped = rx_dropped;
  errors = uart_errors;
  __set_PRIMASK(primask);
  if (!ready) {
    return;
  }
  (void)epuck_link_get_sensor_data(&sample);
  epuck_link_get_stream_status(&stream);
  length = snprintf(usb_report, sizeof(usb_report),
      "[EPUCK] tx=%lu started=%lu rx=%lu pong=%lu drops=%lu errors=%lu tx_fail=%lu bad_lines=%lu stream_tx=%lu data=%lu data_bad=%lu last=\"%s\"\r\n"
      "[SENSOR] valid=%u age_ms=%lu time_ms=%u prox=[%d,%d,%d,%d,%d,%d,%d,%d] light=[%d,%d,%d,%d,%d,%d,%d,%d] acc=[%d,%d,%d] steps=[%ld,%ld]\r\n"
      "[STREAM] desired=%u pending=%u in_flight=%u acknowledged=%u ack_valid=%u confirmed=%u stale=%u attempts=%u late_ack=%lu\r\n",
      (unsigned long)sent, (unsigned long)started, (unsigned long)received,
      (unsigned long)pong_count, (unsigned long)dropped, (unsigned long)errors,
      (unsigned long)tx_failures, (unsigned long)bad_lines,
      (unsigned long)requested, (unsigned long)sample.accepted_records,
      (unsigned long)malformed_data, last_reply,
      (unsigned int)sample.valid,
      (unsigned long)(sample.valid ? now - sample.received_at_ms : 0U),
      (unsigned int)sample.time_ms,
      sample.prox[0], sample.prox[1], sample.prox[2], sample.prox[3],
      sample.prox[4], sample.prox[5], sample.prox[6], sample.prox[7],
      sample.light[0], sample.light[1], sample.light[2], sample.light[3],
      sample.light[4], sample.light[5], sample.light[6], sample.light[7],
      sample.acc[0], sample.acc[1], sample.acc[2],
      (long)sample.steps_left, (long)sample.steps_right,
      stream.desired_ms,stream.pending,stream.in_flight_ms,stream.acknowledged_ms,
      stream.acknowledged_valid,stream.confirmed,stream.stale,stream.attempts,(unsigned long)stream_late_acks);
  /* Static report storage; CDC copies accepted data into its owned buffer
   * and does not overwrite that buffer until transmission completes.
   */
  if (length > 0 && length < (int)sizeof(usb_report) &&
      CDC_Transmit_FS((uint8_t *)usb_report, (uint16_t)length) == USBD_OK) {
    last_report_tick = now;
  }
}

#else
static void report_status(uint32_t now)
{
  static uint32_t reported_errors,reported_drops,reported_bad;
  if ((uint32_t)(now-last_report_tick)<1000U) return;
  last_report_tick=now;
  uint32_t mask=__get_PRIMASK(); __disable_irq();
  uint32_t errors=uart_errors,drops=rx_dropped,bits=uart_error_bits;
  __set_PRIMASK(mask);
  if (errors==reported_errors && drops==reported_drops && bad_lines==reported_bad) return;
  reported_errors=errors; reported_drops=drops; reported_bad=bad_lines;
  char text[192];
  snprintf(text,sizeof(text),"[EPUCK] FAULT uart=%lu bits=0x%lx drops=%lu bad_lines=%lu data_bad=%lu",
    (unsigned long)errors,(unsigned long)bits,(unsigned long)drops,(unsigned long)bad_lines,(unsigned long)malformed_data);
  usb_console_write(text);
}
#endif

static void service_tx(uint32_t now)
{
  uint32_t primask;
  uint8_t command = TX_IDLE, bit = 0U;
  const uint8_t *buffer = NULL;
  uint16_t size;
  if ((uint32_t)(now - last_ping_tick) >= 1000U) {
    last_ping_tick = now;
    tx_pending |= PENDING_PING;
  }
  uint32_t retry_ms = (motor_stop_pending || motor_arm_pending || motor_state == MOTOR_ARMED)
                     ? 10U : 100U;
  if (tx_retry && (uint32_t)(now - last_tx_attempt) < retry_ms) {
    return;
  }
  primask = __get_PRIMASK();
  __disable_irq();
  if (tx_command != TX_IDLE) {
    __set_PRIMASK(primask);
    return;
  }
  /* Reserved STOP slot cannot fill. Heartbeats take priority over queued
   * speed changes, then ARM/movement, then streaming configuration and background PING.
   * Only one motor command awaits acknowledgement at any time.
   */
  if (motor_stop_pending) {
    command = TX_STOP;
    buffer = (const uint8_t *)"STOP\n";
  } else if (motor_wait == TX_IDLE && motor_state == MOTOR_ARMED &&
             (uint32_t)(now - motor_last_keepalive) >= MOTOR_HEARTBEAT_MS) {
    command = TX_HB;
    buffer = (const uint8_t *)"HB\n";
  } else if (motor_wait == TX_IDLE && motor_arm_pending) {
    command = TX_ARM;
    buffer = (const uint8_t *)"ARM\n";
  } else if (motor_wait == TX_IDLE && motor_state == MOTOR_ARMED && motor_count) {
    command = TX_MOVE;
    snprintf(motor_tx_buffer, sizeof(motor_tx_buffer), "M %d %d\n",
             motor_moves[motor_tail].left, motor_moves[motor_tail].right);
    buffer = (const uint8_t *)motor_tx_buffer;
  } else if (stream_ready(now)) {
    command = TX_STREAM;
    snprintf(stream_tx_buffer, sizeof(stream_tx_buffer), "STREAM %u\n", stream_desired);
    buffer = (const uint8_t *)stream_tx_buffer;
  } else if (tx_pending & PENDING_PING) {
    command = TX_PING;
    bit = PENDING_PING;
    buffer = ping;
  }
  if (command == TX_IDLE) {
    __set_PRIMASK(primask);
    return;
  }
  size = (uint16_t)strlen((const char *)buffer);
  if (command >= TX_ARM && command != TX_STOP && !owner_available()) {
    __set_PRIMASK(primask);
    epuck_link_source_lost(motor_owner, "owner connection changed; explicit ARM required");
    return;
  }
  tx_command = command;
  last_tx_attempt = now;
  if (HAL_UART_Transmit_IT(&huart1, buffer, size) == HAL_OK) {
    tx_pending &= (uint8_t)~bit;
    tx_retry = 0U;
    if (command == TX_PING) {
      ++ping_started;
    } else if (command == TX_STREAM) {
      stream_begin(now);
    } else {
      motor_wait = command;
      motor_ack_source = command == TX_STOP ? motor_stop_source : motor_owner;
      memcpy(motor_ack_session, command == TX_STOP ? motor_stop_session : motor_owner_session, 17U);
      motor_wait_tick = now;
      if (command == TX_MOVE) {
        motor_wait_left = motor_moves[motor_tail].left;
        motor_wait_right = motor_moves[motor_tail].right;
        motor_tail = (uint8_t)((motor_tail + 1U) % MOTOR_QUEUE_SIZE);
        --motor_count;
      }
      if (command == TX_STOP) {
        motor_stop_pending = 0U;
      } else if (command == TX_ARM) {
        motor_arm_pending = 0U;
        motor_last_keepalive = now;
      } else {
        motor_last_keepalive = now; /* M/HB refresh the peer watchdog. */
      }
    }
  } else {
    tx_command = TX_IDLE;
    tx_retry = 1U;
    ++tx_failures;
  }
  __set_PRIMASK(primask);
}

void epuck_link_boot_safety(void)
{
#if MUL_UWB_ONLY
  command_context(EPUCK_SOURCE_USB);
  motor_stop("[SAFETY] UWB-only startup STOP; ARM/movement disabled",0U);
  service_tx(HAL_GetTick());
#endif
}

void epuck_link_task(void)
{
  uint32_t now, primask;
  if (!active) {
    return; /* Commenting out init also prevents every task side effect. */
  }
  primask = __get_PRIMASK();
  __disable_irq();
  restart_rx();
  __set_PRIMASK(primask);
  now = HAL_GetTick();
  motor_check_deadlines(now); /* Reject late ACKs before consuming them. */
  consume_rx(); /* At most 64 bytes per invocation; no waiting or printf IRQ. */
  now = HAL_GetTick();
  stream_task(now);
  service_tx(now);
  report_status(now);
}

#else

void epuck_link_init(void) {}
void epuck_link_task(void) {}
void epuck_link_boot_safety(void) {}
uint8_t epuck_link_flash_safe(void) { return 1U; }
const char *epuck_link_flash_error(void) { return NULL; }
void epuck_link_get_stream_status(epuck_stream_status_t *status)
{
  if (status) *status=(epuck_stream_status_t){.sample_age_ms=UINT32_MAX};
}
void epuck_link_source_lost(epuck_source_t source, const char *reason) { (void)source; (void)reason; }
void epuck_link_command(epuck_source_t source, const char *command)
{
  command_context(source);
  if (strcmp(command, "HELP") == 0) {
    command_reply("ARM | M <left> <right> | STOP | HELP | MOTOR_STATUS | STREAM <ms> | STREAM_STATUS | LINK_STATUS");
    command_reply("Commanded steps/s: -1000..1000; e-puck feature is disabled");
  } else {
    command_reply("ERR e-puck feature disabled (APP_EPUCK_ENABLED=0)");
  }
}
uint8_t epuck_link_get_sensor_data(epuck_sensor_data_t *sample)
{
  if (sample != 0) {
    *sample = (epuck_sensor_data_t){0};
  }
  return 0U;
}

#endif

/* Single HAL callback definitions serve both independent link features. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
  if (esp_link_rx_callback(uart)) return;
#if APP_EPUCK_ENABLED
  epuck_RxCplt_callback(uart);
#endif
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
  if (esp_link_tx_callback(uart)) return;
#if APP_EPUCK_ENABLED
  epuck_TxCplt_callback(uart);
#endif
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  if (esp_link_error_callback(uart)) return;
#if APP_EPUCK_ENABLED
  epuck_Error_callback(uart);
#endif
}

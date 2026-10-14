#include "epuck_link.h"
#include "usb_console.h"
#include <stdio.h>
#include <string.h>

#if APP_EPUCK_ENABLED

#include "main.h"
#include "usbd_cdc_if.h"

extern UART_HandleTypeDef huart1;
extern USBD_HandleTypeDef hUsbDeviceFS;

#define RX_RING_SIZE 1024U
#define RX_TASK_BUDGET 64U

static const uint8_t ping[] = "PING\n";
static const uint8_t sensor_request[] = "R\n";
enum { TX_IDLE, TX_PING, TX_SENSOR, TX_ARM, TX_MOVE, TX_HB, TX_STOP };
#define PENDING_PING 1U
#define PENDING_SENSOR 2U
static volatile uint8_t tx_command;
static uint8_t tx_pending, prefer_sensor, tx_retry;
static uint32_t last_tx_attempt;
static uint8_t incoming;
/* ISR produces, task consumes. Critical sections protect consumption, fault
 * flushing and snapshots. One empty slot distinguishes full from empty.
 */
static volatile uint8_t ring[RX_RING_SIZE];
static volatile uint16_t head, tail;
static volatile uint8_t active, rx_fault;
static volatile uint32_t rx_bytes, rx_dropped, uart_errors;
static volatile uint32_t ping_started, ping_sent, sensor_requests;
static uint32_t tx_failures, pong_count, bad_lines, malformed_data;
static uint32_t last_ping_tick, last_report_tick;
static char line[256], last_reply[256], usb_report[1024];
static uint32_t line_length;
static uint8_t discard_line, saw_cr, discarded_data;
static epuck_sensor_data_t latest_sample;

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

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
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

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
  if (active && uart == &huart1) {
    if (tx_command == TX_PING) {
      ++ping_sent;
    } else if (tx_command == TX_SENSOR) {
      ++sensor_requests;
    }
    tx_command = TX_IDLE;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  if (!active || uart != &huart1) {
    return;
  }
  ++uart_errors;
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
  return 1U;
}

static void motor_stop(const char *reason, uint8_t lost)
{
  motor_arm_pending = 0U;
  motor_head = motor_tail = motor_count = 0U;
  requested_left = requested_right = 0;
  /* A transmitted command cannot be recalled; STOP is the very next UART
   * command. Ignore its late ACK and require a STOP ACK before another ARM.
   */
  if (motor_wait != TX_STOP) {
    motor_wait = TX_IDLE;
    motor_stop_pending = 1U;
  }
  motor_state = lost ? MOTOR_LOST : MOTOR_STOPPING;
  motor_known = 0U;
  tx_retry = 0U;
  usb_console_write(reason);
}

void epuck_link_console_lost(const char *reason)
{
  if (active) {
    motor_stop(reason, 1U);
  }
}

static void motor_receive_lost(void)
{
  if (motor_state == MOTOR_ARMED || motor_state == MOTOR_ARMING) {
    motor_stop("[MOTOR] session lost: UART receive loss; explicit ARM required", 1U);
  }
}

static void motor_check_deadlines(uint32_t now)
{
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

static void motor_reply(const char *reply)
{
  uint8_t command = TX_IDLE;
  char message[128];
  uint32_t now = HAL_GetTick();
  if (strcmp(reply, "OK ARM") == 0) command = TX_ARM;
  else if (strcmp(reply, "OK M") == 0) command = TX_MOVE;
  else if (strcmp(reply, "OK HB") == 0) command = TX_HB;
  else if (strcmp(reply, "OK STOP") == 0) command = TX_STOP;
  else if (strncmp(reply, "ERR ", 4U) != 0) return;

  snprintf(motor_latest_ack, sizeof(motor_latest_ack), "%.63s", reply);
  if (strncmp(reply, "ERR ", 4U) == 0) {
    snprintf(message, sizeof(message), "[MOTOR] %.100s", reply);
    usb_console_write(message);
    motor_stop("[MOTOR] peer error; explicit ARM required", 1U);
    return;
  }
  if (command != motor_wait) {
    snprintf(message, sizeof(message), "[MOTOR] ignored late/unexpected %s", reply);
    usb_console_write(message);
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
    motor_stop_pending = 0U;
    acknowledged_left = acknowledged_right = 0;
  }
  snprintf(message, sizeof(message), "[MOTOR] %s", reply);
  usb_console_write(message);
}

void epuck_link_console_command(const char *command)
{
  char text[64], message[192];
  const char *p;
  size_t length = strlen(command);
  int64_t left, right;
  if (length >= sizeof(text)) {
    usb_console_write("ERR oversized command");
    return;
  }
  memcpy(text, command, length + 1U);
  while (length && text[length - 1U] == ' ') text[--length] = '\0';
  p = text;
  while (*p == ' ') ++p;
  if (strcmp(p, "HELP") == 0) {
    usb_console_write("ARM | M <left> <right> | STOP | HELP | MOTOR_STATUS");
    usb_console_write("M: integer commanded steps/s, -1000..1000; wait for OK ARM. HB every 100 ms; STOP disarms.");
    return;
  }
  if (strcmp(p, "MOTOR_STATUS") == 0) {
    static const char *const states[] = { "DISARMED", "ARMING", "ARMED", "STOPPING", "LOST" };
    snprintf(message, sizeof(message),
        "[MOTOR] %s requested=[%d,%d] acknowledged=[%d,%d] known=%u queued=%u heartbeat=%u (commanded steps/s)",
        states[motor_state], requested_left, requested_right,
        acknowledged_left, acknowledged_right, motor_known, motor_count,
        motor_state == MOTOR_ARMED);
    usb_console_write(message);
    snprintf(message, sizeof(message), "[MOTOR] latest_ack=\"%s\"", motor_latest_ack);
    usb_console_write(message);
    return;
  }
  if (!active) {
    usb_console_write("ERR e-puck link not initialized");
    return;
  }
  if (!usb_console_available()) {
    usb_console_write("ERR USB connection changed; command rejected");
    return;
  }
  if (strcmp(p, "STOP") == 0) {
    motor_stop("[MOTOR] STOP queued; heartbeats disabled", 0U);
  } else if (strcmp(p, "ARM") == 0) {
    if ((motor_state != MOTOR_DISARMED && motor_state != MOTOR_ARMED) || motor_stop_pending) {
      usb_console_write("ERR ARM rejected: wait for OK STOP / current session; STOP first");
      return;
    }
    motor_wait = TX_IDLE;
    motor_head = motor_tail = motor_count = 0U;
    motor_arm_pending = 1U;
    motor_state = MOTOR_ARMING;
    requested_left = requested_right = 0;
    motor_known = 0U;
    usb_console_write("[MOTOR] ARM queued; wait for OK ARM");
  } else if (p[0] == 'M' && p[1] == ' ') {
    p += 2;
    if (!parse_number(&p, 1000U, 1000U, &left) ||
        !parse_number(&p, 1000U, 1000U, &right)) {
      usb_console_write("ERR M requires exactly two integer speeds in -1000..1000");
      return;
    }
    while (*p == ' ') ++p;
    if (*p != '\0') {
      usb_console_write("ERR M requires exactly two integer speeds in -1000..1000");
      return;
    }
    if (motor_state != MOTOR_ARMED) {
      usb_console_write("ERR NOT_ARMED: explicit ARM and OK ARM required");
      return;
    }
    if (motor_count == MOTOR_QUEUE_SIZE) {
      usb_console_write("ERR motor queue full; command rejected");
      return;
    }
    requested_left = (int16_t)left;
    requested_right = (int16_t)right;
    motor_moves[motor_head] = (motor_move_t){(int16_t)left, (int16_t)right};
    motor_head = (uint8_t)((motor_head + 1U) % MOTOR_QUEUE_SIZE);
    ++motor_count;
    snprintf(message, sizeof(message), "[MOTOR] M %d %d queued (commanded steps/s)",
             requested_left, requested_right);
    usb_console_write(message);
  } else {
    usb_console_write("ERR unknown/malformed command; type HELP");
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

static void report_status(uint32_t now)
{
  uint32_t primask, sent, started, requested, received, dropped, errors;
  epuck_sensor_data_t sample;
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
  requested = sensor_requests;
  received = rx_bytes;
  dropped = rx_dropped;
  errors = uart_errors;
  __set_PRIMASK(primask);
  if (!ready) {
    return;
  }
  (void)epuck_link_get_sensor_data(&sample);
  length = snprintf(usb_report, sizeof(usb_report),
      "[EPUCK] tx=%lu started=%lu rx=%lu pong=%lu drops=%lu errors=%lu tx_fail=%lu bad_lines=%lu sensor_req=%lu data=%lu data_bad=%lu last=\"%s\"\r\n"
      "[SENSOR] valid=%u age_ms=%lu time_ms=%u prox=[%d,%d,%d,%d,%d,%d,%d,%d] light=[%d,%d,%d,%d,%d,%d,%d,%d] acc=[%d,%d,%d] steps=[%ld,%ld]\r\n",
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
      (long)sample.steps_left, (long)sample.steps_right);
  /* Static report storage; CDC copies accepted data into its owned buffer
   * and does not overwrite that buffer until transmission completes.
   */
  if (length > 0 && length < (int)sizeof(usb_report) &&
      CDC_Transmit_FS((uint8_t *)usb_report, (uint16_t)length) == USBD_OK) {
    last_report_tick = now;
  }
}

static void service_tx(uint32_t now)
{
  uint32_t primask;
  uint8_t command = TX_IDLE, bit = 0U;
  const uint8_t *buffer = NULL;
  uint16_t size;
  if ((uint32_t)(now - last_ping_tick) >= 1000U) {
    last_ping_tick = now;
    tx_pending |= PENDING_PING | PENDING_SENSOR;
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
   * speed changes, then ARM/movement, then the existing background polls.
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
  } else if (tx_pending & PENDING_SENSOR &&
             (prefer_sensor || !(tx_pending & PENDING_PING))) {
    command = TX_SENSOR;
    bit = PENDING_SENSOR;
    buffer = sensor_request;
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
  if (command >= TX_ARM && command != TX_STOP && !usb_console_available()) {
    __set_PRIMASK(primask);
    epuck_link_console_lost("USB connection changed; explicit ARM required");
    return;
  }
  tx_command = command;
  last_tx_attempt = now;
  if (HAL_UART_Transmit_IT(&huart1, buffer, size) == HAL_OK) {
    tx_pending &= (uint8_t)~bit;
    tx_retry = 0U;
    if (command == TX_PING || command == TX_SENSOR) {
      prefer_sensor = (command == TX_PING);
      if (command == TX_PING) {
        ++ping_started;
      }
    } else {
      motor_wait = command;
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
  if (!usb_console_available() &&
      (motor_state == MOTOR_ARMED || motor_state == MOTOR_ARMING)) {
    epuck_link_console_lost("USB disconnected; explicit ARM required");
  }
  motor_check_deadlines(now); /* Reject late ACKs before consuming them. */
  consume_rx(); /* At most 64 bytes per invocation; no waiting or printf IRQ. */
  now = HAL_GetTick();
  service_tx(now);
  report_status(now);
}

#else

void epuck_link_init(void) {}
void epuck_link_task(void) {}
void epuck_link_console_lost(const char *reason) { (void)reason; }
void epuck_link_console_command(const char *command)
{
  if (strcmp(command, "HELP") == 0) {
    usb_console_write("ARM | M <left> <right> | STOP | HELP | MOTOR_STATUS");
    usb_console_write("Commanded steps/s: -1000..1000; e-puck feature is disabled");
  } else {
    usb_console_write("ERR e-puck feature disabled (APP_EPUCK_ENABLED=0)");
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

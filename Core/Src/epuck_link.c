#include "epuck_link.h"

#if EPUCK_LINK_ENABLED

#include "main.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;
extern USBD_HandleTypeDef hUsbDeviceFS;

#define RX_RING_SIZE 256U
#define RX_TASK_BUDGET 64U

static const uint8_t ping[] = "PING\n";
static uint8_t incoming;
/* ISR produces, task consumes. Critical sections protect consumption, fault
 * flushing and snapshots. One empty slot distinguishes full from empty.
 */
static volatile uint8_t ring[RX_RING_SIZE];
static volatile uint16_t head, tail;
static volatile uint8_t active, rx_fault;
static volatile uint32_t rx_bytes, rx_dropped, uart_errors;
static volatile uint32_t ping_started, ping_sent;
static uint32_t tx_failures, pong_count, bad_lines;
static uint32_t last_ping_tick, last_report_tick;
static char line[64], last_reply[64], usb_report[320];
static uint32_t line_length;
static uint8_t discard_line, saw_cr;

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
    ++ping_sent;
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

static void parse_byte(uint8_t byte)
{
  if (byte == '\n') {
    if (discard_line) {
      ++bad_lines;
    } else if (line_length != 0U) {
      line[line_length] = '\0';
      memcpy(last_reply, line, line_length + 1U);
      if (strcmp(line, "PONG") == 0) {
        ++pong_count;
      }
    }
    line_length = 0U;
    discard_line = saw_cr = 0U;
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
    uint8_t byte;
    __disable_irq();
    if (rx_fault) {
      /* Queued bytes may straddle a loss. Flush in O(1) and discard through
       * a subsequent LF: never combine fragments into a false PONG.
       * Count intentionally flushed bytes as dropped as well.
       */
      rx_dropped += (head + RX_RING_SIZE - tail) % RX_RING_SIZE;
      tail = head;
      rx_fault = 0U;
      line_length = 0U;
      saw_cr = 0U;
      discard_line = 1U;
    }
    if (tail == head) {
      __set_PRIMASK(primask);
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
  uint32_t primask, sent, started, received, dropped, errors;
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
  received = rx_bytes;
  dropped = rx_dropped;
  errors = uart_errors;
  __set_PRIMASK(primask);
  if (!ready) {
    return;
  }
  length = snprintf(usb_report, sizeof(usb_report),
      "[EPUCK] tx=%lu started=%lu rx=%lu pong=%lu drops=%lu errors=%lu tx_fail=%lu bad_lines=%lu last=\"%s\"\r\n",
      (unsigned long)sent, (unsigned long)started, (unsigned long)received,
      (unsigned long)pong_count, (unsigned long)dropped, (unsigned long)errors,
      (unsigned long)tx_failures, (unsigned long)bad_lines, last_reply);
  /* Static report storage; CDC copies accepted data into its owned buffer
   * and does not overwrite that buffer until transmission completes.
   */
  if (length > 0 && length < (int)sizeof(usb_report) &&
      CDC_Transmit_FS((uint8_t *)usb_report, (uint16_t)length) == USBD_OK) {
    last_report_tick = now;
  }
}

void epuck_linkk_task(void)
{
  uint32_t now, primask;
  if (!active) {
    return; /* Commenting out init also prevents every task side effect. */
  }
  primask = __get_PRIMASK();
  __disable_irq();
  restart_rx();
  __set_PRIMASK(primask);
  consume_rx(); /* At most 64 bytes per invocation; no waiting or printf IRQ. */
  now = HAL_GetTick();
  if ((uint32_t)(now - last_ping_tick) >= 1000U) {
    /* Retry each second even after HAL_BUSY/error or with no peer present.
     * Static PING storage remains valid through the interrupt-driven TX.
     * This is the only command emitted: no motor commands or arming.
     */
    last_ping_tick = now;
    if (HAL_UART_Transmit_IT(&huart1, ping, sizeof(ping) - 1U) == HAL_OK) {
      ++ping_started;
    } else {
      ++tx_failures;
    }
  }
  report_status(now);
}

#else

void epuck_link_init(void) {}
void epuck_link_task(void) {}

#endif

#include "usb_console.h"
#include "epuck_link.h"
#include "esp_link.h"
#include "main.h"
#include "uwb_runtime.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>

extern USBD_HandleTypeDef hUsbDeviceFS;

#define CONSOLE_RING_SIZE 1024U
#define CONSOLE_BUDGET 128U
#define CONSOLE_MESSAGES 16U
#define CONSOLE_MESSAGE_SIZE 192U
static volatile uint8_t console_ring[CONSOLE_RING_SIZE];
static volatile uint16_t console_head, console_tail;
static volatile uint8_t console_overflow, console_disconnect;
static char console_line[64], console_output[CONSOLE_MESSAGE_SIZE];
static uint32_t console_length, console_dropped_messages;
static uint8_t console_discard, console_cr, console_was_connected;
static char console_messages[CONSOLE_MESSAGES][CONSOLE_MESSAGE_SIZE];
static uint8_t console_message_head, console_message_tail, console_message_count;

void usb_console_receive_isr(const uint8_t *bytes, uint32_t length)
{
  /* Full-speed CDC packets are small; also bound unexpected callback lengths. */
  for (uint32_t i = 0U; i < length && i < CONSOLE_RING_SIZE; ++i) {
    uint16_t next = (uint16_t)((console_head + 1U) % CONSOLE_RING_SIZE);
    if (console_overflow || next == console_tail) {
      console_overflow = 1U;
      break;
    }
    console_ring[console_head] = bytes[i];
    console_head = next;
  }
  if (length > CONSOLE_RING_SIZE) {
    console_overflow = 1U;
  }
}

void usb_console_disconnected_isr(void)
{
  /* Sticky flag catches a disconnect/reconnect entirely between task calls. */
  console_disconnect = 1U;
}

uint8_t usb_console_available(void)
{
  uint32_t primask = __get_PRIMASK();
  uint8_t available;
  __disable_irq();
  available = (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED &&
               !console_disconnect && !console_overflow);
  __set_PRIMASK(primask);
  return available;
}

bool usb_console_try_write(const char *message)
{
  if (strlen(message) > CONSOLE_MESSAGE_SIZE - 3U || console_message_count == CONSOLE_MESSAGES) return false;
  snprintf(console_messages[console_message_head], CONSOLE_MESSAGE_SIZE, "%s\r\n", message);
  console_message_head = (uint8_t)((console_message_head + 1U) % CONSOLE_MESSAGES);
  ++console_message_count;
  return true;
}

void usb_console_write(const char *message)
{
  if (console_message_count == CONSOLE_MESSAGES) {
    ++console_dropped_messages;
    return;
  }
  snprintf(console_messages[console_message_head], CONSOLE_MESSAGE_SIZE,
           "%s\r\n", message);
  console_message_head = (uint8_t)((console_message_head + 1U) % CONSOLE_MESSAGES);
  ++console_message_count;
}

static void console_parse_byte(uint8_t byte)
{
  if (byte == '\n') {
    if (console_discard) {
      usb_console_write("ERR malformed or oversized USB command");
    } else if (console_length != 0U) {
      console_line[console_length] = '\0';
      #if MUL_UNIVERSAL
      if (uwb_runtime_command(console_line)) {}
      else
#endif
      if (strcmp(console_line,"ESP_STATUS")==0) esp_link_usb_status();
      else {
        if (strcmp(console_line,"HELP")==0) usb_console_write("ESP_STATUS: STM32 ESP session and fault diagnostics");
        epuck_link_console_command(console_line);
      }
    }
    console_length = 0U;
    console_discard = console_cr = 0U;
  } else if (!console_discard) {
    if (console_cr || byte < 32U || byte > 126U ||
        console_length >= sizeof(console_line) - 1U) {
      if (byte == '\r' && !console_cr) {
        console_cr = 1U;
      } else {
        console_discard = 1U;
      }
    } else {
      console_line[console_length++] = (char)byte;
    }
  }
}

void usb_console_task(void)
{
  uint32_t primask = __get_PRIMASK();
  uint8_t disconnected, overflow, configured;
  USBD_CDC_HandleTypeDef *cdc;
  __disable_irq();
  configured = (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED);
  disconnected = console_disconnect || (console_was_connected && !configured);
  overflow = console_overflow;
  console_disconnect = console_overflow = 0U;
  if (disconnected || overflow || !configured) {
    console_tail = console_head;
    console_length = 0U;
    console_cr = 0U;
    console_discard = overflow;
  }
  console_was_connected = configured;
  __set_PRIMASK(primask);
  if (disconnected) {
    console_message_head = console_message_tail = console_message_count = 0U;
    epuck_link_console_lost("USB disconnected; explicit ARM required");
  }
  if (overflow) {
    epuck_link_console_lost("USB input overflow; explicit ARM required");
    usb_console_write("ERR USB input overflow; discarded queued commands");
  }
  if (!configured) {
    return;
  }
  for (uint32_t budget = 0U; budget < CONSOLE_BUDGET; ++budget) {
    uint8_t byte;
    primask = __get_PRIMASK();
    __disable_irq();
    /* Defer new loss events to the next task instead of parsing stale input. */
    if (console_disconnect || console_overflow || console_tail == console_head) {
      __set_PRIMASK(primask);
      break;
    }
    byte = console_ring[console_tail];
    console_tail = (uint16_t)((console_tail + 1U) % CONSOLE_RING_SIZE);
    __set_PRIMASK(primask);
    console_parse_byte(byte);
  }
  primask = __get_PRIMASK();
  __disable_irq();
  cdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
  configured = (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED &&
                cdc != NULL && cdc->TxState == 0U);
  __set_PRIMASK(primask);
  if (!configured) {
    return;
  }
  if (console_dropped_messages) {
    snprintf(console_output, sizeof(console_output),
             "[CONSOLE] %lu messages dropped (output queue full)\r\n",
             (unsigned long)console_dropped_messages);
    if (CDC_Transmit_FS((uint8_t *)console_output,
                        (uint16_t)strlen(console_output)) == USBD_OK) {
      console_dropped_messages = 0U;
    }
  } else if (console_message_count) {
    /* Never rewrite this persistent buffer until CDC is idle. The shared
     * CDC implementation also copies accepted bytes into owned TX storage.
     */
    memcpy(console_output, console_messages[console_message_tail], sizeof(console_output));
    if (CDC_Transmit_FS((uint8_t *)console_output,
                        (uint16_t)strlen(console_output)) == USBD_OK) {
      console_message_tail = (uint8_t)((console_message_tail + 1U) % CONSOLE_MESSAGES);
      --console_message_count;
    }
  }
}

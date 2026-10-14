#ifndef USB_CONSOLE_H
#define USB_CONSOLE_H

#include <stdint.h>

/* ISR hooks only copy bytes/mark connection loss; never transmit or parse. */
void usb_console_receive_isr(const uint8_t *bytes, uint32_t length);
void usb_console_disconnected_isr(void);
/* Configured connection with no unprocessed disconnect/overflow event. */
uint8_t usb_console_available(void);
/* Main-loop processing, including when APP_EPUCK_ENABLED is zero. */
void usb_console_task(void);
/* Main-loop only: enqueue a bounded message for an idle/configured CDC. */
void usb_console_write(const char *message);

#endif

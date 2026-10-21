#ifndef ESP_LINK_H
#define ESP_LINK_H
#include <stdint.h>
void esp_link_init(void);
void esp_link_set_enabled(uint8_t enabled);
uint8_t esp_link_telemetry(const char *line);
uint32_t esp_link_uart_errors(void);
uint32_t esp_link_telemetry_dropped(void);
void esp_link_reset_diagnostics(void);
void esp_link_task(void);
/* Main-loop USB-only diagnostic snapshot, even when UART3 is broken. */
void esp_link_usb_status(void);
void esp_link_publish_sensor(void);
/* HAL callbacks delegate USART3 only; return 1 when handled. */
uint8_t esp_link_rx_callback(void *uart);
uint8_t esp_link_tx_callback(void *uart);
uint8_t esp_link_error_callback(void *uart);
void esp_link_log(const char *text);
void esp_link_reply_to(const char *session, const char *text);
void esp_link_session(char out[17]);
uint8_t esp_link_owner_alive(const char *session);
#endif

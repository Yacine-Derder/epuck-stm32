#ifndef EPUCK_LINK_H
#define EPUCK_LINK_H

#include "app_config.h"
#include <stdint.h>

/* APP_EPUCK_ENABLED controls all activity, IRQ setup and callbacks.
 * Alternatively, comment out BOTH calls in main.c and rebuild.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Existing USART1: PA9 TX, PA10 RX, 115200 8N1.
 * Init enables reception and its NVIC interrupt; task must run regularly.
 * With the switch off, both functions are side-effect-free stubs.
 */
void epuck_link_init(void);
void epuck_link_task(void);

/* Raw sensor readings, without calibration or unit conversion.
 * Accelerometer readings require e-puck JE1 bridges 33-34, 35-36, 37-38.
 * Step counters describe commanded steps, not measured wheel motion.
 */
typedef struct {
  uint8_t valid;
  uint16_t time_ms;              /* Peer clock wraps every 65536 ms. */
  int16_t prox[8];
  int16_t light[8];
  int16_t acc[3];
  int32_t steps_left;
  int32_t steps_right;
  uint32_t received_at_ms;       /* STM32 HAL tick when record was accepted. */
  uint32_t accepted_records;     /* Increments only on fully valid DATA. */
} epuck_sensor_data_t;

/* Copy a coherent snapshot; returns its valid flag (0 for NULL/disabled).
 * Before the first record, the sample is zeroed/invalid. Invalid input never
 * changes the previous sample. Staleness: HAL_GetTick() - received_at_ms,
 * using unsigned subtraction. Both 32-bit tick and record count may wrap.
 */
uint8_t epuck_link_get_sensor_data(epuck_sensor_data_t *sample);

/* Main-loop USB console hooks. Motor commands require an explicit ARM and
 * its acknowledgement; speeds are commanded steps/s, not measured speed.
 * Console command handling still reports disabled when the switch is off.
 */
typedef enum { EPUCK_SOURCE_USB, EPUCK_SOURCE_WIFI, EPUCK_SOURCE_NONE } epuck_source_t;
void epuck_link_command(epuck_source_t source, const char *command);
void epuck_link_source_lost(epuck_source_t source, const char *reason);
void epuck_link_console_command(const char *command);
void epuck_link_console_lost(const char *reason);

#ifdef __cplusplus
}
#endif

#endif

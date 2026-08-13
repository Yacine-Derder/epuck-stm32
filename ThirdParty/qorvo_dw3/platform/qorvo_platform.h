#ifndef QORVO_PLATFORM_H
#define QORVO_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "deca_device_api.h"
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Board role                                                                 */
/* -------------------------------------------------------------------------- */
typedef enum
{
    QORVO_ROLE_TX = 0,
    QORVO_ROLE_RX = 1
} qorvo_role_t;

/* -------------------------------------------------------------------------- */
/* Public high-level helpers                                                  */
/* -------------------------------------------------------------------------- */
int32_t qorvo_probe_and_init(void);
uint32_t qorvo_read_devid(void);
void qorvo_basic_test(void);

int32_t qorvo_configure_radio_default(void);
void qorvo_config_test(void);

int32_t qorvo_tx_test_once(void);
void qorvo_tx_test(void);

void qorvo_tx_timestamp_test(void);
void qorvo_rx_timeout_test(void);

/* -------------------------------------------------------------------------- */
/* Interrupt-driven link test API                                             */
/* -------------------------------------------------------------------------- */
int32_t qorvo_link_init(uint8_t board_id, qorvo_role_t role);
void qorvo_link_process(void);

/* -------------------------------------------------------------------------- */
/* SPI low-level callbacks exposed to the Qorvo driver                        */
/* -------------------------------------------------------------------------- */
int32_t readfromspi(uint16_t headerLength,
                    uint8_t *headerBuffer,
                    uint16_t readlength,
                    uint8_t *readBuffer);

int32_t writetospi(uint16_t headerLength,
                   const uint8_t *headerBuffer,
                   uint16_t bodyLength,
                   const uint8_t *bodyBuffer);

int32_t writetospiwithcrc(uint16_t headerLength,
                          const uint8_t *headerBuffer,
                          uint16_t bodyLength,
                          const uint8_t *bodyBuffer,
                          uint8_t crc8);

void qorvo_spi_set_slowrate(void);
void qorvo_spi_set_fastrate(void);

/* -------------------------------------------------------------------------- */
/* Timing hooks                                                               */
/* -------------------------------------------------------------------------- */
void deca_sleep(unsigned int time_ms);
void deca_usleep(unsigned long time_us);

/* -------------------------------------------------------------------------- */
/* IRQ / critical-section hooks                                               */
/* -------------------------------------------------------------------------- */
decaIrqStatus_t decamutexon(void);
void decamutexoff(decaIrqStatus_t s);

/* -------------------------------------------------------------------------- */
/* Board/platform helpers                                                     */
/* -------------------------------------------------------------------------- */
void qorvo_hw_reset(void);
void qorvo_wakeup_device_with_io(void);

#ifdef __cplusplus
}
#endif

#endif /* QORVO_PLATFORM_H */
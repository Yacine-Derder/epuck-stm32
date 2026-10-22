#include "qorvo_platform.h"
#include "mul_timing.h"

decaIrqStatus_t decamutexon(void)
{
    decaIrqStatus_t s = __get_PRIMASK();
#if MUL_UNIVERSAL
    /* Universal EXTI0 stays disabled; only the main loop owns the radio/SPI.
     * UART ISRs do not call this driver. A global mask at 1.3125MHz would hide
     * several 115200-baud RX bytes during one frame transfer. */
    return s;
#else
    __disable_irq();
    if (s == 0) mul_timing_spi_begin();
    return s;
#endif
}

void decamutexoff(decaIrqStatus_t s)
{
#if MUL_UNIVERSAL
    (void)s; /* Preserve caller PRIMASK; never enable IRQs on its behalf. */
#else
    if (s == 0) {
        mul_timing_spi_end();
        __enable_irq();
    }
#endif
}
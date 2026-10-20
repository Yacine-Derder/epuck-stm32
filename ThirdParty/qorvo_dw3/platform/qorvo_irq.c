#include "qorvo_platform.h"
#include "mul_timing.h"

decaIrqStatus_t decamutexon(void)
{
    decaIrqStatus_t s = __get_PRIMASK();
    __disable_irq();
    if (s == 0) mul_timing_spi_begin();
    return s;
}

void decamutexoff(decaIrqStatus_t s)
{
    if (s == 0) {
        mul_timing_spi_end();
        __enable_irq();
    }
}
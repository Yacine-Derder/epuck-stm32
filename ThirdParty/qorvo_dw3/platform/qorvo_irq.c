#include "qorvo_platform.h"

decaIrqStatus_t decamutexon(void)
{
    decaIrqStatus_t s = __get_PRIMASK();
    __disable_irq();
    return s;
}

void decamutexoff(decaIrqStatus_t s)
{
    if (s == 0) {
        __enable_irq();
    }
}
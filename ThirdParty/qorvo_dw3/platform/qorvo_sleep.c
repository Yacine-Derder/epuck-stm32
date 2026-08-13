#include "qorvo_platform.h"

static void qorvo_enable_cycle_counter(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0;
}

void deca_sleep(unsigned int time_ms)
{
    HAL_Delay(time_ms);
}

void deca_usleep(unsigned long time_us)
{
    uint32_t start;
    uint32_t cycles;

    qorvo_enable_cycle_counter();

    start = DWT->CYCCNT;
    cycles = (SystemCoreClock / 1000000UL) * (uint32_t)time_us;

    while ((DWT->CYCCNT - start) < cycles) {
    }
}
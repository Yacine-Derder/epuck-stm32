#ifndef TEST_MAIN_H
#define TEST_MAIN_H
#include <stdint.h>
uint32_t HAL_GetTick(void);
#endif
#define EXTI0_IRQn 6
#define DWM_IRQ_Pin 1
void HAL_NVIC_DisableIRQ(int irq);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_Delay(uint32_t ms);

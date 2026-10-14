#include "uwb_link.h"
#include "main.h"

#if APP_UWB_ENABLED
#include "qorvo_platform.h"
#include <stdio.h>

/* Shared with the DWM3000 EXTI callback. Set before enabling the IRQ. */
static volatile uint8_t ready;
#endif

void uwb_link_init(void)
{
  /* CubeMX GPIO setup enables EXTI0. Take ownership before touching the
   * radio; a disabled or failed link must leave the interrupt disabled.
   */
  HAL_NVIC_DisableIRQ(EXTI0_IRQn);

#if APP_UWB_ENABLED
  ready = 0U;
  /* Preserve the existing startup settling delays and diagnostics. */
  HAL_Delay(100U);
  if (qorvo_link_init(BOARD_ID, BOARD_ROLE) != DWT_SUCCESS) {
    printf("qorvo_link_init() failed\r\n");
  } else {
    printf("qorvo_link_init() OK\r\n");
    ready = 1U;
    /* Retain any pending radio event from initialization: enabling EXTI0
     * now delivers it to the guarded callback and the original driver.
     */
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
  }
  HAL_Delay(100U);
#endif
}

void uwb_link_task(void)
{
#if APP_UWB_ENABLED
  if (ready) {
    qorvo_link_process();
  }
#endif
}

void uwb_link_exti_callback(uint16_t gpio_pin)
{
#if APP_UWB_ENABLED
  if (ready && gpio_pin == DWM_IRQ_Pin) {
    dwt_isr();
  }
#else
  (void)gpio_pin;
#endif
}

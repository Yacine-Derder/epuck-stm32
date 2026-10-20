#include "uwb_link.h"
#include "main.h"
#include "phy_interop.h"
#include "mul_diag.h"
#include "mul_ads_radio.h"
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
#include "usb_console.h"
#endif

#if APP_UWB_ENABLED || PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
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

#if APP_UWB_ENABLED || PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
  ready = 0U;
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
#if MUL_TWR_ROLE
  if (mul_ads_radio_init()==DWT_SUCCESS) ready=1U;
  else usb_console_write("MULH INIT FAILED");
#elif MUL_DIAG_ROLE
  if (mul_diag_init()==DWT_SUCCESS) ready=1U;
  else usb_console_write("MULG INIT FAILED");
#else
  if (phy_interop_init()==DWT_SUCCESS) ready=1U;
  else usb_console_write("PHY INIT FAILED");
#endif
  // Diagnostic polling owns radio events; EXTI remains disabled.
#else
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
#endif
}

void uwb_link_task(void)
{
#if APP_UWB_ENABLED || PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
  if (ready) {
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
#if MUL_TWR_ROLE
    mul_ads_radio_process();
#elif MUL_DIAG_ROLE
    mul_diag_process();
#else
    phy_interop_process();
#endif
#else
    qorvo_link_process();
#endif
  }
#endif
}

void uwb_link_exti_callback(uint16_t gpio_pin)
{
#if APP_UWB_ENABLED || PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
  (void)gpio_pin;
#else
  if (ready && gpio_pin == DWM_IRQ_Pin) {
    dwt_isr();
  }
#endif
#else
  (void)gpio_pin;
#endif
}

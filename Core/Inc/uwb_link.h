#ifndef UWB_LINK_H
#define UWB_LINK_H

#include "app_config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call after peripheral setup. Controls DWM3000 EXTI0; when disabled,
 * init only disables that IRQ and performs no Qorvo communication.
 */
void uwb_link_init(void);
/* Safe before init, when disabled, or after initialization failure. */
void uwb_link_task(void);
/* Forward the DWM3000 GPIO EXTI callback; ignored until init succeeds. */
void uwb_link_exti_callback(uint16_t gpio_pin);

#ifdef __cplusplus
}
#endif

#endif

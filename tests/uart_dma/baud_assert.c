#include "stm32f4xx_hal.h"
_Static_assert(UART_BRR_SAMPLING16(42000000U,921600U)==45U,"bundled HAL divisor at42MHz");
_Static_assert(UART_BRR_SAMPLING16(42000000U,115200U)==364U,"legacy USART3 divider");
_Static_assert(UART_BRR_SAMPLING16(84000000U,115200U)==729U,"unchanged USART1 onAPB2");

#include "app_config.h"
_Static_assert(APP_ESP_BRR_NEAREST(42000000U,921600U)==46U,"corrected experimental USART3 divider");

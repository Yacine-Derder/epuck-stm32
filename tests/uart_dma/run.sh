#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -g -fsanitize=address,undefined \
 -DMUL_UNIVERSAL=1 -DMUL_UWB_ONLY=1 -DMUL_ROBOT_EXPERIMENT=1 -DAPP_ESP_DMA_RX=1 -DAPP_ESP_BAUD=921600U \
 -DAPP_EPUCK_STREAM_PERIOD_MS=100U -DAPP_WIFI_SENSOR_PERIOD_MS=100U \
 -Itests/esp_link/stubs -ICore/Inc -IThirdParty/mul_core tests/uart_dma/test.c -o "$work/test"
for case_name in initfail wrap burst errors fairness sustained; do "$work/test" "$case_name"; done
# Compile assertions against the actual installed STM32 HAL baud macros.
arm-none-eabi-gcc -c -mcpu=cortex-m4 -mthumb -DSTM32F405xx -DUSE_HAL_DRIVER -ICore/Inc \
 -IDrivers/STM32F4xx_HAL_Driver/Inc -IDrivers/CMSIS/Include -IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
 tests/uart_dma/baud_assert.c -o "$work/baud.o"

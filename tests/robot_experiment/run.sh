#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for hz in 1 5 10; do
 period=$((1000 / hz))
 cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -g \
 -DMUL_UNIVERSAL=1 -DMUL_ROBOT_EXPERIMENT=1 -DMUL_UWB_ONLY=1 -DAPP_EPUCK_STREAM_PERIOD_MS=${period}U -DAPP_WIFI_SENSOR_PERIOD_MS=${period}U \
 -Itests/esp_link/stubs -ICore/Inc -IThirdParty/mul_core tests/robot_experiment/test.c -o "$work/test"
 for case_name in motor failure rate coexistence; do "$work/test" "$case_name"; done
done

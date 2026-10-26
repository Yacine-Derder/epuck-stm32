#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d);trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-const-variable \
 -fsanitize=undefined -fno-pie -no-pie -ffunction-sections -fdata-sections \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/frequency/test_driver.c -Wl,--gc-sections -lm -o "$work/driver"
"$work/driver"
for enabled in 0 1;do
 cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_UNIVERSAL=1 -DMUL_UWB_ONLY=1 -DMUL_TWR_ROLE=2 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_HYBRID_FREQ_DIAG=$enabled \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/frequency/test_adapter.c Core/Src/uwb_config.c \
 Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_multi_protocol.c ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c \
 ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c \
 ThirdParty/mul_core/mul_localization.c ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c \
 ThirdParty/mul_core/mul_time.c ThirdParty/mul_core/mul_ads.c ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c \
 -lm -o "$work/adapter"
 "$work/adapter"
done

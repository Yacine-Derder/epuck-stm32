#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -g -fsanitize=address,undefined -fno-pie -no-pie \
 -ICore/Inc -IThirdParty/mul_core tests/runtime_config/test_config.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c -lm -o "$work/config"
"$work/config"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_runtime.c Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_multi_protocol.c ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/runtime"
"$work/runtime"

cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DAPP_UWB_ENABLED=0 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core -IThirdParty/qorvo_dw3/driver \
 tests/mul_diag/test_link.c -o "$work/link"
"$work/link"
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -Itests/runtime_config/stubs -ICore/Inc -IThirdParty/mul_core \
 -Wl,--defsym=__uwb_config_start__=0x080C0000 -Wl,--defsym=__uwb_config_end__=0x08100000 \
 tests/runtime_config/test_flash.c -o "$work/flash"
"$work/flash"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_UWB_ONLY=1 -Itests/epuck_console/stubs -ICore/Inc -IThirdParty/mul_core \
 tests/runtime_config/test_console.c -o "$work/console"
"$work/console"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_UWB_ONLY=1 -Itests/esp_link/stubs -ICore/Inc -IThirdParty/mul_core \
 tests/runtime_config/test_wifi.c ThirdParty/mul_core/mul_telemetry.c -o "$work/wifi"
"$work/wifi"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_multi.c Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_multi_protocol.c ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/multi"
"$work/multi"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_beacon.c Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_multi_protocol.c ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/beacon"
"$work/beacon"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_hybrid.c Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_multi_protocol.c ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/hybrid"
"$work/hybrid"

cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_help.c Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_multi_protocol.c ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/help"
"$work/help"

for slot in 500 50 10;do
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_FOLLOWER_SLOT_MS=$slot -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_network.c Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_multi_protocol.c ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/network"
"$work/network"
done

for slot in 500 50 10;do
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_FOLLOWER_SLOT_MS=$slot -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_commands.c Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_multi_protocol.c ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/commands"
"$work/commands"
done
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -g -fsanitize=address,undefined -fno-pie -no-pie \
 -ICore/Inc -IThirdParty/mul_core tests/runtime_config/test_schedule_config.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ranging_plan.c \
 ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/schedule_config"
"$work/schedule_config"

cc -std=c11 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_schedule_runtime.c Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_multi_protocol.c ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/schedule_runtime"
"$work/schedule_runtime"

cc -std=c11 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_multi_hybrid.c Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_multi_protocol.c ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/multi_hybrid"
"$work/multi_hybrid"

cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -g -fsanitize=address,undefined -fno-pie -no-pie \
 -ICore/Inc -IThirdParty/mul_core tests/runtime_config/test_plan_config.c Core/Src/uwb_config.c \
 ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c \
 ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/plan_config"
"$work/plan_config"

cc -std=c11 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_plan_runtime.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ranging_plan.c \
 Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_multi_protocol.c \
 ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c \
 ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c \
 ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/plan_runtime"
"$work/plan_runtime"

cc -std=c11 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_plan_radio.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ranging_plan.c \
 Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_multi_protocol.c \
 ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c \
 ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c \
 ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/plan_radio"
"$work/plan_radio"

cc -std=c11 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_plan_responder.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ranging_plan.c \
 Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_multi_protocol.c \
 ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c \
 ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c \
 ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/plan_responder"
"$work/plan_responder"

if [ -n "${MUL_POSE_FRAMES:-}" ]; then
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_pose_radio.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ranging_plan.c \
 Core/Src/mul_plan_radio.c ThirdParty/mul_core/mul_multi_receiver.c ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_multi_protocol.c \
 ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c \
 ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c \
 ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c ThirdParty/mul_core/mul_time.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c -lm -o "$work/pose_radio"
"$work/pose_radio"

fi

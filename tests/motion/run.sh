#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
python3 tools/build_replay_host.py --fixture --motion-guard --sanitize --output "$work/motion"
"$work/motion"
MUL_REPLAY_TDMA=1 MUL_REPLAY_KALMAN=1 "$work/motion"
MUL_REPLAY_FF=1 MUL_REPLAY_KALMAN=1 "$work/motion"
python3 tools/build_replay_host.py --transport --motion-guard --sanitize --output "$work/transport"
"$work/transport"
# Actual five-message AR responder keeps its independent-domain timestamp path.
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function -g \
 -fsanitize=address,undefined -fno-pie -no-pie -DMUL_RX_MOTION_GUARD=1 \
 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_plan_responder.c Core/Src/uwb_config.c Core/Src/mul_plan_radio.c \
 ThirdParty/mul_core/mul_ranging_plan.c ThirdParty/mul_core/mul_multi_receiver.c \
 ThirdParty/mul_core/mul_plan_protocol.c ThirdParty/mul_core/mul_multi_protocol.c \
 ThirdParty/mul_core/mul_hybrid.c ThirdParty/mul_core/mul_legacy_twr.c \
 ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c \
 ThirdParty/mul_core/mul_relation.c ThirdParty/mul_core/mul_observation.c \
 ThirdParty/mul_core/mul_downlink.c ThirdParty/mul_core/mul_localization.c \
 ThirdParty/mul_core/mul_telemetry.c ThirdParty/mul_core/mul_beacon.c \
 ThirdParty/mul_core/mul_time.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c \
 ThirdParty/mul_core/mul_schedule.c -lm -o "$work/responder"
"$work/responder"

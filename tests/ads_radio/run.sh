#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
python3 ThirdParty/mul_core/verify.py
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
flags='-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie'
for trace in 0 1; do
for role in 1 2; do
 cc $flags -DMUL_ADS_TRACE_TIMES=$trace -DMUL_TWR_ROLE=$role -DMUL_TWR_SESSION=123ULL -DMUL_UWB_ONLY=1 \
  -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
  -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
  tests/ads_radio/test.c ThirdParty/mul_core/mul_ads.c ThirdParty/mul_core/mul_core.c \
  ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c -lm -o "$work/radio"
 "$work/radio"
done
done

for app in 0 1; do
 for role in 1 2; do
  cc $flags -DAPP_UWB_ENABLED=$app -DMUL_TWR_ROLE=$role -DMUL_TWR_SESSION=123ULL \
   -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
   -IThirdParty/qorvo_dw3/driver tests/mul_diag/test_link.c -o "$work/link"
  "$work/link"
 done
done
if cc -std=c11 -fsyntax-only -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=0 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 Core/Src/mul_ads_radio.c 2>"$work/unsafe.log"; then
 echo 'Unsafe ADS profile unexpectedly accepted' >&2; exit 1
fi
# Require the intentional safety guard, not an unrelated compiler error.
rg -q 'ADS requires explicit UWB-only safety policy' "$work/unsafe.log"

cc $flags -DMUL_TIMING_TRACE=1 -DMUL_TWR_ROLE=2 -Itests/mul_diag/timing_stubs -ICore/Inc \
 -IThirdParty/qorvo_dw3/driver tests/mul_diag/test_timing.c -o "$work/timing"
"$work/timing"

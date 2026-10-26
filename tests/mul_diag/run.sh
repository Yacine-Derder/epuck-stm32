#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
python3 ThirdParty/mul_core/verify.py
sh ThirdParty/mul_core/tests/run.sh
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
flags='-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie'
for role in 1 2; do
 cc $flags -DMUL_DIAG_ROLE=$role -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
  -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
  tests/mul_diag/test_radio.c Core/Src/uwb_diag_profile.c \
  ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c ThirdParty/mul_core/mul_schedule.c ThirdParty/mul_core/mul_clock_fit.c ThirdParty/mul_core/mul_clock.c -o "$work/radio"
 "$work/radio"
done
for app in 0 1; do
 for role in 0 1 2; do
  cc $flags -DAPP_UWB_ENABLED=$app -DMUL_DIAG_ROLE=$role -Itests/phy_interop/stubs -ICore/Inc \
   -IThirdParty/mul_core -IThirdParty/qorvo_dw3/driver tests/mul_diag/test_link.c -o "$work/link"
  "$work/link"
 done
done
printf '%s\n' 'MUL diagnostic TX/RX and all six activation/failure/IRQ tests passed'

cc $flags -DMUL_TIMING_TRACE=1 -Itests/mul_diag/timing_stubs -ICore/Inc \
 -IThirdParty/qorvo_dw3/driver tests/mul_diag/test_timing.c -o "$work/timing"
"$work/timing"
printf '%s\n' 'Opt-in cycle timing nested IRQ/reset/wrap tests passed'

#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
flags='-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie'
cc $flags -ICore/Inc -IThirdParty/mul_core tests/phy_interop/test_codec.c -o "$work/codec"
"$work/codec"
for role in 1 2; do
 cc $flags -DPHY_INTEROP_ROLE=$role -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
  -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
  tests/phy_interop/test_radio.c Core/Src/uwb_diag_profile.c -o "$work/radio"
 "$work/radio"
done
printf '%s\n' 'PHY codec, TX and RX mock-driver sanitizer tests passed'
for app in 0 1; do
 for role in 0 1 2; do
  cc $flags -DAPP_UWB_ENABLED=$app -DPHY_INTEROP_ROLE=$role -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
   -IThirdParty/qorvo_dw3/driver tests/phy_interop/test_link.c -o "$work/link"
  "$work/link"
 done
done
printf '%s\n' 'UWB activation/init-failure/IRQ ownership tests passed for all six configurations'

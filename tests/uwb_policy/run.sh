#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined -fno-pie -no-pie -g \
 -DMUL_UWB_ONLY=1 -DAPP_DEBUG_TELEMETRY=1 -Itests/epuck_console/stubs -ICore/Inc tests/uwb_policy/test.c -o "$work/test"
"$work/test"

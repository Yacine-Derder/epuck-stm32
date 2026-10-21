#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -g -fsanitize=address,undefined -fno-pie -no-pie \
 -ICore/Inc tests/runtime_config/test_config.c Core/Src/uwb_config.c -lm -o "$work/config"
"$work/config"

#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d);trap 'rm -rf "$work"' EXIT HUP INT TERM
for universal in 0 1;do
 cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie -DMUL_UNIVERSAL=$universal \
 -Itests/spi_polling/stubs -ICore/Inc -IThirdParty/qorvo_dw3/driver tests/spi_polling/test.c -o "$work/spi"
 "$work/spi"
done

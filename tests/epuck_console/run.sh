#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I"$repo_dir/tests/epuck_console/stubs" -I"$repo_dir/Core/Inc" \
  "$repo_dir/tests/epuck_console/test.c" -o "$test_dir/test"
# Uses the actual app_config.h; when disabled the same test checks no UART.
printf '%s\n' 'validation / disabled feature'
if "$test_dir/test" validation; then
  for case_name in stop heartbeat lost disconnect retry; do
    printf '%s\n' "$case_name"
    "$test_dir/test" "$case_name"
  done
else
  exit 1
fi

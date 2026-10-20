#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -g \
 -I"$repo_dir/tests/esp_link/stubs" -I"$repo_dir/Core/Inc" "$repo_dir/tests/esp_link/test.c" -o "$test_dir/test"
for case_name in ownership watchdog faults telemetry sensors initfail diagnostics reasons streaming; do
 printf '%s\n' "$case_name"
 "$test_dir/test" "$case_name"
done

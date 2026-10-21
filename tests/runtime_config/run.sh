#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -g -fsanitize=address,undefined -fno-pie -no-pie \
 -ICore/Inc tests/runtime_config/test_config.c Core/Src/uwb_config.c -lm -o "$work/config"
"$work/config"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DMUL_UWB_ONLY=1 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core \
 -IThirdParty/qorvo_dw3/driver -IThirdParty/qorvo_dw3/driver/dw3000 \
 tests/runtime_config/test_runtime.c ThirdParty/mul_core/mul_telemetry.c Core/Src/uwb_config.c ThirdParty/mul_core/mul_ads.c \
 ThirdParty/mul_core/mul_core.c ThirdParty/mul_core/mul_common.c -lm -o "$work/runtime"
"$work/runtime"

cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_UNIVERSAL=1 -DMUL_TWR_ROLE=2 -DAPP_UWB_ENABLED=0 \
 -Itests/phy_interop/stubs -ICore/Inc -IThirdParty/mul_core -IThirdParty/qorvo_dw3/driver \
 tests/mul_diag/test_link.c -o "$work/link"
"$work/link"
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_UNIVERSAL=1 -Itests/runtime_config/stubs -ICore/Inc -IThirdParty/mul_core \
 -Wl,--defsym=__uwb_config_start__=0x080C0000 -Wl,--defsym=__uwb_config_end__=0x08100000 \
 tests/runtime_config/test_flash.c -o "$work/flash"
"$work/flash"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_UNIVERSAL=1 -DMUL_UWB_ONLY=1 -Itests/epuck_console/stubs -ICore/Inc -IThirdParty/mul_core \
 tests/runtime_config/test_console.c -o "$work/console"
"$work/console"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation -g -fsanitize=address,undefined -fno-pie -no-pie \
 -DMUL_UNIVERSAL=1 -DMUL_UWB_ONLY=1 -Itests/esp_link/stubs -ICore/Inc -IThirdParty/mul_core \
 tests/runtime_config/test_wifi.c ThirdParty/mul_core/mul_telemetry.c -o "$work/wifi"
"$work/wifi"

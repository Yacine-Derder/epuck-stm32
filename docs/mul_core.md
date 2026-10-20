# Stage 3G STM32 integration

Software and host/build validation are complete; hardware acceptance is pending.
Do not begin Stage 3H, mixed-radio TWR/TDoA or localization from this checkpoint.

Canonical source is LPS `portable/mul_core`. This repository's
[checked-in snapshot/API and wire specification](../ThirdParty/mul_core/README.md)
is pinned by [UPSTREAM.json](../ThirdParty/mul_core/UPSTREAM.json) to published
LPS `1b807c5c7507d67d802ee976ae4dfe5b27fd88e9`. The corresponding STM32 code
checkpoint is `1bd3ecdba534549b4c9ac89f17fd8bd7270fe056`. Later documentation
commits keep this same dependency; record both final HEADs with hardware logs.

No third remote, submodule or sibling checkout is needed for a standalone build.
Python 3 (standard library only) is required for the offline build-time checks.
Only edit the canonical LPS core; use its `tools/mul_core_vendor.py` exporter for
explicit updates. CMake configure and every build verify the snapshot offline,
including added-file drift. Nothing fetches or updates it implicitly. Core tests
are runnable in a fresh checkout without LPS. Public APIs are fixed-buffer/status
`mulCoreEncodeDiag`, `mulCoreDecodeDiag`, `mulCoreSourceValid`, sequence reset /
observe and `mulCoreObserve`, plus the unchanged legacy `mul_common` functions.
The ABI is source-level; structs/padding are never serialized directly.

`Core/Src/mul_diag.c` is the new opt-in adapter. Existing `uwb_link.c` selects it
only for MUL_DIAG_ROLE1/TX or2/RX, overrides APP_UWB_ENABLED0 without editing NVM
or app_config, and leaves EXTI0 disabled for bounded main-loop polling. Default0
retains the existing normal radio path. Stage3F and3G roles cannot be enabled
together. `uwb_diag_profile.c` factors the proven Stage3F channel5 configuration
into one setup shared with Stage3G; no PHY settings or Stage3F packets change.

The diagnostic is a separate44-byte MUL type0x70/version1 plus hardware FCS.
It validates extended source identity, MAC, type/version, sequence/length and
metadata. It carries a previous **completed** TX marker and its sequence; the
first marker is unavailable. Actual40-bit TX markers come from
`dwt_readtxtimestamp`, RX from `dwt_readrxtimestamp(...,DWT_COMPAT_NONE)`, captured
before rearming. No application power correction, cross-clock subtraction or
calibration claim is added. Width40 and nominal DTU units are explicit; full
identity identifies the clock domain. DONE logs are matched to next-packet META
by `prev_seq`, never by comparing independent receiver/source timestamps.

## UART coexistence and resource limits

Main-loop order, USART1/USART3 communication, sensor and ESP paths, motor state,
STOP/ARM/ownership, heartbeats and watchdog code remain unchanged. No blocking
delay or protocol wait loop is added. The adapter polls at most once per host
millisecond, handles one event, rearms before formatting output and uses the
existing16x192-byte USB output queue. Its existing overflow counter remains
visible; no additional event queue or heap is introduced. Error/overrun status
wins over simultaneous CRC-good; failed RX enable retries at most every50ms.

`MUL_TIMING_TRACE=ON` (default OFF, diagnostic only) enables DWT cycle maxima
for outer Qorvo SPI mutex IRQ-off windows, service runtime and service-start gap.
`MULG TIME` logs the actual cycle frequency; divide cycles by hz. Maxima exclude
startup/calibration and include instrumentation effects. They are not WCET,
all-system IRQ latency or measured stack high-water. The cycle counter wraps
about25.6s at168MHz; intervals must be shorter than a wrap. Nested IRQ/reset/wrap
logic is host-tested. No physical cycle trace has yet been collected.

SPI still globally masks interrupts through blocking HAL transfers at1.3125MHz;
a44-byte body alone takes~268us against~86.8us per UART character at115200-8N1.
That remains a plausible UART overrun risk, not proof of Stage3F's six errors.
The code does not claim the UART fault is fixed or change speed/masking/priorities.
Instrumented hardware RX plus a matched disabled baseline is required.

Measured Release text/data/bss (bytes): workspace default52600/712/27856;
Stage3F RX78852/736/28256; new TX79136/736/28288;
RX79260/736/28288; RX+trace79700/736/28312. Flash section contents=text+data; linker spans also include alignment gaps:
workspace default53,372 bytes; new TX79,928, RX80,052 and RX+trace80,492.
Static RAM=data+bss, including linker reservations. New RX RAM29024 (29048 with trace), compared with
Stage3F28992. Clean committed export: default50000/712/27504;
new TX76536/736/27936; RX+trace77100/736/27960. The352-byte RAM difference is
preserved user streaming/control work. Disabled builds link no diagnostic state.
Adapter state<=128 bytes, frame storage46; no heap. Compiler process frames
272 TX/280 RX/288 traced RX and216 timing-report, plus existing HAL/libc/SPI/IRQ
callers. The1024-byte linker stack minimum is not measured high-water.
No warnings originate in new core/adapters; existing vendor unused functions /
possibly-uninitialized chan_ctrl and motor-format warnings remain.

## Validation and hardware handoff

```sh
sh ThirdParty/mul_core/tests/run.sh
sh tests/mul_diag/run.sh
python3 tests/mul_diag/run_coexist.py
sh tests/phy_interop/run.sh
sh tests/epuck_console/run.sh
sh tests/esp_link/run.sh
```

These pass ASan/UBSan: core/golden/malformed/20000 fuzz inputs; diagnostic
TX/RX and six activation/failure/IRQ combinations; timing nesting/reset/wrap;
15 console and9 ESP scenarios. Nine interleaved MUL RX + ESP/sensor/safety
scenarios also exercise recoverable PHY errors and bounded USB overflow.
A clean committed-tree export independently passes the same available suites
(6 console/8 ESP/8 coexistence scenarios), and builds default, Stage3F RX,
Stage3G TX/RX and retained normal UWB-enabled/channel9. Workspace builds also
cover Stage3F TX/RX and traced/untraced Stage3G. LPS 195-test default/continuous regression,
continuous178 sanitizer, old/new DW role and cross-repository vector checks pass.
Mocks prove cooperative state and bounds, not physical UART deadline safety.

The canonical [hardware and resource guide](https://github.com/Yacine-Derder/lps-node-firmware/blob/0978fd9/docs/mul_core.md)
contains exact build/flash/capture/restore commands, expected logs and A/B/C
acceptance criteria. Only the user executes flashing. A/B require >=100 completed
TX each in intact overlapping windows, valid strict decode and exact previous-TX
marker matches. C compares RX + sensor/ESP load against UWB disabled and checks
STOP/ARM/heartbeat at zero speed with chassis secured/wheels lifted. Do not send
nonzero motor speeds. UART/drop/parser/age/timing counters must be captured.

Important reproducibility limit: ten pre-existing user files remain uncommitted
and unchanged (including100ms streaming). Fresh committed firmware retains1Hz R
polling; STREAM/STREAM_STATUS apply to the preserved streaming tree. Record the
local patch fingerprint when performing10Hz trials; do not claim the published
SHA alone reproduces that image. Both variants build/test successfully.

No hardware was flashed; central project, ESP32 and dsPIC files were untouched.
Next action is the user's full A/B/C/baseline logs and build/pin/geometry metadata.
Physical timestamp semantics, ranging accuracy and UART causation remain open.

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

## UWB-first operating policy

`MUL_UWB_ONLY=ON` disables ESP, forces the e-puck safety link on, sensor period0
and quiet debug telemetry. Startup STOP is sent before radio initialization;
STOP ACK/retry and watchdogs stay active. ARM, movement and stream changes are
rejected from all sources. STOP, HELP, MOTOR_STATUS, STREAM_STATUS and
LINK_STATUS remain available. After fresh PONG the adapter requests STREAM0;
unacknowledged idempotent STREAM0 can retry after5s with a fresh fence.
Verify actual STOP acknowledgement/disarm on hardware. Normal operating mode
remains available with the flag OFF. No dsPIC changes are required.

Normal mode defaults to1000ms acquisition. ESP telemetry is the latest new
sample at most1Hz, compact DATA2 with16 numeric fields and no lights. Safety
CONTROL_HB/motor HB and watchdogs are not throttled. Raw dsPIC DATA parsing
is unchanged. Updated ESP firmware/client accept both legacy DATA and DATA2;
an old ESP bridge drops DATA2, so update it for normal Wi-Fi operation.
See [wifi_protocol.md](wifi_protocol.md) and [sensor_streaming.md](sensor_streaming.md).

Default APP_DEBUG_TELEMETRY=0 suppresses routine USB sensor arrays and HB ACKs;
important fault changes remain. LINK_STATUS gives concise counter snapshots.
Verbose diagnostics can be enabled in normal mode. MUL RX logs now carry
previous-TX metadata inline, preserving every timestamp field with fewer
messages. The44-byte UWB packet and portable-core pin remain unchanged.

## Timing evidence and limitations

Main-loop order is USB, UWB, ESP, e-puck. The diagnostic handles at most one
status event per host millisecond, rearms before logging, and uses bounded USB
queues. This does not establish a physical1ms scheduling bound. SPI still uses
global PRIMASK around blocking HAL transfers at1.3125MHz; no speed, masking,
priority or driver rewrite was made to optimize delivery.

User A/B logs demonstrate shared-core exchange in both directions. Comparable
previous-TX markers match43/43 and62/62; TX completion endpoints89/93 are short
of the >=100 gate. C reaches at least542 valid RX,6 gaps, and an ESP USART3
hardware overrun (HAL ORE0x8). RX continues without a new recorded gap around
that visible fault. A confirmed UWB-disabled baseline has separate1000ms and
100ms sessions with zero UART/drop/parser errors. Unequal windows and missing
history do not prove SPI causation or a synchronized packet delivery ratio.
The full accounting and log provenance are in the canonical guide below.

`MUL_TIMING_TRACE=ON` measures outer SPI IRQ-off windows, service runtime and
service-start gaps after radio startup. User C maxima at168MHz are49980cycles
(297.5us),107650 (640.77us),214368 (1276us), respectively. A UART character is
86.8us at115200-8N1: masking is a plausible overrun mechanism, not a proven
cause. Other IRQ-off sites are unmeasured. DWT32 wraps~25.6s; intervals must be
shorter. Maxima include instrumentation and are not WCET/all-system IRQ latency.

Current Release text/data/bss: UwbOnlyRX+trace76124/732/16288;
UwbOnlyTX75604/732/16264; UwbOffSafe49088/708/15828;
CoexistRX+trace79292/736/27056; NormalBaseline52156/712/26600.
RAM=data+bss including linker reservations, not physical stack high-water.
The diagnostic adds no heap. Retained vendor warnings are not timing proofs.

## Verification and next hardware action

```sh
sh ThirdParty/mul_core/tests/run.sh
sh tests/mul_diag/run.sh
python3 tests/mul_diag/run_coexist.py
sh tests/phy_interop/run.sh
sh tests/epuck_console/run.sh
sh tests/esp_link/run.sh
sh tests/uwb_policy/run.sh
```

Core/golden/20000 fuzz, TX/RX, activation/failure/IRQ and timing/reset/wrap
suites pass ASan/UBSan, alongside15 console,9 ESP and9 interleaved coexistence
scenarios. New policy tests exercise startup STOP, lost ACK retry, disarm,
command rejection, quiet output and fault reporting. Telemetry tests cover
1Hz coalescing, tick wrap and light omission. All five policy profiles build.
Mocks do not prove physical safety deadlines. Reviewed previously local
streaming work is now included in the published source.

The canonical [hardware/results guide](https://github.com/Yacine-Derder/lps-node-firmware/blob/master/docs/mul_core.md#next-user-experiments--uwb-first)
contains exact five-profile CMake commands, full STM32CubeProgrammer ELF paths,
user-only SWD/DFU instructions, capture requirements and restoration commands.
Next: sustained UwbOnlyRX then UwbOnlyTX (>=10min,>=500 completed TX per
direction), followed by UwbOffSafe baseline. Optional NormalBaseline/CoexistRX
comparison must use matching geometry/power/duration and1000ms stream.
Keep motors disarmed, require actual STOP ACK, preserve all logs and explicit
LINK_STATUS endpoints. No SPI-related cause or fix is claimed.

Stage3G remains hardware-pending; do not begin Stage3H. No hardware was flashed,
no dsPIC or central project document was changed. Normal Wi-Fi is secondary;
UWB-only reliability and UART impact must first be demonstrated physically.

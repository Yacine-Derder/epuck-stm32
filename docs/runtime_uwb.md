# Universal STM32/DWM3000 configuration

One UWB-only image supports both ADS-TWR roles and any distinct local/peer byte
addresses. Each accepted SET validates the entire configuration, safely restarts
UWB, saves and verifies it, then confirms `CFG SAVED / ACTIVE`. No normal APPLY,
SAVE, radio-off command or maintenance mode is needed. A rejected command leaves
settings unchanged. Wait for the confirmation before sending another change.

Robot identity is metadata, independent of radio role. Motor-enabled normal
operation remains a separate legacy build: the universal image cannot ARM or
move motors, keeps startup STOP/retries/watchdogs/emergency stops and disables
sensor streaming. ESP32 transport is optional (`wifi off` by default); UWB needs
no ESP32, robot controller or host. Multi-peer ADS and experimental beacon/listen
acquisition are described in [the network guide](uwb_network.md). A position
estimator and distributed collision-free multi-initiator scheduler are not implemented.

## Build

From epuck-stm32:

```sh
cmake -S . -B build/Universal -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Release -DMUL_UNIVERSAL=ON -DMUL_UWB_ONLY=ON \
  -DMUL_TWR_ROLE=0 -DMUL_DIAG_ROLE=0 -DPHY_INTEROP_ROLE=0 \
  -DMUL_TIMING_TRACE=ON -DMUL_ADS_TRACE_TIMES=ON
cmake --build build/Universal
```

STM32CubeProgrammer ELF:
`/home/derder/Documents/epuck-stm32/build/Universal/EPuck_STM32.elf`.
No build command flashes hardware. CMake internally uses MUL_TWR_ROLE=2 only as
the pinned core's inclusion/timing guard; actual initiator/responder selection is
runtime. Leave legacy role selectors zero for this profile.

## Console

USB CDC accepts one newline-terminated command per line, up to 63 printable
characters (CRLF accepted). Command/field names and enum values are case-insensitive.
Startup and USB connection display a short HELP reminder. `HELP` and `CFG HELP`
list all settings, defaults, units, persistence/restart behavior and examples.
HELP and SHOW send bounded incremental output that waits for console capacity;
a new HELP/SHOW replaces an unfinished display. Configuration reserves reply
capacity before changing anything; a busy console returns a retry error.

`CFG SHOW` displays readable current settings, schema version, generation,
SAVED/ACTIVE or error state, and whether a valid saved record was loaded at boot.
`CFG SHOW ACTIVE`, `CFG SHOW SAVED`, `CFG SHOW DEFAULTS` inspect the corresponding
snapshots. Normal commands have no pending-application state. SAVED-but-radio-OFF
is an explicit hardware activation failure, not a successful update confirmation.

| Field | Values / units | Factory default |
|---|---|---|
| address (node alias) | Hex 0x00..0xFF or decimal 0..255 | 0x00 |
| peer | Same range; must differ from local address | 0x01 |
| identity | standalone, alr, rlr | standalone |
| mode | off, twr, beacon, listen, hybrid, hybrid_static | off |
| role | initiator, responder (ADS role) | responder |
| reference / follower | Distinct legacy TDMA IDs0..254; enable both as peers for hybrid | 0x84 / 0x85 |
| interval_ms | 250..60000 ms | 1000 |
| phy | mixed: channel5, 6.8Mbps, code9, preamble128 | mixed |
| tx_ant_dtu / rx_ant_dtu | 0..65535 DW timestamp units | 16380 each |
| calibration | on, off | off |
| correction_mm | Signed -10000..10000 mm | 0 |
| verbosity | quiet, summary, full | full |
| telemetry | on, off | off |
| telemetry_ms | 1000..60000 ms | 1000 |
| samples | on, off; optional range/event samples | off |
| wifi | on, off; existing ESP32 UART3 bridge | off |
| telemetry_format | compact (wire v2; reads v1 too) | compact |

Every field persists automatically and restarts the radio when changed, using a
fresh hardware-RNG session and reset per-session counters. Identical values do
not write flash or restart a healthy, already saved active radio. Numeric enums
remain compatibility aliases, but SHOW uses words. No byte address is reserved:
0xFF is an ID in `ID:00:00:00:00:00:CF:BC`, not an all-FF broadcast MAC. Provision
unique IDs; local validation cannot discover network duplicates. Legacy diagnostic
address policies are unchanged. The legacy DW1000 ADS adapter still uses its
existing fixed 0x84/0x86 pairing; widening the codec does not change that roster.

Two-board example, one command at a time after the 2 s startup grace and safety
confirmation. On previously configured boards, start with `CFG DEFAULTS` if needed.

Board A:

```text
CFG SET address 0x10
CFG SET peer 0x11
CFG SET identity alr
CFG SET role initiator
CFG SET calibration off
CFG SET mode twr
CFG SHOW
```

Board B:

```text
CFG SET address 0x11
CFG SET peer 0x10
CFG SET identity rlr
CFG SET role responder
CFG SET calibration off
CFG SET mode twr
CFG SHOW
```

Use `CFG SET role responder` / `CFG SET role initiator` to switch roles without
rebuilding; change the other board correspondingly. Full six-timestamp ADS
messages and raw nominal range remain compatible. When provisioning other pairs,
change peer first if the desired local address equals its current peer.

`CFG DEFAULTS` activates and saves factory settings, including radio OFF and
calibration OFF. `CFG LOAD` activates the last valid saved record; `REVERT` is an
alias. A failed LOAD never erases records or changes live settings. `CFG SAVE` and
`CFG APPLY` remain compatibility aliases for the same automatic transaction;
they are unnecessary in normal use. Power-cycle and request SHOW: the settings
must match and `saved settings loaded at boot: YES` should appear.

## Why the old SAVE sequence failed

Bitcraze `src/main.c` writes settings immediately through cfgWriteU8 (EEPROM),
reads them back for status, and prints **restart for it to take effect**. It does
not immediately reinitialize the radio. Its `i` menu accepts decimal 0..255;
`m`, `r`, `d`, `h` control mode/radio/defaults/help. STM32 keeps its existing
line-based console, but follows the immediate-persistence convention and adds
immediate radio activation as requested.

Previously SET/DEFAULTS changed only RAM; APPLY activated it; SAVE alone persisted.
After the reported successful mode-off APPLY, started was false. The subsequent
mode-on SET changed desired mode only, so it did not restart the radio. Thus the
remaining SAVE blocker was the independent epuck_link_flash_safe predicate:
startup tick >=2000, then either no UART1 bytes at all, or confirmed disarmed STOP,
no motor command waiting, and confirmed STREAM 0. The supplied output does not
identify which of those safety conditions failed; no flash write was attempted.
The old instructions made an awkward RAM/active workaround look sufficient without
explaining how an unconfirmed robot state could still block it.

Tests reproduce the complete supplied command sequence, plus the stopped-radio /
received-byte / missing-STOP-ACK failure and the missing-STREAM0-ACK condition.
Errors now distinguish grace, armed/pending movement, STOP unconfirmed and STREAM0
unconfirmed, and name the commands/wiring checks needed. The predicate remains
mandatory. An armed/pending-movement state is rejected even on a silent UART.
Normal SET handles the radio stop itself; it never treats desired mode as proof
that hardware is quiescent.

USART1 RX PA10 was also configured with GPIO_NOPULL, so an absent controller left
an unbiased input. A weak idle-high pull-up is now applied only in UWB-only builds.
This hardens standalone operation; it is not proof that floating-pin noise caused
the observed failure. Any actual received bytes still require acknowledged STOP
and STREAM0. Check MOTOR_STATUS, STREAM_STATUS and LINK_STATUS on rejection;
connect/power the robot controller and wait for acknowledgments before retrying.
The original silent-peer grace policy is preserved; software cannot establish
physical motor status on disconnected hardware. Isolate wheels for provisioning.

## Transaction, flash recovery and endurance

The STM32F405RGT6 application remains at 0x08000000 with a 768 KiB linker limit.
Sectors 10/11 (128 KiB each, 0x080C0000..0x080FFFFF) are exclusively reserved;
the backend checks the actual 1 MiB device geometry and linker symbols. See ST
[RM0090](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-and-stm32f407-advanced-armbased-32bit-microcontrollers-stmicroelectronics.pdf)
and [DS8626 flash characteristics](https://www.st.com/resource/en/datasheet/stm32f405rg.pdf).
Single-bank program/erase stalls instruction fetch; this is a deliberate bounded
configuration interruption, never a continuous-ranging operation.

1. Validate the complete proposal, console capacity and robot safety, before any change.
2. Cancel an active ADS exchange and force TX/RX off (polling owns the radio; no ISR worker).
3. Preflight the candidate radio settings, including entropy and antenna readback,
   then stop RX again. This catches initialization failures before a durable change.
4. Append and verify the CRC-protected record with radio stopped; commit word last.
5. Resume the prepared radio and confirm SAVED / ACTIVE only after both succeed.

Flash/preflight failure restores the old configuration/radio where hardware allows.
A complete committed record is resolved by CRC readback even if the backend reported
an error after writing it. Unreadable storage is reported as durability UNCONFIRMED;
no false durability acknowledgment is sent. A post-commit hardware failure reports
SAVED but radio OFF; repair hardware and retry LOAD, without a redundant save.
Radio recovery itself can fail; such errors explicitly leave service stopped.

The 128-byte little-endian journal retains CRC32/IEEE, modular generation and
commit-last semantics. Schema v2 introduced Wi-Fi and eight peers. Schema v3
packs reference/follower ID bytes into the previously-zero upper bytes of the
node word (offset16); its top byte must remain zero. Peer fields/offsets, CRC and
commit position are unchanged. Existing v1/v2 records load without a migration
write, with calibration/antenna/peer settings preserved and roster84/85. V1 retains
its single enabled primary peer and Wi-Fi off. New edits use v3. Factory defaults
apply only to blank/corrupt storage or explicit DEFAULTS. Old firmware cannot read
newer records: a downgrade may load older settings. Inspect configuration before
enabling RF after any downgrade; do not mix old/custom-roster firmware blindly.
Skip incomplete/invalid/incompatible records, use an older valid record when present,
and otherwise boot radio-OFF defaults without erasing or writing recovery data.

Each sector holds 1024 records. Normal changes append, **not erase per command**;
only a full-sector rotation erases the other sector, retaining the latest committed
sector. Equal settings write nothing. Power loss recovers old/new complete records.
The specified erase endurance is 10 kcycles, not infinite; configuration is for
operator settings, not high-rate parameter streaming. No auto-save loop runs at boot.
Mass erase or old unrestricted images can destroy settings: back up SHOW and use
programmer sector erase settings that preserve sectors 10/11. Use stable supply
within the specified programming voltage range.

## Calibration and telemetry

`calibrated_m = raw_m + correction_mm/1000` only when calibration is ON. Positive
correction increases range. Hardware TX/RX antenna delays enter timestamps once;
software offset operates only on derived range. Changing an antenna delay disables
software calibration; refit before explicitly enabling it. Raw negative/nonfinite
or otherwise invalid results stay invalid even if an offset makes them positive;
negative corrected ranges are also invalid. Neither value is clamped. Raw RESULT
and six TIMES remain; CAL exposes raw/calibrated values and validity.

```text
CFG SET correction_mm -100
CFG SET calibration on
CFG SET calibration off
CFG SET telemetry on
CFG SET telemetry_ms 1000
TLM STATUS
TLM NEXT
CFG SET telemetry off
```

The example correction is syntax, not a recommendation. No -469 mm value is built
into firmware. Existing offline calibration proposals are pair-specific; no scale
fit is supported. The bounded four-record queue retains raw/calibrated samples
and low-rate counters, IDs and sessions. STATUS exposes occupancy/high-water/drops;
NEXT respects USB backpressure. Optional UART3/Wi-Fi forwarding uses separate
bounded queues, described below and in the network guide. Compare
MULH TIME maxima and UART errors with telemetry OFF/ON and a stalled reader;
low rate is not evidence of safe timing. Flash/configuration pauses are maintenance
interruptions and must be marked in captures.

## Verification and first hardware test

Run `sh tests/runtime_config/run.sh` for ASan/UBSan configuration, real runtime /
radio-adapter same-binary role tests, flash mocks and USB parser/backpressure tests.
It covers all 65536 address combinations, strict decimal/hex/enum parsing, old v1
records, automatic activation/save, identical no-write updates, exact old SAVE
sequence, per-word failed-write rollback, post-commit failure reporting, reboot,
HELP coverage, corrupted records and factory restoration. Journal tests cover
power-cut/rollover recovery, generation wrap, CRC corruption, and 1024th append /
1025th rotation erase counts. Safety tests check real STOP/STREAM0 predicates.

Regression commands:

```sh
sh ThirdParty/mul_core/tests/run.sh
for suite in runtime_config ads_radio mul_diag phy_interop epuck_console esp_link uwb_policy; do
  sh tests/$suite/run.sh || exit 1
done
python3 tests/mul_diag/run_coexist.py
python3 tests/runtime_config/check_elf.py build/Universal/EPuck_STM32.elf
```

Before claiming hardware validation, the user should:

1. Back up settings/ELFs; program the same Universal ELF on both nonmoving boards,
   preserving sectors10/11. No assistant flashing or motion is authorized.
2. Verify startup STOP/safety and configure A/B above; each accepted command must
   say SAVED / ACTIVE. Record SHOW on both and check reciprocal IDs/opposite roles.
3. Power-cycle each board without a host/ESP, reconnect and compare SHOW; boot-loaded
   must be YES. Do not infer durability merely from an unchanged RAM display.
4. Capture both serial streams from startup. Run stationary, uncalibrated channel5
   ADS-TWR for >=10 minutes / >=600 attempts, retain all six stamps and failures.
   Check counters, session IDs, raw validity, UART faults and safety endpoints.
5. Swap roles using SET only, repeat with the same ELFs, and compare telemetry OFF/ON
   plus a genuine UWB-OFF baseline. Do not treat counter snapshots as matched PDR.
6. On an isolated backed-up test unit, interrupt configuration saves and verify
   old/new complete settings; test corruption and actual sector rotation separately.

All host persistence/power-cut tests are simulations, not physical flash validation.
Absolute ranging accuracy, UART fault attribution, radio timing and Stage3G hardware
acceptance remain open. Downgrades need explicit settings review because older
images cannot read schema v2. Current operating instructions, feature tests and
the ordered hardware campaign are in [uwb_network.md](uwb_network.md).

## Untethered telemetry extension (software-tested)

Universal UWB-only now includes optional UART3 Wi-Fi transport. Factory `wifi off`
retains the quiet radio baseline. `CFG SET wifi on` enables the existing ESP32
bridge; `CFG SET telemetry on` produces bounded summaries, and `CFG SET samples on`
adds range records. `telemetry_ms` is 1000..60000. `telemetry_format compact` is
currently the only encoding. `TLM NEXT` also retrieves the same records over USB.
`DIAG RESET` resets diagnostics only while an ADS transaction is idle.

The line format is UWB2 plus 304 hexadecimal characters: 152 binary bytes with
CRC16-CCITT, full 40-bit timestamps, integer millimetres and explicit radio epochs.
Receivers accept UWB1 too. STM32 has separate four-record USB and Wi-Fi queues;
ESP32 has eight optional records. Old Wi-Fi records expire after one second.
Safety/control queues have priority. An already started 310-byte UART record
cannot be preempted (about 27ms at 115200 baud); physical coexistence needs testing.
Telemetry and identity never authorize ARM or movement in this image.

With the existing ESP32 provisioned on Wi-Fi, run from epuck-esp32:

```sh
python3 tools/uwb_collect.py --robot 192.168.1.21:3333 --robot 192.168.1.22:3333 --raw
```

The read-only collector sends session heartbeats and saves per-robot JSONL files,
including separate host UTC/monotonic reception times, sequence gaps and reconnects.
It never sends motor commands. Only one client can own each existing bridge port.
Host reception times are not radio timestamps. Queue loss and UART/PHY errors are
explicit measurements, not evidence of their causes. Compare Wi-Fi disabled,
connected/telemetry off and connected/telemetry on before claiming coexistence.
Schema-v1 persistent records migrate in RAM, preserving calibration and antenna
delays; Wi-Fi defaults off. No automatic boot write or configuration reset occurs.

Experimental custom-network modes `hybrid` and `hybrid_static` are documented in
[hybrid_uwb.md](hybrid_uwb.md). They reuse the existing DW1000 TDMA protocol as a
passive DWM3000 RLR and preserve the normal configuration journal and safety policy.

## USB help topics

The Universal image now emits a26-line ASCII overview for `help`, grouped into
General, UWB configuration, UWB diagnostics, Peers, Telemetry and Robot safety.
No system-wide `status` command or new robot alias is invented. Details are paged:

|Command|Contents|
|---|---|
|help config / cfg help|Actual fields/values, persistence, calibration, examples|
|help hybrid|Passive modes, status/diag/frames/frequency, age/rate/rejection interpretation|
|help peers / peer help|List/stats/add/remove/enable/disable/interval, ID constraints|
|help telemetry|Enablement, queues, retrieval, output options and counter limits|
|help safety|Existing stop/status commands and UWB-only restrictions|
|help all|Overview followed by every topic in the same grouped order|

Topic aliases `help cfg`, `help peer`, `help tlm` are accepted. UWB commands and
help topics remain case-insensitive. Unknown topics list the valid topic names.
Legacy robot commands remain case-sensitive: send `STOP`, `MOTOR_STATUS`,
`STREAM_STATUS`, `LINK_STATUS` in uppercase. The safety topic says this explicitly
before showing their lowercase reference names. No new lowercase motor/control
aliases are introduced; arming/movement/stream changes stay prohibited.

Help is a fixed flash table plus bounded pager state. At most one line is queued
per main-loop service call; failed enqueue retries without advancing. Four of the
existing16 USB message slots are reserved for other replies. Optional UWB summaries,
raw-time traces and observation text pause while help is being paged; radio service,
telemetry production/forwarding and critical errors/safety handling continue. No
CDC wait, heap allocation, buffer-size increase, persisted verbosity change or RF
reconfiguration occurs. Safety/errors may interleave deliberately. Issue commands
one at a time and let a help page finish; another paged response replaces it.

Routing investigation: usb_console.c accumulates fragmented bytes until newline,
then calls uwb_runtime_command before epuck_link_console_command. A complete
case-insensitive cfg show is always consumed by the runtime, including read-only
commands under the UWB-only mutation gate. Tests exercise that path and USB CRLF/
fragmentation separately. A real newline between CFG and SHOW makes two commands;
SHOW alone falls through to the safety dispatcher and reproduces its generic
prohibition error. This is a demonstrated framing example, not evidence explaining
the earlier sporadic valid-command report. No valid cfg show misroute was reproduced.
Unknown help topics previously fell through; they now return a topic error within
the runtime. No safety checks are weakened or unknown command fragments joined.
If the sporadic symptom recurs, the indispensable evidence is the exact bytes and
line endings sent for that one command plus its immediate response, not more RF logs.


`hybrid frequency` is a bounded read-only snapshot command. Raw diagnostic capture
requires Universal built with `MUL_HYBRID_FREQ_DIAG=ON` (default OFF). It does not
set receiver rate or enable metric TDoA. See docs/hybrid_uwb.md for exact optional
build/ELF, pre-rearm semantics, sign uncertainty and physical validation limits.


Checkpoint C uses `cfg set reference 0x42` and `cfg set follower 0x23` independently
of the RLR address and legacy ADS initiator/responder role. Both configured ALRs
must be distinct, enabled peers before activating hybrid; the RLR ID must differ.
Change the roster while mode is off when replacing participants. `cfg show` now
includes the saved/active snapshot's explicit roster. It does not infer roles from
84/85 when a custom roster is configured. Calibration and peer lists are retained.

# Universal STM32/DWM3000 configuration

One UWB-only image supports both ADS-TWR roles and any distinct local/peer byte
addresses. Each accepted SET validates the entire configuration, safely restarts
UWB, saves and verifies it, then confirms `CFG SAVED / ACTIVE`. No normal APPLY,
SAVE, radio-off command or maintenance mode is needed. A rejected command leaves
settings unchanged. Wait for the confirmation before sending another change.

Robot identity is metadata, independent of radio role. Motor-enabled normal
operation remains a separate legacy build: the universal image cannot ARM or
move motors, keeps startup STOP/retries/watchdogs/emergency stops, disables ESP
and sensor streaming, and needs no ESP32, robot controller or host for UWB.
There is no TDMA/TDoA implementation or UART/Wi-Fi telemetry forwarding.

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
| mode | off, twr | off |
| role | initiator, responder | responder |
| interval_ms | 250..60000 ms | 1000 |
| phy | mixed: channel5, 6.8Mbps, code9, preamble128 | mixed |
| tx_ant_dtu / rx_ant_dtu | 0..65535 DW timestamp units | 16380 each |
| calibration | on, off | off |
| correction_mm | Signed -10000..10000 mm | 0 |
| verbosity | quiet, summary, full | full |
| telemetry | on, off | off |
| telemetry_ms | 1000..60000 ms | 1000 |
| samples | on, off; optional compact range samples | off |

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

The existing 128-byte little-endian schema v1, CRC32/IEEE, modular generation and
commit-last journal are unchanged. Existing valid records (including old 0x84..86
addresses/numeric stored enums) load without migration writes. New names are parser
aliases; factory defaults change only for blank/corrupt storage or explicit DEFAULTS.
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
NEXT respects USB backpressure. UART3/Wi-Fi forwarding remains deferred. Compare
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
acceptance remain open. Rollback baseline: STM32 a1db617bce685731d16090e96652ee42ace0830c;
old settings remain schema-compatible, but old firmware cannot use new addresses
outside its former allowlist.

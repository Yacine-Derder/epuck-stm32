# Runtime UWB configuration

The universal image is a UWB-only experimental profile. Robot identity is metadata,
independent of ADS initiator/responder role. Factory defaults disable UWB and
calibration. The existing compile-time normal, PHY and MUL diagnostic profiles
remain available. No TDMA/TDoA mode is implemented.

## Storage design

The target is STM32F405RG (1 MiB), with the application vector at 0x08000000;
there is no relocated application bootloader in the linker/startup configuration.
No existing persistent data or flash writes were found. Reserve sectors 10 and
11, 0x080C0000..0x080FFFFF (128 KiB each), and reduce application FLASH to 768 KiB.
The linker must reject any application load section crossing this boundary.
The backend must also verify the actual device flash-size register before access.

References: ST [RM0090, Flash memory interface](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-and-stm32f407-advanced-armbased-32bit-microcontrollers-stmicroelectronics.pdf)
and [DS8626, flash characteristics/endurance](https://www.st.com/resource/en/datasheet/stm32f405rg.pdf).
Flash access stalls during programming/erase on this single-bank part. SAVE is
maintenance, not a realtime operation; radio and robot control must be quiescent.
Use a stable supply in the specified programming voltage range. The specified
endurance is 10 kcycles; saves append 128-byte records (1024 per sector) and
identical saves do not write. This is not an unlimited-write settings service.

Records explicitly encode little-endian magic, schema version, length, modular
32-bit generation, 18 configuration words, zeroed extension space, CRC32/IEEE,
and a commit word programmed last. Scan both sectors and accept only completely
validated records. Skip torn records. At rollover erase the other sector,
preserving the newest committed record. Interrupted save recovers the previous
record or the fully committed new one. Invalid/incompatible records fall back
to safe defaults; no silent schema migration. SAVE is explicit, never per SET.
Mass erase, old images with unrestricted link maps, external writes to sectors
10/11, or interrupted firmware programming can destroy settings; redundancy is
not protection against those actions. Back up SHOW output before programming.

## Schema and calibration conventions

Node/peer are different 0x84..0x86 IDs in the pinned shared-core extended address
namespace (ID,00,00,00,00,00,CF,BC). Local validation cannot detect another robot
using the same ID; provision unique IDs before putting boards on air. Identity
0=standalone,1=ALR,2=RLR; mode 0=off,1=ADS; role 1=initiator,2=responder.
Only PHY 0 is supported: existing channel5/6.8Mbps/code9 profile. Unsupported
addresses, modes and PHYs fail rather than silently selecting another behavior.
interval_ms is 250..60000; telemetry_ms is 1000..60000; antenna delays are unsigned
16-bit hardware DTU. Three reserved words must remain zero in schema v1.

TX/RX antenna delays enter hardware once; actual adjusted 40-bit timestamps feed
the unchanged ADS formula. Raw range is nominal metres calculated from those
six timestamps with the configured hardware delays. It is not a second range
computed with antenna delay removed. Software calibration is independently
opt-in: calibrated_m = raw_m + correction_mm / 1000. Positive correction increases
range; a positive measured bias normally proposes a negative correction. No
scale factor is supported. Default correction=0, calibration=0. Raw invalid or
negative input remains invalid even if the offset makes its output positive;
a negative calibrated result also remains invalid. Neither value is clamped.
Changing hardware antenna delays invalidates any previously fitted software
offset: collect new calibration data with the intended delay configuration.

## Build and console

From epuck-stm32 (no hardware is flashed by these commands):

```sh
cmake -S . -B build/Universal -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Release -DMUL_UNIVERSAL=ON -DMUL_UWB_ONLY=ON \
  -DMUL_TWR_ROLE=0 -DMUL_DIAG_ROLE=0 -DPHY_INTEROP_ROLE=0 \
  -DMUL_TIMING_TRACE=ON
cmake --build build/Universal
```

ELF: `build/Universal/EPuck_STM32.elf`. One image contains both ADS roles.
Internally CMake supplies MUL_TWR_ROLE=2 as the pinned core's inclusion/timing
profile guard; the universal adapter selects roles exclusively from configuration.
Legacy selector cache values must be zero for this build. The universal image
requires the existing UWB-only policy; ESP/Wi-Fi and automatic sensors are off,
ARM/movement are rejected, and UART1 startup STOP/retries/watchdogs remain active.
An absent e-puck/ESP/host does not block UWB. Normal motor-enabled operation still
requires a legacy normal build; runtime configuration cannot weaken safety policy.

Commands are newline-terminated over existing USB CDC. Input is bounded to 63
printable characters with CRLF accepted; malformed/overlong lines are discarded.

```text
CFG SHOW
CFG SHOW ACTIVE
CFG SHOW SAVED
CFG SHOW DEFAULTS
CFG SET address 0x86
CFG SET peer 0x84
CFG SET identity 2
CFG SET mode 1
CFG SET role 2
CFG SET interval_ms 1000
CFG APPLY
```

This configures an e-puck RLR responder. For a standalone board use identity 0.
For a WaveRover ALR, select identity 1, e.g. address 0x84 and peer 0x86. Set peer
first when replacing the default node with 0x84 to avoid a transient duplicate:
`CFG SET peer 0x85`, `CFG SET address 0x84`, `CFG SET peer 0x86`.
To switch either identity to initiator: `CFG SET role 1`, `CFG APPLY`.
Use role 2 to switch back. APPLY stops any exchange (recording cancellation),
reinitializes the radio with the selected hardware antenna delays, resets run
counters and obtains a fresh nonzero hardware-RNG session. No compiled session or
flash write is needed per boot. Entropy/driver/readback failure leaves radio off;
SHOW reports unapplied/pending state. Fresh sessions are probabilistic unique
64-bit nonces, not cryptographic replay protection. The peer must be configured
with opposite role and matching reciprocal IDs. Existing DW1000 role profiles
remain wire compatible; do not change their PHY or shared core.

SET/LOAD/REVERT/DEFAULTS update only current desired settings. ACTIVE describes
what was last successfully applied (with explicit applied/radio_running flags),
SAVED the latest valid nonvolatile record, DEFAULTS the immutable factory values.
APPLY makes current settings active; SAVE persists current settings, independently
of APPLY. LOAD and REVERT are synonyms; neither writes flash or starts a radio.
Defaults can only replace saved settings with an explicit SAVE.

Inspecting Bitcraze `src/main.c` revealed a single-character menu (`i` decimal
node ID, `m` mode, `r` radio, `d` reset) with immediate EEPROM persistence/reset,
not a line-based SAVE interface. STM32 already uses newline commands for safety.
The CFG namespace therefore reuses address/node, mode, radio and default concepts,
with the project's familiar 0x84 representation, but deliberately avoids a modal
menu and implicit flash writes. `address` aliases `node`; numeric enum values and
units are explicit. This is an intentional interface difference.

Saving a working ADS profile:

```text
CFG SET mode 0
CFG APPLY
CFG SET mode 1
CFG SAVE
CFG SHOW SAVED
CFG SHOW
CFG APPLY
```

SAVE requires active radio off, startup watchdog grace >=2 s, and confirmed
STOP plus STREAM0 on an attached e-puck. A peer with no received UART bytes after
the grace period is treated as absent, allowing standalone provisioning. This
cannot establish physical motor status on disconnected hardware; isolate wheels
and never provision a robot whose independent motor controller lacks a working
watchdog. A present but unconfirmed peer refuses SAVE. Single-bank flash stalls
are intentionally confined to this maintenance operation. No IRQ/watchdog/control
loop is disabled. A reset during SAVE recovers the previous/new complete record.
Mass erase loses configuration. Sector-only programming of the ELF should leave
reserved sectors untouched; verify programmer erase options yourself.

Calibration and diagnostics, each followed by APPLY:

```text
CFG SET tx_ant_dtu 16380
CFG SET rx_ant_dtu 16380
CFG SET correction_mm -450
CFG SET calibration 1
CFG APPLY
CFG SET calibration 0
CFG APPLY
```

The example offset is syntax only, not a fitted or recommended calibration.
`MULH RESULT`/`TIMES` preserve raw six-timestamp audit output. `MULH CAL` adds
raw_m, calibrated_m, calibration and derived range_valid. verbosity 0 suppresses
routine results/statistics, 1 keeps results/summaries, 2 includes six timestamps.
Important errors/init/config output remains. SHOW gives active hardware delays.

## Telemetry infrastructure and limitations

```text
CFG SET telemetry 1
CFG SET telemetry_ms 1000
CFG SET samples 0
CFG APPLY
TLM STATUS
TLM NEXT
CFG SET telemetry 0
CFG APPLY
```

The separate machine queue holds four complete records of <=191 bytes. Producers
run only in the main loop, never transmit or format in interrupts, never allocate
memory, and drop newest on saturation. STATUS exposes occupancy/capacity/high
water/cumulative dropped and accepted. APPLY accounts for discarded old records.
NEXT copies one record only if the USB output queue can accept it; USB backpressure
retains the item. Machine records start `TLM STAT` or `TLM RANGE`; ordinary debug
starts MULH. Optional samples=1 emits raw/calibrated valid flags per completion,
up to the configured transaction rate. Summaries are 1 Hz by default and include
node/peer/session/role, attempt/done/abort/reject/bad_range counters. Queue counters
are not RF loss counters. No host attached simply saturates the bounded queue.

No UART3/Wi-Fi forwarding is enabled: that path currently carries a safety/session
protocol and previously exhibited overruns. This phase delivers STM32-side
infrastructure only; an acknowledged/backpressure-aware integration remains.
ESP firmware is unchanged. Compare MULH TIME maxima (SPI interrupt-off cycles,
service cycles, service gaps, calls, clock Hz) with telemetry disabled/enabled and
with a stalled USB reader. Instrumentation covers formatting/enqueue work; it is
not a worst-case execution-time proof. Saves and APPLY initialization are excluded
from steady-state timing and must be marked as maintenance in captures.

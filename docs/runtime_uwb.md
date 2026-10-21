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

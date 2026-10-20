# MUL STM32 development

## Verified baseline

Stage3F bidirectional channel5 packet interoperability is accepted from user
logs; this does not validate ranging or combined safety timing. The central
project document remains read-only and needs an owner status update.

## Stage3G

**Implemented/software tested; hardware gate pending. Do not begin Stage3H.**
Canonical LPS portable/mul_core is vendored with offline hash verification,
pinned to1b807c5c7507d67d802ee976ae4dfe5b27fd88e9. No sibling checkout, third
remote or new submodule is required. The44-byte diagnostic carries explicit
clock identity/40-bit DTU and previous completed TX metadata. Stage3E math,
radio frame formats and the core pin are unchanged in this follow-up.

User A/B marker comparisons match43/43 and62/62, but TX endpoints89/93 fall
short of the >=100 requirement. C demonstrates >=542 valid RX with6 gaps and
USART3 ORE0x8. No recorded new gap occurs near that visible fault. Confirmed
UWB-disabled baseline sessions1000ms and100ms have zero UART/drop/parser faults.
Unequal captures prevent a matched delivery ratio or proof of SPI causation.
SPI IRQ-off297.5us is a concrete concern; masking/speed/priorities remain unchanged.

Follow-up integrates the reviewed local streaming work, adds explicit UWB-only
startup STOP/disarm/stream-off and command rejection, quiet debug telemetry,
concise fault/counter output, and normal1Hz compact DATA2 without lights.
Normal mode remains available; safety deadlines/heartbeats are not throttled.
The compatible ESP bridge/client update is required for normal DATA2 operation.
No dsPIC change. Tests cover policy safety/ACK retry, telemetry tick wrap and
coalescing, core/diagnostic/timing,15 console/9 ESP/9 interleaved cases. Five
Release profiles build; UwbOnlyRX static RAM17020, CoexistRX27792 bytes.
Software tests do not establish physical interrupt latency or motor safety.

Next: user sustained UWB-only >=10min/>=500 completed TX per direction, then
matched UwbOffSafe baseline; optional NormalBaseline/CoexistRX comparison.
Require STOP acknowledgement/disarm, exact metadata, UART/drop/error/timing and
capture endpoints. [docs/mul_core.md](docs/mul_core.md) links canonical results,
exact builds, CubeProgrammer ELF paths and acceptance instructions. No hardware
flashed. Full Stage3G validation and Stage3H await adequate physical evidence.

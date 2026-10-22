# Current checkpoint C — independent persistent roles/addresses

The user authorized proceeding with C/D while preserving ordinary hybrid NO_RATE.
A's original age/relation defect is physically accepted from the user's STM
f1047b3/8c5fdc5 retest (last_status1, clock1, pairs27, passive TWR5). Later3 rejects,
one timeout and PHY errors remain open. Metric TDoA/accuracy are unvalidated.

B feasibility and validated components are complete in software: CFO and timestamp
cadence still mix clock with Doppler. No independent motion/frequency or justified
bounds exist, so no general live moving estimator is admitted. hybrid_static stays
stationary-only; conditional host output has measurement=null. ESP docs/uwb_mobile.md
contains the model, alternatives, raw diagnostics and explicit bounds contract.

C implementation/software validation:
- Canonical core pin0cb81e3 adds explicit per-context rosters through all relevant
  codecs/clock/delay/relation/association/TWR/math boundaries; defaults remain84/85.
- LPSd63a29d integrates persistent independent roles/addresses and bounded :mul
  configuration commands, with unchanged modes0..6, timing and packet layout.
  The follow-up bounds TLV traversal before migration. Existing invalid EEPROM
  is retained with normal RF idle, without silently resetting calibration/settings.
  Legacy EEPROM is not a power-cut journal.204 Unity tests pass; default channel2
  and channel5 at500/50/10ms build. Same ELF serves both DW1000 ALR roles.
- STM32 schema3 adds reference/follower IDs within the existing128-byte journal.
  V1/v2 load without migration writes; calibration/peers/antenna values persist.
  Power-cut migration/rotation tests and real adapter tests cover arbitrary42/23,
  swapped85/84, full passive TWR,40/32-bit wrap, NO_RATE, no TX and self-ID rejection
  at500/50/10ms. Runtime/help/USB, safety, ADS, PHY, SPI and ESP-link suites pass.
- ESP replay/mobile CLIs accept explicit --reference/--follower IDs; native C uses
  that same roster. UWB1/2/3 wire remains unchanged. All62 host tests and the ESP
  firmware build pass; conditional outputs retain real IDs and no point validity.
- Both consumers vendor verified core0cb81e3. Universal, Hybrid50/10 and optional
  HybridFrequency builds pass reserved-sector ELF checks. No devices flashed.

Next: finish C pushes, then D's compatibility matrix and unified lowercase command
aliases. Preserve immediate legacy menus, persistence/calibration, passive RLR,
NO_RATE and UWB-only safety. Do not claim moving metric or custom-roster hardware
validation. Operator retest/build/ELF and exact configuration examples are in
STM docs/hybrid_uwb.md and LPS docs/mul_heterogeneous.md. Any residual PHY/TWR/UART
fault requires fresh evidence; more UWB-only logs cannot resolve clock/motion
observability. Existing LPS docs/mul_core.md formatting edit remains untouched.

---

# MUL STM32 development

## Verified baseline

Stage3F bidirectional channel5 packet interoperability is accepted from user
logs; this does not validate ranging or combined safety timing. The central
project document remains read-only and needs an owner status update.

## Stage3G

**Implemented/software tested; hardware gate pending. Do not begin Stage3H.**
Canonical LPS portable/mul_core is vendored with offline hash verification,
initially pinned to1b807c5c7507d67d802ee976ae4dfe5b27fd88e9 (current pin below). No sibling checkout, third
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

## ADS-TWR foundation

Portable core pin updated to ccd78acfb56ac9ea42c31b88d5db458380c67564. New opt-in
MUL_TWR_ROLE1/2 is exclusive with diagnostics and requires MUL_UWB_ONLY=ON.
Both initiator/responder adapters compile and pass measured-timestamp/recovery
host tests. Three ranging frames plus REPORT return all six actual40-bit
markers to the initiator; no intended TX timestamp is substituted. Fresh explicit
initiator session required after restart; timeouts retry whole exchanges.
Physical ranging, accuracy and antenna equivalence remain unverified.

Stage3G logs now locally available reproduce the retained-baseline OLD anomaly;
valid counts decoding, not acceptance. Explicit order and logged idle epochs
make this visible without changing counter meanings. No console/PHY count alone
establishes zero loss. Reverse sustained hardware gate and genuine STM32 disabled
baseline remain pending (the newer off-baseline file contains DW TX output).
Canonical [ADS handoff](https://github.com/Yacine-Derder/lps-node-firmware/blob/master/docs/mul_ads_twr.md)
records exact builds/ELFs, safe stationary tests, limitations and cleanup inventory.
No ESP/dsPIC update or optional Wi-Fi repair. No hardware flashed.


## First ADS logs: audit and hardening

The user's first mixed-radio ranging logs show computed exchanges, not calibrated
accuracy or a passed safety/hardware gate. The negative sample is reproduced from
its six inputs without changing the formula; physical cause remains unknown.
One visible USART1 e-puck ORE report has cumulative uart5/drop14; no synchronized
link to the initiator outlier can be established from the partial responder log.

Current core pin0e3b5802e04f87e2c6e5e41ae07c165ae07ea0d1 separates raw completion
from nonnegative range validity, retains raw negatives, and classifies abort causes
and failed stages. New session-tagged diagnostics and optional ADS timing use
bounded output, while normal/Stage3G and UWB-only safety behavior stay available.
No SPI/IRQ policy, antenna constants, ESP/dsPIC or equation changes. Software tests
and both-role/traced/quiet builds pass; hardened firmware has not been flashed.

Next: canonical docs/mul_ads_twr.md multi-distance procedure,>=500 completions per
precisely measured separation, full two-device/safety logs and disabled baseline.
No new Stage3H work or hardware gate closure. Physical accuracy/negative-range
cause and safety-UART overrun remain unresolved.

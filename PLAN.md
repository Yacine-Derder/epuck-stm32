# Current checkpoint B — moving receiver-rate feasibility

Checkpoint A original age/relation failure is physically accepted from the user's
retest of STM f1047b3/8c5fdc5, ALRs unchanged: last_status0→1/stays1, clock1,
pairs0→27, passive TWR0→5. After26 pairs, rejects0→3 with one timeout and PHY
errors: residual delivery failures remain open. Rate UNKNOWN and metric TDoA NA
are expected; neither metric TDoA nor accuracy/position is physically validated.

B feasibility and conditional offline prototype complete/software tested:
- True oscillator rate and radial motion are not identifiable from these two-ALR
  timestamps alone; deterministic identical-history examples prove it. Even
  smooth between-packet clock curvature can hide different geometries.
- A bracketing reference-ARRIVAL mapping can provide a conditional simultaneous
  range-difference interval without calling its apparent slope oscillator rate.
  The first implementation is offline; no mobile firmware validity flag changes.
- Explicit speed/acceleration/minimum reference distance, oscillator scale/rate
  slew, physical baseline and metrology bounds are all mandatory. No defaults or
  ADS correction substitute for an error bound. Missing bracket/invalid relation
  stays unavailable; conditional outputs never feed the point-based2D solver.
- ESP mainbb6fd59 records feasibility before implementation; cdf069c implements
  tools/uwb_mobile.py and actual shared C replay extraction. 93fd7e4 hardens native-context recreation. All53 Python tests,
  host C transport/replay builds and12 focused physics/mobile tests pass.
- STM maina867702 records A physical acceptance; this documentation follow-up
  links that host-only B result. At that checkpoint no firmware or build files
  changed. Canonical/vendor pin3bbd324 remains unchanged in this follow-up.

B follow-up: user confirms no independent motion/frequency input, integrated
encoder model or verified bounds. DW3000 carrier/CIA APIs support ordinary mixed
Ipatov PHY diagnostics, but CFO still mixes clock with radial Doppler; noiseless
CFO plus cadence is structurally ambiguous. No defensible general live moving
metric estimator is admitted. hybrid_static remains the stationary baseline.
- New Universal-only MUL_HYBRID_FREQ_DIAG option is OFF by default. Optional
  hybrid frequency captures raw CI/COE/status before rearm; no true-rate/metric
  admission, on-air changes, TX, persistence or arbitrary correction.
- Bundled COE sign comments contradict the vendor guide. Raw values and API
  algebra stay labelled; hardware sign/bias/noise remain unvalidated.
- ESP df5dfc5 tools/uwb_frequency.py decodes snapshots and compares explicit synthetic
  Doppler/frequency/holdover scenarios at500/50/10ms. No numerical hardware prior.
- Actual vendor decoder/all8192 COE codes, passive adapter guards, runtime/help,
  safety/ADS/PHY/SPI/ESP-link suites and all59 host tests pass. Universal and optional
  HybridFrequency build and respect reserved flash sectors. No devices flashed.

Next: choose and justify an independent path-rate constraint, an observable
encoder/relative-mapping model, or explicit motion/clock/metrology bounds before
admitting mobile metric output. Stationary calibration needs finite holdover/error
bounds; raw CFO alone is not an independent frequency reference. No more UWB-only
logs can remove that ambiguity. Optional stationary raw-frequency/reference
comparison would validate sign/scale/bias, not mobile observability. See expanded
ESP docs/uwb_mobile.md and STM docs/hybrid_uwb.md for alternatives/build/ELF details.
The previous UWB3 offline interpolation still works with STM f1047b3; the diagnostic
build is optional and physically untested.
Full analysis/use/tests/limitations: epuck-esp32/docs/uwb_mobile.md; A acceptance:
docs/hybrid_uwb.md. hybrid remains NO_RATE and hybrid_static stationary-only.
Residual intermittent TWR/PHY faults stay open. C/D remain deferred by user request.
No new physical test was performed by the assistant.

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

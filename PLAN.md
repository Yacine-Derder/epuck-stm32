# Current checkpoint A — capture-age correction and USB help

User physical diagnostics and three pasted raw frames identify an adapter format
bug: DW3000 SYS_TIME is four bytes bits39:8, previously decoded as five full bytes.
The incorrect host mock hid it. Real-contract mock reproduces failure; correction
reconstructs local40-bit time conservatively without increasing15ms threshold.
The same invalid65535 age caused TWR capture_age and available-relation failures;
no separate clock-model or wire-protocol change is justified.

Completed/software tested: corrected adapter, real-contract mock, actual supplied
raw C codec vectors,500ms pairing, reported v2 relation ages, wrap, missing TWR
stages, late capture and expiration guards. Runtime tests now match6000ms firmware
policy. No ALR/ESP/core protocol change; vendor pin remains LPS3bbd324.
Capture-age fix pushed STM main8c5fdc5. Independent help cleanup is implemented:
26-line overview, six detailed/all topics, legacy entry points/aliases, bounded
backpressure pager, optional text suppression only. Real parser/pager/USB framing,
read-only/mutation gates and concurrent passive RX are tested. Broader B–D remains
deferred. Full root cause/raw-vector interpretation and exact retest are in
docs/hybrid_uwb.md; help contract/routing evidence in docs/runtime_uwb.md.

Validation: runtime/core sanitizer, real ADS/PHY/SPI/UART/safety suites; seven STM
profiles and ELF reserved-sector checks; unchanged ESP transport/41 Python checks.
Remaining: user physical retest of latest Universal ELF, settings/ALRs unchanged,
small valid capture ages, clock=1/NO_RATE for fresh pairs, TWR completion only on
complete listener history, RX continues through help. No physical fix/accuracy,
PHY-delivery or UART-fault closure claimed. Valid cfg show misrouting is not
reproduced; exact byte framing would be needed only if it recurs. No protocol or
clock-freshness change beyond the shared invalid-age input correction is justified.

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

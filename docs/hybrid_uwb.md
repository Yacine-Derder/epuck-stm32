# DWM3000 receiver for the custom DW1000 MUL network

This mode replaces the passive DW1000 RLR, not the ALR scheduler. Reference source
and wire mapping are in lps-node-firmware/docs/mul_heterogeneous.md. ALR0x84/0x85
retain their original1Hz hardware TDMA and directed inter-ALR TWR650..800ms.
The RLR does not transmit. Other universal ADS/beacon/listen modes remain separate.

## Build and configuration (software-tested, physical acceptance pending)

Use the normal Universal build in runtime_uwb.md, additionally selecting the
**same follower slot as both ALRs**:

```sh
cmake -S . -B build/Universal -G Ninja \
 -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
 -DCMAKE_BUILD_TYPE=Release -DMUL_UNIVERSAL=ON -DMUL_UWB_ONLY=ON \
 -DMUL_TWR_ROLE=0 -DMUL_DIAG_ROLE=0 -DPHY_INTEROP_ROLE=0 \
 -DMUL_HYBRID_SLOT_MS=500 -DMUL_TIMING_TRACE=ON -DMUL_ADS_TRACE_TIMES=ON
cmake --build build/Universal
```

ALRs need an explicit channel5 mixed-profile build because the original channel2
is unsupported by DWM3000. In lps-node-firmware:

```sh
make clean
make -j4 EXTRA_CFLAGS='-DMUL_HYBRID_CH5=1 -DMUL_FOLLOWER_SLOT_MS=500 -DMUL_PROP_MAX_AGE_MS=6000'
```

This only changes PHY in persisted custom modes5/6. It does not select a diagnostic
algorithm, alter TDMA, packet bytes or EEPROM. Default builds retain channel2.
ALR modes/IDs are still configured with the existing USB menu (mode5, ID132/133).
No assistant flashing is authorized. Preserve calibration/configuration sectors.

For the STM32 RLR, one command at a time after the existing safety grace/ACKs.
Keep an existing unique RLR address if already provisioned;0x21 is an example:

```text
CFG SET mode off
CFG SET address 0x21
CFG SET role responder
PEER ADD 0x84
PEER ADD 0x85
CFG SET wifi on
CFG SET telemetry on
CFG SET mode hybrid
HYBRID STATUS
CFG SHOW
```

Existing saved peers may already include these ALRs: inspect PEER LIST rather
than resetting settings. Keep the existing primary ADS peer and calibration; ADD
the ALRs instead of replacing the primary with CFG SET peer. A full table needs
unneeded non-primary entries removed first. Changing/removing the primary would
correctly disable its pair-specific calibration, so avoid that for passive setup. The complete configuration is validated before a save;
hybrid requires both legacy ALR peers enabled and an RLR identity distinct from
the ALRs. Extra configured peers are retained but the legacy wire roster remains
unchanged. `mode hybrid` retains the original UNKNOWN receiver-rate policy:
association and relation can be valid while geometric TDoA is NO_RATE.

For explicitly stationary experiments only:

```text
CFG SET mode hybrid_static
```

This opts into the original cadence-based rate estimator, conditional on stable
propagation. It is not a mobile-clock solution. Inter-ALR TWR supplies propagation
correction between anchors, not receiver oscillator skew and not an RLR range.
DWM3000 RX uses its native antenna-adjusted40-bit hardware stamp; DW1000 empirical
power-bias tables are deliberately UNSUPPORTED on this radio. Existing antenna
and additive ADS calibration settings persist unchanged; ADS additive offsets
are not applied to TDoA. PHY is channel5/6.8Mbps/PRF64/code9/preamble128/IEEE SFD.

## Telemetry

UWB3 retains the same152-byte/310-UART-byte bound and CRC16 framing. UWB1/UWB2
remain decodable. New type6 hybrid_rx carries the untouched legacy frame (34/74/
84bytes), native local RX, outward-rounded hardware capture age, source sequences,
receiver epoch, inferred source generations, configured slot and explicit rate
policy. The ALR follower epoch is acquisition-local, never a global boot UUID.
Summaries retain full queue/UART/PHY/timing diagnostics. Raw records need telemetry
ON; they are not gated by samples, because offline replay requires all exchanges.

HYBRID STATUS reports received/rejected frames, pairs, last observation status,
reference/follower freshness, observed
inter-ALR TWR completions/rejections/timeouts and radio faults. DIAG RESET clears
only diagnostics, preserving protocol ordering, model/safety state and settings.
The mode has no TX operation; multiple RLRs add no airtime. Lost relations are not
replaced from a stale cache. Missed packets can suppress pairs and TWR completion
rather than inventing measurements. Epoch/config restarts require warmup.

Checkpoint B: adapter compiles; actual adapter/safety/config/queue regressions and
shared original receiver tests pass for500/50/10ms. Checkpoint C: producer/bridge v3 and targeted UART software hardening implemented.
Checkpoint D: shared-core offline replay and guarded generic2D solver pass synthetic
end-to-end and geometry/validity tests. Physical validation remains pending. Hardware interoperability and accuracy have not been tested here.

## Historical UART handoff — user-reported 2026-10-22 evidence

The original four JSONL captures were deleted. The following is the user's
previous analysis, preserved as historical diagnostic evidence; it has not been
recomputed from files and is not new hardware validation.

- Mode-OFF baseline A (192.168.1.141): ~60s,60 telemetry records,1 missing sequence,
  one reconnect (`stm32_session_lost`, control_hb_timeout, hb_ms500), no corrupt
  records/UART errors. STM32 aggregate drops308→369. Baseline B (192.168.1.142):
  ~55s,55 telemetry records, no gaps, stable after initial connection, drops66→120.
  Both had tx_dropped0 and ESP UWBSTAT dropped0. Unconsumed USB queues likely
  dominated aggregate STM32 drops; these are not Wi-Fi loss measurements.
- ADS initiator A0x8D→0x8E: ~219s,641 JSONL rows (419 telemetry:216peer/203range;
  220bridge), stable session after initial failure. Sequences89..510 with3 missing.
  ESP final dropped3/corrupt0/UARTerrors0/depth0/high1. Attempts48→266 (218),
  completions42→245 (203;~93.1%), aborts5→21 (+16), PHY errors0→19,
  timeouts3→5042, invalid/reject0.203 valid raw=calibrated uncalibrated samples:
  min1.954m,max2.018m,mean~1.9886m. STM32 UARTerrors0,tx_dropped0,
  aggregate STM32 drops173→595. Idle radio RX timeouts are not failed exchanges.
- ADS responder B0x8E→0x8D: ~224s,498 JSONL rows (214peer telemetry, no ranges,
  245bridge),20 connects/19 disconnects,11 missing telemetry sequences.
  ESP final dropped11/corrupt0/UARTerrors0/depth0/high1. Attempts63→279 (216),
  completions61→269 (208;~96.3%), aborts2→10 (+8),timeouts0→3,
  PHY errors0,invalid/reject0. STM32 UARTerrors0→35,tx_dropped0,
  aggregate drops161→386. Responder range absence is expected by the ADS protocol;
  initiator/responder windows are unmatched, not a joint PDR denominator.
- B repeatedly disconnected with stm32_session_lost / uart_fault; representative:
  `hb_ms=99 seen=1 uart=2 bits=0x8 rx_ovf=0 tx_full=0 q=0 fault_q=0 mask=0x1`.
  UART errors often rose by2 per disconnect; hb_ms~99..101. ESP UART errors stayed0;
  TCP saw EOF and reconnected. Software ring overflow0 does not rule out hardware ORE.
- Later independent A symptom: `TLM STATUS usb=4 wifi=2 capacity_each=4 high=4/3
  drops=4826/4254 accepted=4830 uart_errors=4`. Wi-Fi/telemetry ON, compactv2,
  interval1000ms, ranging active, TCP session ready, repeated UWBSTAT but no visible
  UWB2. ESP dropped6/corrupt0/UARTerrors0/depth0/high1. samples-off and interval1000
  commands were unchanged no-ops, not a genuine before/after load reduction.

Established: homogeneous ADS and Wi-Fi forwarding worked; B session faults strongly
associate with STM32 UART handling; A's later forwarding symptom is separate or
related but unexplained. Cumulative drops cannot establish current loss/root cause.
No evidence establishes which interaction caused the overrun or why A stopped
forwarding. Deleted captures must not be treated as fresh validation.

Code tracing confirms bits is `huart3.ErrorCode`; bundled HAL defines
HAL_UART_ERROR_ORE=0x08. The prior SPI implementation globally masked IRQs even
at the unchanged /64 SPI rate (1.3125MHz). A56..84-byte transfer takes roughly
0.34..0.51ms, versus86.8us per115200-baud UART byte: a concrete overrun mechanism,
not proof that it caused these particular failures. Universal polling mode now
leaves interrupts enabled in the radio mutex because EXTI0 stays disabled and
only main-loop code owns the radio; UART ISRs never access SPI. Other builds keep
the original mutex. SPI speed is unchanged. Universal transactions fail fast on
BUSY and use a2ms HAL timeout, with normal chip-select cleanup. The existing
UART fault detection, counter and session invalidation are retained. There is no
suppression of ORE or artificial heartbeat extension.

Targeted host tests inject HAL ORE with queued telemetry, verify purge and session
loss, then a new OPEN/heartbeat and resumed UWB3 forwarding. Actual platform SPI
callbacks are exercised under simulated UART arrivals, timeout/BUSY failure and
nested/caller PRIMASK conditions. These establish software behavior only. The
legacy spi_off_cycles metric now records only remaining global masked sections;
zero is expected in Universal radio mutex paths. service/gap timing maxima still
cover the complete polled radio service. Fresh physical runs must compare both
boards, RF off/hybrid on, connected-idle/forwarding, UART errors, matched sequences,
TLM STATUS USB/Wi-Fi drops and DWT timing. Neither historical fault is declared fixed.

## Workstation replay and localization

Use the new ESP32 image; the old bridge does not recognize UWB3. Build with the
existing ESP-IDF6.1 activation/commands in uwb_network.md. On the workstation,
from epuck-esp32 (substitute actual endpoint addresses):

```sh
python3 tools/uwb_collect.py --robot 192.168.1.141:3333 --robot 192.168.1.142:3333 --output captures
python3 tools/uwb_hybrid.py captures/*-robot-*.jsonl --slot 500 \
 --output hybrid-observations.jsonl \
 --anchors tools/examples/hybrid_anchors.json --positions hybrid-positions.jsonl
```

Replace the example anchor coordinates with surveyed fixed ALR positions in
metres. Replay compiles the verified canonical C snapshot into a temporary host
library using `cc`; it requires the existing host C compiler and Python standard
library, no radio access or network dependency. Exact original association,
relation inversion, receiver-rate policy and TWR arithmetic are reused, not
reimplemented as a second Python clock model. Output includes reference/follower
sequences, inferred generations, acquisition epoch, local receiver domain, status,
rate policy, native TDoA, metres, unknown physical quality and provenance. Captured
source-generation changes reset host replay conservatively; missing initial history
needs warmup. Start capture before the reference stream for complete evidence.
No fitting uses Wi-Fi receipt time. TWR completion rows explicitly target the
inter-ALR baseline and never become RLR ranges. Incomplete TWR has no reported ToF.

With current two ALRs, position rows correctly have position=null and
insufficient_geometry. Default hybrid also produces NO_RATE. Neither is a failed
transport or a reason to assume equal oscillator rates. hybrid_static makes
conditional TDoA available after the original five-interval stable rate warmup;
it still does not supply another independent position constraint.

The separate generic solver accepts normalized, already synchronized/co-temporal
measurement batches (JSONL with batch_id and measurements[]). Each item has valid,
kind=range or range_difference, anchor ID, value_m, optional sigma_m, and reference
ID for differences. Positive difference means distance(anchor)-distance(reference).
It accepts arbitrary IDs and mixed constraints; it does not parse radio packets or
infer missing synchronization. Anchor JSON has anchors{ID:[x,y]} and optional
bounds[xmin,xmax,ymin,ymax]. CLI/example synthetic fixtures:

```sh
python3 tools/uwb_localize.py tools/examples/localization_synthetic.jsonl \
 --anchors tools/examples/localization_anchors.json --output synthetic-positions.jsonl
```

Both fixture batches recover(1,2)m; these are synthetic tests, not measured output
or a new on-air ALR roster. A caller integrating more future anchors/active ranges
must establish compatible clock domains, measurement times and validity first.
The solver counts independent constraints on per-anchor distances: duplicate or
closed-loop TDoA edges do not add geometry. It requires at least three distinct
noncollinear anchors, and three independent constraints without explicit bounds.
Bounded two-constraint cases are checked with multiple starts and reject multiple
solutions. Invalid, impossible, singular, inconsistent or ambiguous batches yield
position=null. The configurable residual threshold defaults0.25m; it is a software
selection policy, not measured accuracy. Unit/default weights, residuals and matrix
conditioning do not establish calibrated physical uncertainty. No mobile-anchor
covariance or online statistical calibration is implemented.

## Short ordered hardware campaign

No assistant hardware operation is authorized. User programming must preserve
STM32 sectors10/11 and existing calibrated settings. Save SHOW/LIST, firmware SHAs,
ALR boot/PHY banners, physical positions, complete serial streams and Wi-Fi JSONL.
Existing STOP/disarm/watchdog safety requirements remain mandatory; isolate wheels.

1. Program matching channel5/slot500 ALR images. In each LPS USB menu send `i`
   **without newline**, then `132`+Enter on reference or `133`+Enter on follower.
   Send `m` without newline then single key `5`. These settings persist but need
   a reboot. Expected startup: roleREF/FOLLOWER, slot_ms500 and HYBRID_CH5 channel5
   banner. Preserve other EEPROM settings; saved radio-rate menu values are
   overridden only by this explicit custom-mode profile. Both ALRs must show
   packet/scheduled/actual TX equality delta0, follower acquisition and bounded
   inter-ALR TWR. Do not reset EEPROM as a routine deployment step.
2. Program STM32 Universal slot500 ELF, preserve configuration sectors; use commands
   above with `wifi off`, `mode hybrid`. Capture >=10min: both source streams,
   correct cadence, growing pairs, clock association when transported metadata is
   fresh, NO_RATE by default, no RLR transmissions. No geometric value is valid
   merely because a packet decoded. Record errors, gaps and full status.
3. Hold all devices stationary, select `mode hybrid_static`, wait >=6 reference
   packets. Expect conditional AVAILABLE TDoA only when relation/rate/ages match;
   before warmup, loss or invalid relation, expect explicit unavailable status.
   Compare raw TDoA signs at known placements. Absolute error is unvalidated;
   retain geometry and antenna settings. Optional original DW1000 RLR comparison
   needs the same channel5/slot/prop-age and explicit stationary build policy.
4. Turn an ALR off/reboot it, restore it, then reboot the RLR. No stale relation or
   sequence may be paired across inferred generations. Expect loss/NO_RATE and
   warmup/reacquisition rather than extrapolated fake positions. Rapid ALR restarts
   remain limited by the original absent boot UUID. Test two passive RLRs together;
   they must add no RF replies or initiator activity.
5. Set wifi on with telemetry off and repeat, then telemetry on with collector.
   Capture >=10min each, and a genuine mode-off UART baseline. Compare matched
   windows of raw-frame sequences, pairs, inter-ALR TWR observation, PHY errors,
   UART HAL error bits/counts, TLM STATUS separate USB/Wi-Fi queues and DWT service/
   gap maxima. Stalled readers may drop records but must not stop UWB or grow queues
   beyond their bounds. Unconsumed USB drops are expected and are not Wi-Fi PDR.
6. Replay captures with the matching --slot. Validate source/receiver domains,
   generation resets, unavailable masks and conditional TDoA. Two-ALR position
   outputs must remain null/insufficient_geometry. Run the synthetic solver fixture
   separately; never describe it as physical localization evidence.
7. Inspect MOTOR_STATUS/STREAM_STATUS/LINK_STATUS; exercise STOP and collector
   disconnect/reconnect without arming/moving. Confirm disarmed state, normal
   session-loss handling and retained fault reporting. Power-cycle and verify
   mode/peers/antenna/calibration persistence. Existing ADS/beacon/listen profiles
   should be checked separately, with only the intended RF topology enabled.

Do not call either historical UART symptom resolved until repeated physical runs
show stable sessions and quantified error/drop/timing behavior on both boards.
Stop the campaign on wrong associations, accepted bad CRC, unexpected RF TX from
an RLR, invalid positions from insufficient geometry or unsafe motor state.

## Validated software envelope and artifacts

Passing automated checks: shared portable sanitizer suites (including hybrid at
all500/50/10ms),195 LPS Unity tests,178 MUL sanitizer tests at each of those slots,
DW1000 actual-driver ADS/PHY/profile contracts,20 existing Python ADS analysis
checks, STM32 runtime/persistence/flash-power-cut/USB/ADS/diagnostic/PHY/safety/UART
suites, nine coexistence simulations, actual SPI/IRQ fault/preemption tests, and
ESP transport plus41 Python tests. Tests include full C encoder→collector→shared
receiver replay→guarded position output, synthetic ground-truth localization,
wrong/stale epochs, generation changes, CRC precedence, no-RLR-TX, timeout/fault
recovery, calibration preservation and geometry/constraint independence rejection.

Builds pass for STM32 Universal500 and additional Hybrid50/Hybrid10 images, legacy
ADS initiator/responder, NormalBaseline and UwbOffSafe. All seven ELF load segments
stay outside reserved configuration sectors. ESP32-S3 ESP-IDF6.1 builds. LPS clean
channel5 custom builds at500/50/10ms, legacy ADS roles and default build pass; default
bin/lps-node-firmware.elf is restored after saving the opt-in artifacts below.
Existing vendor unused-function/maybe-uninitialized, legacy text truncation and
newlib/syscall warnings remain visible; software tests do not measure physical
stack high-water, radio accuracy or deadline compliance.

Paths (relative to their repository):

- STM32: build/Universal/EPuck_STM32.elf (slot500),
  build/Hybrid50/EPuck_STM32.elf, build/Hybrid10/EPuck_STM32.elf.
- LPS: bin/hybrid500/lps-node-firmware.elf and .dfu; likewise hybrid50/hybrid10.
  These are the custom channel5 images. The default top-level bin artifact remains
  the unchanged default channel2 profile; do not confuse it with the opt-in image.
- ESP32: build/comm_with_stm32.elf and .bin; build/bootloader/bootloader.bin and
  build/partition_table/partition-table.bin. Existing provisioning is retained.

Resource comparison against the prior untethered milestone (same Release/trace
flags for STM32): Universal text/data/BSS105928/768/34680→115268/768/35248B;
text+data flash+9340B, static RAM+568B (36016B total main RAM; CCM unused).
LPS default remains123024/1072/15292B. Opt-in channel5 slot500 is123408/1072/15292B
(+384B text), slot50/10 text123368B. ESP application .bin815664→816080B at the
validated build. Static sizes are not peak stack/heap or physical timing evidence.

Current source checkpoint hashes are in each repository's Git history and LPS
PLAN.md. Both firmware vendor snapshots pin the same canonical core revision,
verified by ThirdParty/mul_core/verify.py; builds/replay require no sibling checkout
or fetch. No central project document, calibration flash, dsPIC firmware or physical
robot was modified by the assistant. User-supplied local mul_core.md formatting is
left untouched. Physical acceptance, receiver-rate observability during motion,
additional independent localization geometry and publication-grade accuracy remain
open; this software checkpoint does not close prior hardware gates.

## Reception investigation: targeted evidence gate (2026-10-22)

The new user-reported channel5/slot500 run demonstrates DW1000→DWM3000 reception,
association and successful inter-ALR TWR (~0.53–0.60m reported, no surveyed truth).
It does not establish a complete passive exchange or valid transported relation:
`rx=187 reject=65 pairs=58 twr_inter_alr=0 twr_reject=56 phy=33`, with every
reported observation `status=0 clock=0 rate=0`. UNKNOWN receiver rate is expected
in hybrid, but cannot explain clock=0: a valid model would yield clock=1/NO_RATE.

Inspection: the shared PHY explicitly calls dwt_configureframefilter(0,0), so no
configured hardware destination filter deliberately excludes directed ALR packets.
The software accepts enabled ALR sources without requiring the destination to be
the RLR. The five-frame TWR audit requires a fresh reference, matching transaction,
600–800ms local reception window, POLL plus each next stage, unchanged timestamp/
power prefixes, bounded report/result intervals and a matching recomputed ToF.
Previously every failure in those checks (or capture age>15ms) incremented the
same twr_reject counter; malformed codec/address failures incremented reject.
The follower's 74-byte published relation is consumed independently. Successful
ALR TWR does not prove the passive RLR heard all five messages, nor that the next
follower publication contained a usable relation. No failing raw packet was
supplied. No exact physical root cause is established yet.

The canonical diagnostic checkpoint is LPS3bbd324b3f5329e5cdc1e50cdce0b78b51054bce.
It adds counters only: packet bytes, ALR firmware behavior, timestamp arithmetic,
validation gates, UNKNOWN/static rate policy and UWB3 layouts are unchanged. Both
consumer snapshots use this pin. Complete exchanges and the actual ALR v2 relation
publisher/codec pass rollover tests at all500/50/10ms. Missing/reordered components,
changed prefixes, wrong addresses, missing reference, late capture and invalid
relation are rejected with distinct evidence; no missing stamps are synthesized.

Read-only case-insensitive USB commands (documented lowercase):

- `hybrid status`: existing summary; last_status=0 means NO_RELATION,1 means NO_RATE,
  4 means AVAILABLE. Read current freshness separately from last observation.
- `hybrid diag`: three bounded lines: decoded POLL/RESPONSE/FINAL/REPORT/RESULT
  counts, total/available decoded relation publications, last diagnostic type/ID/
  prior stage, and reason counters plus the last accepted follower relation ages.
  `missing_twr` means no active prior state or an unexpected stage: it includes
  loss/reordering/duplicates, and does not alone prove RF packet loss. `decode`
  includes invalid packet type/version/metadata. missing_relation counts unavailable
  publications on associated pairs; stale_relation counts failed binding/age/
  conversion of a publication marked available. Counters may overlap; they are
  not independent loss denominators. Decoded frame counts include duplicates.
- `hybrid frames`: exactly three bounded on-demand records, slots0/1/2 for the
  last physically captured34/74/84-byte frame. Each includes RX40, hardware
  capture-to-service age, coarse observed_ms, length and complete uppercase hex.
  Each slot is independent, not necessarily one coherent exchange; observed_ms
  and wire sequences identify this. Even codec-rejected shaped frames are retained.
  Hardware CRC/error events do not replace these snapshots. No continuous trace,
  dynamic allocation, automatic extra serial output or radio TX is introduced.
- `diag reset`: existing idle-only counter reset. Protocol/rate/sequence state,
  settings and frame snapshots remain unchanged. last model/freshness are not
  diagnostic counters and may predate a reset.

**Smallest next capture, before checkpoint B/C/D:** leave both ALRs on their
currently working channel5/500ms images. Only the STM32 diagnostic image needs
programming; do not mass-erase configuration sectors10/11. Use
`build/Universal/EPuck_STM32.elf` with the build command at the top of this guide.
Keep node0x8D, both ALR peers and primary/calibration settings unchanged. Confirm
`cfg show`, then `cfg set wifi off`, `cfg set verbosity quiet`,
`cfg set mode hybrid`. Wait10s for normal acquisition. Capture the complete USB
response to `hybrid status`, `hybrid diag`, `hybrid frames`, then repeat that
three-command group every5s for30s (one command at a time, let output drain).
Do not reset counters between groups. Include matching30s serial logs from both
ALRs with their boot/PHY/slot banners and TX/CLOCK/SYNC/TWR lines. Ground-truth
separation is welcome but unnecessary to diagnose reconstruction. No movement,
antenna tuning, hybrid_static or Wi-Fi capture is needed for this first test.

Expected **if healthy**, not claimed observed: both source freshness flags1,
growing relation available count and hybrid status last_status=1 (NO_RATE),
and twr_inter_alr growing only when all five valid directed packets were heard.
If verbosity is temporarily enabled, associated MULH OBS should show clock=1/
status=1/rate=0. Quiet mode deliberately emits no automatic OBS lines.
TDoA/range_difference remain INT32_MIN/NA in ordinary hybrid. A missing TWR audit
must not by itself invalidate an otherwise fresh valid publication. If available
remains0, the raw follower publication shows whether the ALR sent unavailable
state or decoding rejected it. If available grows but clock stays0, the model
reason counters and raw paired sequences/ages locate the failing binding. Unequal
TWR type counts and missing_twr identify incomplete listener history; prefix,
timestamp/arithmetic or capture_age identify different validation failures.

Software checks for this checkpoint: portable sanitizer suites/all three slots;
195 Unity and178 MUL sanitizer tests per slot; default LPS build; STM32 runtime/
persistence/USB/ADS/beacon/hybrid/no-TX and bounded diagnostics tests; Universal
build and reserved-sector ELF check; ESP transport/41 Python tests and ESP-IDF
build; both vendor manifests verified. Hardware failure is **not fixed or accepted**.
Checkpoint A investigation awaits this capture. B signed-geometry expansion,
C independent persistent role/address and D command unification have not begun.
The existing original configuration commands remain authoritative meanwhile.

## Capture-age correction after the physical diagnostic run

This supersedes the earlier evidence gate. User-supplied counters identify245/245
TWR audit rejects as capture_age, with all shape/address/decoder/stage/math counters
zero,242 available relation publications and208 failed relation checks. All retained
frame ages are65535. These are user hardware observations, not assistant tests.

Root cause: bundled driver deca_device_api.h documents dwt_readsystime as FOUR
bytes; dw3000_device.c ull_readsystime reads SYS_TIME_LEN=4, containing bits39:8.
The adapter mistakenly allocated hardware[5] and mulDecodeTimestamp treated it as
a full40-bit timestamp, leaving byte4 uninitialized and using the wrong scale.
The original host mock also incorrectly wrote five full-resolution bytes, hiding
the defect. Changing only that mock to the real four-byte contract reproduces the
NO_RELATION failure before the production correction.

Core/Src/mul_hybrid_radio.c now reconstructs bits39:8 into the local40-bit domain.
The unavailable low8 bits are set to255, the upper edge of the quantization bin,
so the subsequent millisecond ceiling remains conservative (<=255DTU resolution
allowance, not an empirical timing correction). Native RX40 is unchanged; signed
modular subtraction handles rollover before checking the original0..15ms gate.
65535 is the invalid-age sentinel, not an actual65.535s retention time. The same
value enters both the diagnostic frame output and protocol checks: it is not just
a display fault. Genuine negative/late captures still reject; no timeout increased.

The500ms follower slot separates two RF receptions. Capture age measures each
packet's own RX-to-service latency, not that separation. TWR occupies650..800ms;
its POLL→RESULT progression is~80ms, below the separate200ms observer timeout.
No queue or USB wait is involved before age capture/rearm. nowMs is local HAL uptime
used with unsigned subtraction for coarse age/expiry, while system/RX subtraction
uses the same native DW3000 clock. Epochs and ALR clocks are not substituted.

stale_relation is independently incremented when an associated publication marked
available fails mulDownlinkRelation. That function first rejects captureAgeMs>15,
then checks source identity, receiver domain, exact reference sequence/TX origin,
source freshness, remaining horizon, inverse mapping and configured slot range.
The relation's available bit is the follower's transmitter-side statement, not an
override of local capture validation. Hence available=1/remaining1120 can coexist
with65535-driven failure. No independent clock-freshness change is justified: the
single adapter fix repairs the shared input to two independent validation paths.
Missing passive TWR components do not invalidate a valid published relation.

The three pasted raw snapshots were decoded with the actual C codecs and are
retained as explicit regression vectors in tests/runtime_config/hybrid_vectors.h:

|Snapshot|Actual decoded meaning|
|---|---|
|slot0, RX B8D2B3515F,324487ms|REF0x84 seq2911, TX33CF7F08A2|
|slot1, RX B1626242AD,323987ms|FOLLOWER0x85 own seq2894, TX F8F1D590A2, reference2910, TWR2905, epoch1|
|slot1 relation|v2,1989ppb, reference origin24EEE708A2, follower origin F181898F88, origin age502ms, propagation age4879ms,126DTU, reused, max6000ms, remaining1120ms|
|slot2, RX B4CE5F1003,324217ms|RESULT0x84→0x85 id2910, TX2FCB2CFCA2, encoded corrected ToF132DTU|

The RESULT carries P1..P6:2E9A7D08A2,FB2D20D31F,FB794CD4A2,2EE6A90136,
2F32D502A2,FBC578E10C. Recomputed raw114DTU/corrected132DTU matches its encoded
132. Its DW1000 RX-owned power deltas are18/17/18DTU (table2, applied); no DWM3000
power correction is introduced. These computed values are not surveyed accuracy.
The follower inverse maps TX to reference2C5F321182,499.999009916ms after reference
2910. The next reference TX is exactly1s after its carried origin. The newest REF
RX is500.005187ms after follower RX; RESULT is270.002313ms before that newer REF,
consistent with the prior superframe's~730ms RESULT. Snapshot slots are independent:
reference2910's RLR RX and earlier TWR components are absent, so these three frames
alone cannot establish geometric TDoA or a complete passive exchange. Increasing
five aggregate type counters does not prove one transaction was received intact.
No additional logs are indispensable to identify/fix this driver-contract bug.

Regression coverage includes the corrected four-byte mock, actual hardware vectors,
500ms pairing and reported4879/1120ms model, valid/expired/wrong-sequence bindings,
complete/lost-stage TWR across native40-bit and HAL32-bit rollover, and genuine
20ms RX service rejection. The runtime harness now uses the same6000ms propagation
policy as the Universal CMake build rather than silently testing the v1 default.
Physical validation of the corrected image remains pending. Ordinary hybrid must
be NO_RATE once the relation is usable; hybrid_static remains stationary-only.

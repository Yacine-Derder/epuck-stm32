# Untethered multi-robot UWB operation

This is software ready for user-operated hardware tests, not a claim of physical
coexistence, synchronization or localization validation. The user has verified
homogeneous DWM3000 ADS ranging and pair calibration with earlier firmware.
This milestone preserves that equation/56-byte four-message protocol and adds
optional Wi-Fi telemetry, eight peer states and experimental beacon acquisition.
No motor movement, flashing or physical experiment was performed during development.

## Build and provision

Use the Universal build command in [runtime_uwb.md](runtime_uwb.md). Program:
`/home/derder/Documents/epuck-stm32/build/Universal/EPuck_STM32.elf` through
STM32CubeProgrammer, preserving configuration sectors10/11. Do not mass-erase a
calibrated board. Back up its settings and previous ELF before programming.
The same image handles all runtime modes and roles. `CFG SHOW` must retain the
previous antenna delays, correction and calibration; v1 records migrate in RAM.
Schema2 writes use the existing 128-byte CRC journal; old firmware cannot read
new records. No configuration write occurs just because the image boots.

The universal image keeps startup STOP, motor disarm, watchdogs and emergency
STOP. It refuses ARM, nonzero movement and sensor streaming. Normal motor-enabled
firmware remains a separate build. Configuration still needs the existing safety
predicate: wait 2s; if a controller is attached, obtain STOP and STREAM0 ACKs.
An absent silent controller retains the prior standalone grace behavior.

ESP32 build, in epuck-esp32 with its existing Wi-Fi provisioning:

```sh
source /home/derder/.espressif/tools/activate_idf_v6.1.sh
/home/derder/.espressif/tools/python/v6.1/venv/bin/python3 /home/derder/.espressif/v6.1/esp-idf/tools/idf.py build
```

Artifacts are `build/comm_with_stm32.elf`, `build/comm_with_stm32.bin`,
`build/bootloader/bootloader.bin` and `build/partition_table/partition-table.bin`.
Use the established ESP32 programming procedure yourself; build does not flash.
No new credentials or provisioning stack is introduced. The existing bridge
listens on TCP3333, with one owning client. Close the normal Wi-Fi client before
connecting the collector. Wi-Fi connectivity does not authorize motors.

## Wi-Fi capture and diagnostics

Enter one newline-terminated USB command at a time and wait for SAVED / ACTIVE:

```text
CFG SET wifi on
CFG SET telemetry_format compact
CFG SET telemetry_ms 1000
CFG SET samples off
CFG SET telemetry on
CFG SHOW
TLM STATUS
```

Even with `mode off`, a summary is produced every telemetry interval, so the
transport can be tested without RF. `CFG SET samples on` adds ranges and TX/RX
events; paired beacon observations are produced whenever telemetry is enabled.
Do not assume every record can be delivered at high rates.

From epuck-esp32:

```sh
python3 tools/uwb_collect.py --robot 192.168.1.21:3333 --robot 192.168.1.22:3333 --output captures
```

Substitute actual IPs. Each robot gets a timestamped `captures/*-robot-N.jsonl`
file with endpoint, UTC and host monotonic receipt time. CTRL-C closes files.
The collector reconnects after failures and sends only CONTROL_HB; it never sends
ARM, movement, STOP or configuration. Reconnect/bridge/corrupt events and modulo32
record-sequence gaps remain in the log. A new radio epoch starts a fresh sequence
domain. These gaps describe missing telemetry records, not on-air PHY packet loss.
File writes are ordinary workstation I/O, not real-time operations. A stalled
collector may lose its existing safety session; the firmware fails safe.

`TLM NEXT` retrieves one buffered record through USB; `TLM STATUS` shows separate
USB/Wi-Fi depths, high-water marks and drop counts. `DIAG RESET` clears diagnostic
counters while radio work is idle, leaving transaction IDs, epochs, cached last
success, models and safety state intact. It does not save configuration. To obtain
a clean stable comparison, configure first, reset while idle, then capture; SET
restarts RF and changes its epoch. Reset neither arms motors nor clears faults.

STM32 has four USB records, four optional Wi-Fi records and one UART pending
record; ESP32 has eight optional records. A full optional queue drops data rather
than disconnecting a healthy control session. Unsent Wi-Fi data expires after
1000ms; disconnect purges it. Routine control/STOP replies have separate queue
priority. An in-flight 310-byte UART line cannot be preempted (26.9ms at115200);
a partial TCP line also must finish or the client is closed, preventing framing
splices. These finite bounds are not a measured STOP latency guarantee.

Wire occupancy/high-water/drop fields aggregate STM32 USB and Wi-Fi queues. With
no USB polling, USB drops are expected even when Wi-Fi delivery is perfect; use
TLM STATUS to separate them. `tx_dropped` is the UART sender's optional-record
loss; `UWBSTAT` bridge lines report ESP optional drops, corrupt records, UART
errors and queue metrics. Counter resets and config epochs delimit statistics.
Do not add cumulative counters across restarts or compare unmatched capture windows.
`spi_off_cycles`, `service_cycles` and `gap_cycles` are DWT CPU-cycle maxima;
`cpu_hz` converts them to time. They are not native radio DTU. Zero CPU frequency
means tracing is disabled. UART/PHY faults are observations; causation needs the
controlled comparisons below. No logging occurs in a radio ISR.

## One initiator, several responders

IDs use the complete byte namespace 0x00..0xFF. Provision unique IDs yourself.
Factory primary peer is0x01; change it first if it conflicts with the desired local
ID. Existing single-peer `CFG SET peer` remains usable; it changes table entry0.

Initiator A (0x10), after inspecting existing configuration:

```text
CFG SET mode off
CFG SET address 0x10
CFG SET peer 0x11
PEER ADD 0x12
CFG SET role initiator
CFG SET interval_ms 1000
CFG SET mode twr
PEER LIST
PEER STATS
```

Responder B: address0x11, peer0x10, role responder, mode twr.
Responder C: address0x12, peer0x10, role responder, mode twr.
Use the exact `CFG SET <field> <value>` syntax above. Robot `identity alr/rlr` is
optional metadata, independent of role. A fixed-peer DW1000 legacy adapter remains
0x84/0x86; arbitrary STM IDs do not reconfigure its roster.

```text
PEER HELP
PEER DISABLE 0x12
PEER ENABLE 0x12
PEER INTERVAL 0x12 2000
PEER REMOVE 0x12
CFG SET role responder
CFG SET mode off
```

Peer commands use the same validate/preflight/commit/verify/resume transaction as
CFG SET. Duplicates, self, out-of-byte IDs and a ninth entry are rejected without
writes. Keep at least one entry (DISABLE can disable every peer). `interval_ms`
updates all peer intervals; PEER INTERVAL changes just one. Maximum8 peers;
per-peer interval250..60000ms. LIST and STATS stream one line per main-loop pass.

Only one exchange is active. Due peers are visited cyclically, with at least
100ms between POLL starts and per-stage100ms deadlines. Actual successful ranges
need four approximately56-byte frames plus PHY overhead and turnaround; the
100ms guard is conservative host-cadence budgeting, not hardware TDMA. One lost
peer consumes one bounded attempt; there is no immediate retry loop. Overloaded
interval requests run slower, fairly. A logical exchange has at most four timed
stages (~400ms bound without main-loop starvation); RF and UART service remain
polled and nonblocking. Reset/config stalls are separate maintenance intervals.

**Use one coordinated initiator per collision domain.** Several addressed
responders can listen together and reply only to their own POLL. Local round-robin
does not prevent collisions between independent initiators. Responder states can
route configured initiators, but this is not a decentralized MAC. Transaction
counter exhaustion stops that peer until a fresh configuration/session, never
wraps and reuses IDs within an initiator epoch.

STATS and type5 telemetry expose attempts/completions/abort/timeout/reject/invalid,
last valid raw/calibrated mm, flags and last successful local uptime_ms. Timestamp0
in type5 is the last valid local RESPONSE RX marker. Until success, validity flags
are clear and mm is INT32_MIN. Responders count protocol completions but cannot
compute range from this four-message protocol; their range validity stays clear.
Each telemetry interval visits one peer cyclically; per-transaction type2 samples
are optional. Rejected packets do not finish a different peer's exchange.

Calibration is pair-specific: existing correction applies to the primary peer
only; additional peers remain uncalibrated. Changing/removing the primary disables
calibration. Changing antenna delays also disables it. Adding a peer leaves the
existing pair's calibration intact. Negative raw/corrected values remain available
and invalid; no clamping and no universal fitted correction are introduced.

## Beacon/listen acquisition

Transmitter A:

```text
CFG SET mode off
CFG SET interval_ms 1000
CFG SET samples on
CFG SET telemetry on
CFG SET mode beacon
```

Receiver B/C, each with a distinct address:

```text
CFG SET samples on
CFG SET telemetry on
CFG SET mode listen
SYNC STATUS
```

Enable Wi-Fi as above or pull `TLM NEXT`. `mode beacon` actually transmits broadcast
frames; `mode listen` actually captures CRC-good hardware RX events and maintains
up to8 discovered transmitter/radio entries. Full table rejects new senders until
a configuration restart. Entries have explicit epochs. There is no misleading
`sync` mode or position output. More than5s without samples invalidates apparent
models; choose <=4000ms beacon intervals for repeated model updates. Slower beacons
can still be observed. Duplicate/old frames and CRC errors cannot train a model.

PHY is the existing mixed channel5 profile. New beacon MAC uses MUL type0x75,
version1, fixed56 bytes before hardware CRC. It does not interoperate with legacy
Bitcraze TDoA2/3 packet parsers. Offsets:24type,25version,26radio(1DW1000/2DWM3000),
27previous-marker-valid,28epoch64LE,36current-sequence32LE,40previous-sequence32LE,
44previous-completed-TX40LE,49width40,50unit1;51..55zero. Destination MAC is allFF;
source uses the existing ID:00:00:00:00:00:CF:BC suffix. Frame validation is strict.

TX metadata is read from the *completed hardware TX*, with antenna correction
already applied by the radio. No future/predicted timestamp or arbitrary timing
correction is inserted. A failed/uncertain TX consumes a sequence but never
updates previous-TX metadata. Native hardware zero is a valid timestamp, not an
invalidity sentinel. Each radio restart gets a fresh nonzero hardware-RNG epoch.

Type3 events carry current local hardware TX/RX in stamp0; TX flag distinguishes
them. Receiver events include transmitter domain and current beacon sequence.
When the next beacon advertises a matching previous completed TX, type4 observation
pairs **that previous sequence's cached RX** with its actual remote TX: transaction
is the previous beacon sequence, stamp0=local RX, stamp1=remote TX, remote_epoch/
remote_radio identify its transmitter. It is not the latest packet's RX. Quality
bit0 means model update rejected; bit1 means apparent model valid. model_version
identifies the receiver's local fit. Per-event output requires samples ON; paired
observations require telemetry ON. Radio RX is rearmed before fitting/formatting.

`SYNC STATUS` reports apparent skew, residual DTU, sample/rejection count, gaps and
validity. The anchored model represents remote TX→local arrival. Offset includes
unknown propagation, which one-way data cannot separate from clock offset.
Rate estimates assume propagation stays sufficiently stable; motion can bias
skew. Validity after three pairs, ±1000ppm ratio envelope,200ns residual gate and
5s update horizon are explicit software quality limits, not measured accuracy.
Two-way clock information, known geometry/propagation or an independently calibrated
reference is needed to separate them. All clocks retain node/radio/epoch identity;
raw RX ticks from independent receivers must never be subtracted as global time.

Offline analysis, in epuck-esp32:

```sh
python3 tools/uwb_tdoa.py captures/*-robot-*.jsonl --output observations.jsonl
```

It exports observations, apparent clock fits and grouped transmitter epoch/sequence
records. With independent clocks it emits `missing_sync`, never invented TDoA.
`--maps common-clock.json` accepts independently established mappings to one
explicit target domain; each mapping supplies domain{node,radio,epoch}, local_anchor
and reference_anchor (10-digit hex), rate (local→reference), uncertainty_dtu,
max_delta_dtu<=319488000000, and provenance. Root keys are target_domain and
mappings[]. Use hardware-derived calibration with uncertainty; a one-way apparent
fit is not adequate. Anchors/epochs expire; stale/unknown maps give missing_sync.
Only then is `tdoa_b_minus_a_dtu`, ns and distance_difference_m exported, with a
conservative sum of mapping uncertainty bounds. No full position solver exists.
Groups are bounded at100000 (configurable); evictions are explicit and can reduce
cross-file matches in very large sessions. Clock-model cache is512 domains.

## Transport schema

ASCII line `UWB2 ` +304 uppercase hex digits + newline wraps152 bytes. CRC16
CCITT-FALSE initialFFFF covers bytes0..149; CRC is little-endian at150. V1/128-byte
records remain readable. Timestamp values are full40-bit native DTU modulo2^40
at nominal63897600000Hz (about17.2s wrap). Epoch/domain and bounded differences,
not host receipt time, resolve intervals. Integers are little-endian:

| Offset | Content |
|---|---|
|0..11|MT magic, version2, type, node, peer, radio, mode, role, flags, remote_radio, quality|
|12..39|record sequence32, local epoch64, exchange session64, transaction32, local uptime32|
|40..47|raw/calibrated signed mm32|
|48..67|attempts, completions, aborts, rejects, invalid32|
|68..95|depth/high16, drops/UARTerrors/SPIcycles/servicecycles/gapcycles/CPUHz32|
|96..125|six40-bit timestamps (ADS t1..t6; other types described above)|
|126..149|radio errors/timeouts/UART drops/model version32, remote epoch64|
|150..151|CRC16|

Flags:1raw-valid,2calibration-enabled,4calibrated-valid,8timestamp-valid,16TX-event.
Types:1summary,2range,3event,4paired observation,5peer. Unknown ADS remote radio/
epoch remain0 because the unchanged ADS wire format does not supply them; do not
infer a remote clock identity from its numeric stamps. Range transport rounds to
mm, with overflow explicitly invalid; raw six stamps retain native precision.

## Ordered physical validation campaign

Keep wheels isolated; never arm or move during these RF/transport tests. Save
full startup-to-end serial captures on every participant and collector JSONL.
Record firmware SHAs, SHOW/LIST, antenna settings, physical placement, start/end
windows and configuration pauses. Baseline and telemetry runs need comparable
matched windows; counters before capture are not packet-loss measurements.

| Test | Commands/action after user programming | Required result and logs |
|---|---|---|
|1 Two-board regression|Use A0x10/B0x11 opposite roles, one enabled peer, `wifi off`, `telemetry off`, `mode twr`; retain existing calibration or explicitly record calibration off|At least600 attempts/10min; six stamps, successful ranges, no transaction mismatch; capture A/B serial and raw/cal validity. Compare earlier working pair, do not impose invented accuracy threshold.|
|2 Persistence|`CFG SHOW`, `PEER LIST`, power-cycle, repeat|Same settings, boot-loaded YES, no calibration reset; retain before/after serial.|
|3 Transport without RF|`mode off`, `wifi on`, `telemetry on`; start collector|1Hz summaries and reconnect records, zero corrupt records on healthy link; stalled/disconnected client must not grow queues beyond4/4/1/8. Save JSONL/TLM STATUS/ESP logs.|
|4 Connected idle|`mode twr`, `wifi on`, `telemetry off`; collector connected|Matched10min A/B capture, completion/abort/PHY/UART/TIME maxima vs test1; no persistent UWB outage or unsafe state.|
|5 Active forwarding|`telemetry on`, `samples on`, `telemetry_ms 1000`; repeat|Same statistics plus transport sequence loss and separate queue drops. Samples may drop under saturation; UWB/safety must continue.|
|6 Controlled comparison|Repeat tests1/4/5 at same geometry, restart captures together; include `mode off,wifi off` UART baseline|Report matched-window attempts/completions/timeouts/errors and DWT maxima, not unmatched PDR; any UART fault must be isolated before attributing cause.|
|7 Three nodes|A adds0x12; B/C peers A; `PEER STATS`|Both enabled responders produce successful per-peer ranges; no cross association; retain all three serial logs and Wi-Fi logs.|
|8 Loss/rejoin|Power off C during capture, restore it, no A reconfiguration|B continues within bounded scheduler service; C gets timeouts then resumes, no stale transaction acceptance.|
|9 Roles|Set all radios off, choose one new initiator and reciprocal enabled peers, then mode twr|Only one active initiator; same ELF switches role, settings persist; retain SHOW/STATS and ranges.|
|10 Beacons|A beacon1000ms; B/C listen, samples+telemetry on|TX events contain hardware markers; paired RX matches prior sequence; unknown TX completion is never reported as known. Save event/OBS logs.|
|11 Offline clocks|Run uwb_tdoa.py on B/C JSONL; SYNC STATUS; reboot one receiver|Correct epoch separation/wrap, stable apparent ratios if geometry stationary; missing_sync without external maps is required. Only externally calibrated maps may produce TDoA.|
|12 Safety|Inspect MOTOR_STATUS/STREAM_STATUS/LINK_STATUS; STOP; disconnect collector/ESP link; attempt ARM only with wheels physically isolated per existing safety procedure|Universal rejects ARM, motors remain disarmed/stopped, watchdog/session-loss behavior preserved; retain ACK/errors. No autonomous motion test is needed.|

Fail a campaign on unsafe motor state, unbounded memory, wrong-peer completion,
CRC-corrupt accepted telemetry or invented global timestamps. RF delivery and
accuracy/coexistence tolerances need measured baselines and application targets;
software tests cannot supply them. Remaining work includes actual timing/stack
high-water, flash power-cut tests, physical multi-peer fairness, interference,
motion/propagation observability, common-reference calibration and localization.
Recommended first test: existing two-board ADS regression with Wi-Fi OFF, preserving
its proven calibration and comparing the exact same stationary geometry.

## Software validation and resource envelope

Automated validation uses real portable codecs, radio adapters and persistence /
transport code with mocked hardware, ASan/UBSan, deterministic loss/rejoin and
cross-peer fault injection. The120000-step multi-peer campaign also enables
telemetry with a disconnected sink, saturates queues without starving ranging,
and covers native timestamp and host32-bit tick wrap. Beacon tests cover actual
completed-TX metadata, lost completion, epoch replacement, stale-model expiry,
drift, matching prior RX and CRC/error precedence. C-producer→Python-parser→offline
analysis is tested separately from socket fragmentation/reconnect tests.

Passing checks: portable core sanitizer tests; STM32 runtime/config/flash/console/
Wi-Fi/multi-peer/beacon suite; existing ADS/PHY/MUL/safety/ESP/UART suites and nine
coexistence simulations; ESP32 portable transport tests plus31 Python tests; LPS
195 Unity tests,178 MUL sanitizer tests, actual DW1000 ADS adapter tests and20
Python ADS analysis tests. Universal, legacy ADS initiator/responder, normal and
UWB-off-safe STM32 profiles and ESP32-S3 ESP-IDF6.1 build successfully. All five
STM32 ELFs pass reserved-sector load checks. Clean LPS default and both ADS roles
build successfully; default artifact is restored. Existing vendor unused-function,
newlib/syscall and legacy motor-message truncation warnings remain visible.

Same-toolchain baseline comparisons (STM32 Release Universal with timing enabled;
ESP32 uses identical sdkconfig) distinguish static allocation from runtime peaks:

| Image | Baseline | Current | Change |
|---|---:|---:|---:|
|STM32 Flash text+data|93240B|106696B|+13456B|
|STM32 static RAM data+BSS|18244B|35448B|+17204B|
|ESP32 app .bin|813840B|815664B|+1824B|
|ESP32 DIRAM used (IDF size)|111466B|114122B|+2656B|
|LPS default text/data/BSS|123024/1072/15292B|123024/1072/15292B|unchanged|

STM32 current text/data/BSS=105928/768/34680B, below the768KiB application /
128KiB main-RAM limits;64KiB CCM remains unused. ESP app fits its1MiB partition;
IDF reports227638B remaining DIRAM. ESP's separate16KiB IRAM section is unchanged;
GNU aggregate BSS includes reservations and is not a runtime free-heap metric.
LPS ADS initiator/responder text=127936/127216B, data1080B, BSS15292B.
Runtime stack/heap high-water and interrupt/service timing still require physical
measurements. Resource baselines are the original e39deff STM32 and4337573 ESP32
commits, rebuilt in temporary worktrees with the same flags; no physical flashing.

The new beacon radio owner is STM32-only. Canonical codecs support radio1/2 and
are covered by host compatibility fixtures; a new DW1000 beacon-mode adapter is
not implemented. Existing mixed ADS and Bitcraze TDoA modes remain the DW1000 path.


For a passive DWM3000 RLR in the original custom DW1000 TDMA network, use
[hybrid_uwb.md](hybrid_uwb.md). That mode reuses the existing protocol and needs
matching custom channel5 ALR builds. UWB3 carries raw legacy frames; the existing
UWB2 layouts and experimental0x75 modes remain supported. Hybrid replay/localization
and scoped Universal UART/SPI hardening are separate from the original standalone
ADS/beacon workflow described above. The newer guide records current resources
and preserves historical UART evidence; no hardware resolution is claimed.

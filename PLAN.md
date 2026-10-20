# MUL STM32 development

## Verified baseline

Stage 3F's bidirectional channel5 packet gate is accepted from user hardware
logs. Counts, capture limits and UART faults are in docs/phy_interop.md. This
does not establish ranging/timestamp compatibility or combined safety timing.
Project-wide source is ../MUL UWB FIRMWARE PROJECT.md, read-only for this task;
its Stage 3F-not-started status is stale and needs an owner update.

## Stage 3G

**Software/build validation complete; hardware validation PENDING. Do not begin Stage 3H.**
CP0/CP1: small canonical LPS portable/mul_core with explicit committed vendoring,
not a new remote/submodule. Consumer configure/build rejects file-hash drift.
No sibling/unpublished source is required. Existing user files remain unchanged.
CP2: legacy codec/identity/sequence/40-bit helpers extracted without semantic
changes; new44-byte type0x70/version1 diagnostic and caller-owned metadata API.
No clock/TWR/correction/localization rewrite or packet change to Stage 3E.
CP3/CP4: narrow adapters on both radios, shared Stage 3F profile; default paths
preserved. Raw40-bit RX, actual TX completion and previous-TX packet metadata
have explicit clock identity/units. At-most-once-per-ms task-level polling;
no new ISR queue, heap, delay or safety logic changes.

Compatible published implementation: LPS/canonical core
1b807c5c7507d67d802ee976ae4dfe5b27fd88e9; STM32
1bd3ecdba534549b4c9ac89f17fd8bd7270fe056. UPSTREAM.json pins the former.
Final docs commits retain the dependency. docs/mul_core.md links exact API,
source ownership, resources, build and hardware procedures.

CP5: core/golden/fuzz/reset/wrap, TX/RX, six activation/init-failure/IRQ, timing
and 9 interleaved safety/sensor/ESP scenarios pass ASan/UBSan;15 console/9 ESP
regressions pass. Clean committed export independently tests/builds (6 console,
8 ESP,8 coexistence), including normal UWB-enabled/channel9. All default,
Stage 3F and Stage 3G Release builds pass. New workspace RX RAM29024 bytes,
RX+trace29048; disabled default28568 unchanged. Clean code uses352 fewer bytes
from absent user streaming changes. Stack estimates are not high-water proof.

CP6: optional DWT cycle maxima for outer SPI IRQ-off/service/start gaps; no SPI
speed/masking/priority change and no claimed UART fault fix. CP7 guide complete.
**Next exact action:** user A/B >=100 completed TX per direction with matching
previous actual TX metadata, plus concurrent RX/sensors/ESP and disabled baseline,
UART/drop/age/timing and secured zero-speed STOP/ARM/heartbeat validation.
The user streaming patch enables100ms trials; published baseline retains 1 Hz
polling. Keep that provenance explicit. Physical timing/ranging, UART causation
and full Stage 3G acceptance await hardware logs. No flash or Stage 3H work.

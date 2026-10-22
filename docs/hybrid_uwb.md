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

For the STM32 RLR, one command at a time after the existing safety grace/ACKs:

```text
CFG SET mode off
CFG SET address 0x21
CFG SET role responder
CFG SET peer 0x84
PEER ADD 0x85
CFG SET wifi on
CFG SET telemetry on
CFG SET mode hybrid
HYBRID STATUS
CFG SHOW
```

Existing saved peers may already include0x85: inspect PEER LIST rather than
resetting settings. The complete configuration is validated before a save;
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

HYBRID STATUS reports received/rejected frames, pairs, current status, observed
inter-ALR TWR completions/rejections/timeouts and radio faults. DIAG RESET clears
only diagnostics, preserving protocol ordering, model/safety state and settings.
The mode has no TX operation; multiple RLRs add no airtime. Lost relations are not
replaced from a stale cache. Missed packets can suppress pairs and TWR completion
rather than inventing measurements. Epoch/config restarts require warmup.

Checkpoint B: adapter compiles; actual adapter/safety/config/queue regressions and
shared original receiver tests pass for500/50/10ms. Checkpoint C: producer/bridge v3 and targeted UART software hardening implemented;
offline replay/localization validation and full integration are in progress. Hardware interoperability and accuracy have not been tested here.

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

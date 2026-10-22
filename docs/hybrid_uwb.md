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
shared original receiver tests pass for500/50/10ms. Checkpoint C: producer/bridge
v3 implemented; offline replay/localization validation and full integration are
in progress. Hardware interoperability and accuracy have not been tested here.

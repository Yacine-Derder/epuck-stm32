# RLR 141 combined UWB / motor experiment

> For the matching **921600 baud / DMA RX** images and current transport fixes, use [the UART upgrade guide](uart_921600.md). Earlier 115200 traffic figures below describe the previous image.

This opt-in configuration keeps the Universal RF policy (`MUL_UNIVERSAL=ON`,
`MUL_UWB_ONLY=ON`) and adds `MUL_ROBOT_EXPERIMENT=ON`. The ordinary Universal
preset still prohibits movement and forces STREAM 0. No dsPIC or ALR source,
RF scheduler, IDs, ranging plan/fingerprint EC2B4B2A, PHY, antenna calibration,
clock estimator, UWB message format or persistent radio data is changed.
No EKF or additional AR operations are introduced.

## Build and rollback artifacts

```bash
cd /home/derder/Documents/epuck-stm32
cmake --preset RobotExperiment
cmake --build --preset RobotExperiment -j6
# build/RobotExperiment/EPuck_STM32.elf
```

The initial rate is 10 Hz. Both acquisition and publication are configured:

```bash
cmake --preset RobotExperiment -DMUL_ROBOT_TELEMETRY_HZ=5  # also 1 or 10
cmake --build --preset RobotExperiment -j6
```

For a 10 Hz build, `STREAM 1000`, `STREAM 200`, `STREAM 100` select 1/5/10 Hz
producer acquisition without changing flash. The publication ceiling stays at the
build rate. `STREAM_STATUS` reports the actual acknowledged acquisition period.
The existing STREAM/PONG fence, retries and stale recovery remain. Only a newly
accepted dsPIC DATA record advances DATA2; publication never interpolates counts
or republishes a cached sample. Sequence gaps can reflect coalescing, UART loss,
or transport loss and cannot by themselves identify where loss occurred.

RF configuration persistence retains the existing CRC/plan/preflight checks,
startup grace and confirmed STOP/STREAM 0 gate. In the experiment, stop motors,
wait for actual `OK STOP`, request `STREAM 0` and wait for confirmed stream stop
before saving settings. Restore `STREAM 100` afterward. The experimental flag
separates motor/sensor permission from the radio-integrity policy; it does not
remove that policy. Existing saved startup RF/Wi-Fi/telemetry settings are loaded
unchanged: use the already working RLR 141 configuration. If Wi-Fi telemetry was
disabled, enable it using the existing USB `wifi on` / `telemetry on` workflow,
with the same persistence gate. Do not reinitialize the ranging plan.

A copy of the pre-existing Universal ELF was preserved before rebuilding:
`build/rollback-before-robot-experiment/EPuck_STM32.elf`, SHA256
`0c299072cdab595074082ee693c2674eb8742e9fab1274a1ab442f430ebfb9a3`.
Its original build provenance was not independently established; this is the
existing local binary. The standard preset remains available:
`cmake --preset Universal && cmake --build --preset Universal`.
For an exact source rollback use the pre-change commit `d0a5ab9` in a separate
checkout and build its Universal preset. Keep recordings and persistent settings;
flashing these images does not deliberately erase the configuration sectors.

## External commands and internal dsPIC compatibility

Send ASCII newline-delimited commands over the existing single TCP connection:

```text
M 100 100
M -100 100
MOTOR_STATUS
STOP
```

Limits remain integer -1000..1000 commanded steps/s. A fresh M in the stopped
state queues its speeds, sends internal ARM (which arms at zero), waits for
`OK ARM`, then sends M. Updates use M directly. STM32 sends HB every 100 ms
while internally armed; M also refreshes the unchanged dsPIC watchdog. One
motor command awaits its matching ACK at a time. The 150 ms ACK timeout and
reserved STOP priority remain. STOP cancels queued motion/ARM/HB, takes the next
available UART slot, and disarms on actual `OK STOP`. It cannot recall bytes
already transmitting. Matched M ACKs include `left=` and `right=`. Queued text is
never an acknowledgement. Late/unexpected replies remain diagnostic events.

Removed in this experimental configuration: external ARM, recent CONTROL_HB
requirement, the 500 ms host/ownership lease, the redundant 200 ms STM32 motor
session guard, routine HB ACK forwarding, periodic PC keepalive commands.
Retained: internal dsPIC ARM/HB, bounded UART queues, ACK sequencing/timeouts,
STOP retry, source/session envelopes, transport validation, explicit disconnect
STOP, and source attribution/arbitration between USB and Wi-Fi. The latter
prevents stealing replies from an in-flight command and has no ownership timer.
Explicit ARM is rejected in this image. No undocumented dsPIC commands are used.
The documented peer watchdog is 250 ms; it is not bypassed or reconfigured.

| Event | Behavior |
|---|---|
| Boot/STM32 reset | Requested speeds initialize to zero; startup STOP requested before RF startup. No saved/replayed motor request. Without STM32 HB, the unchanged dsPIC watchdog stops motion. |
| dsPIC reset / BOOT / ERR NOT_ARMED | BOOT or command error cancels pending motion and requests STOP; missing ACK also follows the timeout path. A fresh M after confirmed STOP is required. No automatic re-arm/replay of old motion. |
| M/ARM/HB ACK failure | After 150 ms, cancel motion/HB and request/retry STOP. Physical zero remains unconfirmed until ACK; dsPIC watchdog remains independent. |
| Normal TCP close / Wi-Fi disconnect | ESP sends CLOSE; STM32 cancels motion and requests STOP. Fresh connection does not restore motion. |
| Silent ESP failure / lost CLOSE | There is no replacement PC watchdog. If STM32 remains healthy and keeps HB flowing, motion may continue. Operator STOP/reset is needed. A silent network partition is detected only when the existing transport observes failure. |
| UART overflow/framing/queue fault | Existing diagnostics remain; uncertain input is discarded and transport invalidation/motor STOP paths remain. |

This is an experiment with an explicitly simplified interface, not an independent
emergency-stop system. All physical tests and flashing remain operator actions.

## Timing and traffic

DATA2 retains uint32 STM32 accepted sequence and sample age, uint16 dsPIC source
time, signed raw proximity/acceleration, and signed int32 textual step counts.
These are commanded/motor-phase counts, not measured encoders. Producer counter
width is unknown: retain signed values; do not assume int16/int32 wrap correction,
convert to distance, or invent intermediate counts. Source time wraps every
65536 ms; modulo differences cannot distinguish reset, wrap or a long gap.
STM32 sequence wraps modulo 2^32 and can restart at reset. Capture ID, TCP session,
connection generation, raw BOOT/transport messages and PC reception times must
accompany the observations. No synchronization to UWB time is assumed. Age is
STM32 receipt-to-format time and excludes later UART/Wi-Fi/PC delays.

At 115200 baud 8N1 each direction carries at most 11520 bytes/s. Maximum DATA
source line is 168 bytes; maximum DATA2 line is 135 bytes. At 10 Hz these add
1680 B/s (14.58%) to UART1 RX and 1350 B/s (11.72%) to UART3 TX. Internal HB
adds 30 B/s to UART1 TX and its ACK approximately 60 B/s to RX when armed.
These are calculated upper bounds, excluding other traffic. UWB lines remain
310 bytes each: add their actual rates and diagnostics to the UART3 budget.
No added external CONTROL_HB or forwarded routine HB ACK traffic is needed.

Host mock rate results over inclusive 0..10 s windows: 11 / 51 / 101 new DATA2
records at 1 / 5 / 10 Hz, respectively (691 / 3127 / 6174 output bytes including
initial transport messages for the fixture). Mock combined traffic delivers
10/10 offered UWB records at all three rates, no UART error/drop, including
optional FIFO pressure. This verifies software queue priority, not radio timing.
Actual TDoA availability and physical UART/Wi-Fi errors have NOT been measured.
Use the paired baseline/combined procedure in the ROS experiment guide.

## Operator-only flashing

No hardware operations were run during development. Replace the port as needed.
The updated ESP32 is recommended for explicit controller-busy diagnostics; an
already deployed DATA2/UWB3-capable ESP bridge with unchanged CMD/OPEN/CLOSE
framing can carry the simplified protocol without reflashing. Older bridges that
drop DATA2 must be updated. No dsPIC flashing is needed or permitted here.

```bash
cd /home/derder/Documents/epuck-stm32
source /home/derder/.espressif/tools/activate_idf_v6.1.sh
openocd -s "$OPENOCD_SCRIPTS" -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c 'program build/RobotExperiment/EPuck_STM32.elf verify reset exit'

cd /home/derder/Documents/epuck-esp32
source /home/derder/.espressif/tools/activate_idf_v6.1.sh
python3 "$IDF_PATH/tools/idf.py" -p /dev/ttyACM1 flash
```

If coexistence degrades, STOP, stop recording/client processes, then manually
program the preserved Universal ELF with the same OpenOCD command using
`build/rollback-before-robot-experiment/EPuck_STM32.elf`. Resume the original
read-only workflow with `--legacy-heartbeat` (ROS `legacy_heartbeat:=true`).
Verify stream0/no motion and the original TDoA/error baseline. Do not reflash ALRs
or change the plan to compensate for added traffic.

[Single-socket dataset and ROS commands](https://github.com/Yacine-Derder/mul_rlr_localization/blob/main/docs/ROBOT_EXPERIMENT.md).

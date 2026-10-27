# E-puck UART test and sensor streaming

Enable the module with `APP_EPUCK_ENABLED 1` in `Core/Inc/app_config.h`,
then rebuild and flash. Set it to `0` to disable UART activity and module
diagnostics. UWB uses its separate, unchanged `APP_UWB_ENABLED` switch.

On the existing USART1 connection (PA9 TX, PA10 RX, 115200 8N1), the module
sends PING once per second and configures automatic DATA after PONG. The
boot default is1000ms; use STREAM 0 or STREAM 50..10000 and STREAM_STATUS
from USB or Wi-Fi. Periodic R polling is disabled. See
[sensor streaming](sensor_streaming.md) for acknowledgement ordering, bounded
retries, stale recovery, status fields and client commands. Motor commands
still require explicit operation through the console below.

The peer's sensor reply contains exactly 22 decimal numeric fields:

```text
DATA time_ms prox0 ... prox7 light0 ... light7 acc_x acc_y acc_z steps_left steps_right
```

`time_ms` is unsigned 16-bit and wraps every 65536 ms. Proximity, light, and
accelerometer values are signed 16-bit **raw readings**, without calibration
or unit conversion. The signed 32-bit step counters represent **commanded
steps, not measured wheel motion**. Valid accelerometer readings require
e-puck **JE1 bridges 33–34, 35–36, and 37–38**.

LF and CRLF are supported. The parser rejects missing/extra fields,
nondecimal tokens, out-of-range values, embedded control characters, and
lines longer than 255 characters. After UART errors or ring overflow it
flushes uncertain buffered bytes and discards through a subsequent newline.
A rejected record never replaces the last accepted sensor sample.

Application code can obtain a coherent snapshot without adding another
processing call to `main.c`:

```c
#include "epuck_link.h"

epuck_sensor_data_t sample;
if (epuck_link_get_sensor_data(&sample)) {
    uint32_t age_ms = (uint32_t)(HAL_GetTick() - sample.received_at_ms);
    /* Use age_ms and sample.accepted_records to detect stale data. */
}
```

The getter returns 0 before the first accepted record, for a null output
pointer, or when the feature is disabled. A non-null output is zeroed when
disabled. `valid` means a sample has been accepted; it does not automatically
expire. `received_at_ms` is the STM32 tick when the full record was validated,
not the peer timestamp or the time its first byte reached the UART. Use
unsigned subtraction for age across STM32 tick wrap. `accepted_records`
also wraps as an unsigned 32-bit counter.

USB CDC reports at most once every two seconds, deferring while USB is
unconfigured or busy. Accepted USB data is kept in persistent storage until
completion. Output consists of the existing diagnostic line followed by a
sensor line, for example:

```text
[EPUCK] tx=2 started=2 rx=180 pong=2 drops=0 errors=0 tx_fail=0 bad_lines=0 stream_tx=1 data=2 data_bad=0 last="PONG"
[SENSOR] valid=1 age_ms=150 time_ms=2000 prox=[0,1,2,3,4,5,6,7] light=[10,11,12,13,14,15,16,17] acc=[-100,0,100] steps=[0,0]
```

- `tx` and `started`: completed and started PING transmissions.
- `stream_tx`: completed STREAM transmissions, not acknowledgement confirmation.
- `rx` and `pong`: received bytes and exact valid PONG replies.
- `data` and `data_bad`: accepted and rejected identifiable DATA records;
  DATA records lost before identification cannot be counted here.
- `drops`, `errors`, `tx_fail`, `bad_lines`: intentionally discarded bytes,
  UART error events, failed transmission starts, and rejected lines. Hardware
  overruns may lose additional bytes that software cannot count precisely.
- `last`: latest valid reply. `valid`, `age_ms`, and `time_ms` describe the
  retained sample; ignore sensor values and age when `valid=0`.

The task processes at most 64 queued receive bytes and starts at most one
UART transmission per call. Reception uses a 1024-byte ring with 1023 usable
slots. Keep calling the existing task regularly so queued replies are
consumed. Missing replies alone do not prove reversed wiring.

Default debug output is quiet (`APP_DEBUG_TELEMETRY=0`): periodic sensor arrays
and routine USB HB ACKs are suppressed. The verbose examples above require
APP_DEBUG_TELEMETRY=1 in normal mode. LINK_STATUS provides concise counters;
UART/drop/parser changes still produce important fault messages. Wi-Fi ACKs,
safety heartbeats and deadlines remain active. UWB-only policy is described in
[mul_core.md](mul_core.md).

## USB motor console

Type uppercase commands with LF or CRLF line endings. Commands may be
fragmented across USB packets or combined in a packet. The USB callback only
copies bytes and promptly rearms reception; parsing and transmission happen
in `usb_console_task()` and `epuck_link_task()` in the main loop.

| Command          | Action                                                                                                                                                                                    |
| ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ARM`          | Send `ARM\n`, which stops both motors and arms the peer. Wait for `OK ARM` before sending speeds. Explicit ARM in an armed session resets pending movement and re-arms at zero speed. |
| `M 200 200`    | Request left/right speeds in **commanded steps/s**, each an integer in `-1000..1000`. These are not measured wheel speeds.                                                        |
| `M -200 200`   | Request opposite directions.                                                                                                                                                              |
| `STOP`         | Immediately cancel queued ARM, movement and heartbeat commands; send `STOP\n` at the next available UART slot. Stop and disarm.                                                         |
| `HELP`         | Show command syntax and units.                                                                                                                                                            |
| `MOTOR_STATUS` | Show local session state, requested/acknowledged speeds, acknowledgement validity, queued movement count and latest acknowledgement.                                                      |

`STREAM <ms>` and `STREAM_STATUS` are also accepted from USB and Wi-Fi,
independently of motor ownership. Valid periods are 0 or 50..10000 ms;
configuration acknowledgements and retries are described in the streaming guide.

Unknown commands, extra/missing arguments, invalid numbers, control
characters and lines longer than 63 characters are rejected without being
forwarded. There are four movement queue slots; a full queue rejects the new
command. STOP has a reserved slot and cannot be rejected due to queue fullness.
Output also uses a bounded queue and reports dropped messages when full.

After matching `OK ARM`, the scheduler sends `HB\n` every 100 ms while armed;
`M` also refreshes the peer's 250 ms watchdog. PING and streaming configuration do not
refresh it. Only one motor command waits for acknowledgement at a time.
Priority is STOP, due heartbeat, ARM/movement, then streaming configuration and PING.
No transmit buffer is reused until its active UART transfer completes.

The module never arms or starts movement at startup or on reconnect. A motor
acknowledgement timeout of 150 ms, an armed-session acknowledgement/heartbeat
gap of 200 ms, a peer `ERR` (including `ERR NOT_ARMED`), or UART receive loss
cancels movement and heartbeats, marks the session lost, and requests STOP.
Late or unexpected acknowledgements are displayed but cannot re-arm a lost
session. STOP retries if its acknowledgement is missing. Wait for `OK STOP`,
then send a **new explicit ARM** and wait for `OK ARM` before moving again.

USB deconfiguration/disconnection of a USB-owned session cancels queued movement and requests
STOP. A sticky callback flag detects a disconnect/reconnect entirely between
main-loop calls. USB input overflow discards uncertain commands and loses the
motor session, recovering after a subsequent newline. An already transmitting
command finishes before STOP; cancelled queued commands cannot follow STOP.

`requested` records the most recent accepted user speed request;
`acknowledged` changes only after a matching `OK M`, `OK ARM` or `OK STOP`.
`known=0` means acknowledged state is uncertain, not that wheel motion has
been measured. `latest_ack` includes errors and ignored late acknowledgements.
HB acknowledgements are forwarded to Wi-Fi (USB printing requires debug telemetry). With `APP_EPUCK_ENABLED=0`, the
console reports that the feature is disabled and makes no UART transfers.

Example (periodic sensor/PING diagnostics omitted):

```text
> ARM
[MOTOR] ARM queued; wait for OK ARM
[MOTOR] OK ARM
> M 200 200
[MOTOR] M 200 200 queued (commanded steps/s)
[MOTOR] OK M
[MOTOR] OK HB
> MOTOR_STATUS
[MOTOR] ARMED requested=[200,200] acknowledged=[200,200] known=1 queued=0 heartbeat=1 owner=USB (commanded steps/s)
[MOTOR] latest_ack="OK HB"
> STOP
[MOTOR] STOP queued; heartbeats disabled
[MOTOR] OK STOP
```

The main loop must run frequently enough to meet heartbeat deadlines. A long
UWB processing pause may let the peer watchdog stop/disarm the motors; the
STM32 then loses the session instead of automatically resuming movement.

Host mock tests: run `tests/epuck_console/run.sh`. They use the current
`app_config.h` and check the enabled motor lifecycle or disabled UART silence,
including fragmentation, validation, scheduling, STOP cancellation, heartbeat
deadlines, lost acknowledgements, USB disconnect and receive overflow.

Wi-Fi transport and shared USB/Wi-Fi motor ownership are documented in
[wifi_protocol.md](wifi_protocol.md). USB disconnection leaves a Wi-Fi-owned
session running; STOP remains available from either source.


## Stage F odometry provenance audit

The current DATA parser accepts fields20/21 as signed32-bit integers; that wire
width alone does not establish producer counter width or mechanical measurement.
This document identifies these counters as commanded steps. The
[GCtronic standard motor API](https://projects.gctronic.com/E-Puck/e-puck-library.pdf#page=358)
(section7.75.2) describes step getters/setters in terms of motor phase changes.
This supports treating phase/actuation counts separately from encoder-measured
wheel motion; it does not prove which producer firmware is installed here.
The producer implementation is not present in these STM32/ESP worktrees.

Consequently fields20/21 must not be relabeled as measured wheel odometry or
automatically drive the requested measured-step EKF. Raw counts remain available.
A future measured input needs explicit provenance, counter width/reset epoch,
source sample time and uncertainty/age semantics. Existing commanded motor speed
fields are also unsuitable as measured prediction inputs.

The DATA timestamp is accepted as unsigned16, so its wire coordinate wraps at
65536ms. `received_at_ms` is local completion time, not source acquisition time.
The accepted-record counter is local32-bit receive sequence, not a source epoch
or sequence. DATA2 forwards that sequence, current coarse sample age, source
16-bit timestamp and unchanged step integers. No boot identifier, physical
encoder-valid flag or declared producer step-counter rollover width is conveyed.
A modular16-bit timestamp delta is ambiguous after a sufficiently long gap or
a reset; a source epoch and gap policy are required before EKF use. No source
clock may be equated to UWB hardware time or STM32/ESP uptime implicitly.

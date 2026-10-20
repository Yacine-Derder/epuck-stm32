# Stage 3F PHY diagnostic

Stage3F packet gate accepted from user A/B hardware logs reviewed2026-10-20.
Both directions demonstrate hardware CRC-good and strict v1 payload reception.
Stage3G now reuses this profile through a shared configuration helper; see
[the portable-core integration guide](mul_core.md) for its separate diagnostic
format and pending hardware gate. The Stage3F packet and evidence are unchanged.
The canonical profile/packet specification is
[LPS Stage 3F](https://github.com/Yacine-Derder/lps-node-firmware/blob/25bbc976cdebae4bde1acdbb4a50ff8377cdd665/docs/phy_interop.md).
Local `Core/Inc/phy_diag.h` and `tests/phy_interop/vectors.json` independently
implement/check that diagnostic contract; no shared MUL core or submodule exists.

Candidate: channel5/6489.6MHz, BPRF64 via TX/RX code9, preamble128/PAC8,
IEEE4a ternary 8-symbol SFD,6.8Mb/s, standard850K PHR, STSoff/PDOAM0,
32-byte data frame plus2-byte hardware FCS. Default retained channel9 TWR code
is untouched. STS/4z/SCP or mismatched channels are not compatible profiles.
Qorvo documents channel5 compatibility; only user packet tests can validate this
particular hardware/software pair. Antenna delays/timestamp calibration are not
packet reception prerequisites or a substitute for PHY validation.

`-DPHY_INTEROP_ROLE=0/1/2` in CMake selects off/TX/RX. Default0 keeps app_config
switches, board-role selection and the original UWB path. Diagnostic1/2 explicitly
enables its radio even when APP_UWB_ENABLED=0, taking precedence over its normal
role without modifying the header or NVM. Existing e-puck/ESP/motor/streaming
configuration remains unchanged. Stop at Stage3F; no MUL or mixed-radio ranging.

Diagnostic probe/reset/SPI/configuration reuse the existing Qorvo platform. EXTI0
stays disabled; the existing main-loop uwb_link_task polls a fixed number of
status/data accesses, captures CRC-good packets, then rearms before USB logging.
No protocol busy-wait, HAL_Delay in service, direct UART writes, radio ISR logging,
new timer or automatic ARM command is introduced. A failed RX enable retries at
most once per50ms; ambiguous simultaneous good/error status rejects the frame. Existing HAL SPI blocking and
vendor startup calibration mean no hard worst-case latency guarantee is claimed;
combined UART/heartbeat/telemetry timing still requires physical validation.
USB messages use the established bounded usb_console_write queue.

TX attempts start at0 and run near1Hz with a100ms completion watchdog. RX timeout
is50000 UUS (~51.28ms), rearmed silently; timeouts are expected on an idle1Hz link.
RX length is checked as34 including FCS before reading32 bytes; hardware CRC good,
full codec validation and opposite source84 are required. Pattern-invalid frames
increment invalid, CRC/header/RS failures increment error counters. PHY ERR reports
raw status and separate crc/phr_rs counts where available. Counters are per boot;
no numeric timestamps are compared across radios or used for ranging.

Validation: `sh tests/phy_interop/run.sh` runs codec/TX/RX and all six combinations
of APP_UWB 0/1 with diagnostic0/1/2 under ASan/UBSan. It checks profile, bounded
reads, FCS length, role passivity, timeout/error rearm, failed TX, IRQ ownership,
init failure and tick/sequence wrap. Existing safety/telemetry tests are separate:
`sh tests/epuck_console/run.sh` and `sh tests/esp_link/run.sh`.
Cross-repository golden/profile checks: run `python3 tools/test/phy_interop.py`
from the sibling LPS checkout after its native Unity runners are generated.

Exact paired hardware commands, required connections, log capture, reliability
checks and restoration are maintained in the
[current canonical hardware procedure](https://github.com/Yacine-Derder/lps-node-firmware/blob/92a8759/docs/phy_interop.md#user-procedure-connections-flashing-and-capture). Do not flash ESP32/dsPIC or alter motor ownership to run this test.


## Validation/resource checkpoint

Workspace Release builds: default RAM28568/FLASH53372 bytes; diagnostic RX
RAM28992/FLASH79616 bytes, TX similarly below128KiB RAM/1MiB flash. The opt-in
service compiler frame is280 bytes (plus existing SPI/USB/HAL/library callers);
this is not measured stack high-water or a wall-time bound. Disabled feature
has no linked diagnostic state. Vendor driver unused-variable warnings remain
unchanged. Existing15 console and9 ESP regression scenarios pass; diagnostic
codec, two radio roles and six activation/failure/IRQ cases pass ASan/UBSan.
Pre-existing streaming/control files retain their original checksums and are not
part of these commits. Physical packet interoperability is now demonstrated; combined UART/watchdog
timing and comparable delivery performance remain unverified. Stage3G's frame
gate is open; the limitations and follow-up measurements below remain.

The clean committed-code export, excluding those pre-existing edits, also builds
all roles: default RAM28216 bytes; diagnostic TX/RX RAM28640 bytes. Normal legacy
APP_UWB_ENABLED=1/channel9 builds separately. The working-tree versus clean-export
352-byte RAM difference belongs to the preserved streaming work, not this feature.
The linker's minimum stack reservation remains1024 bytes; actual high-water is
not measured and the large remaining SRAM is not a proof of interrupt-stack safety.


## Hardware review and remaining limitations (2026-10-20)

The user performed the experiments. Codex reviewed all four local captures
`A-tx.log`, `A-rx.log`, `B-tx.log`, `B-rx.log` (2535 lines). The canonical
[full experiment report](https://github.com/Yacine-Derder/lps-node-firmware/blob/398807237eb02bbf0e60fa1b4bc61e5db8b2320a/docs/phy_interop.md#hardware-results-reviewed-2026-10-20)
records SHA-256 provenance, capture-block accounting, per-direction statistics,
source-based error interpretation and the acceptance decision. Raw ignored files
remain local; repeated startup prefixes are not evidence of independent boots.
Do not sum their cumulative counters or use all TX records as an RX denominator.

A (DW1000 TX → DWM3000 RX) reports85 valid packets acrossseq2–90,4 gaps,
103 aggregate errors,1634 frame-wait timeouts, CRC0, PHR/RS8. Only82 RX text
records survive; counter increases establish three unprinted valid packets.
A-tx's199-packet continuous continuation alone exceeds100 completed TX, but
much of that capture is outside RX's window. Its358 completion lines include
repeated startup prefixes. A-rx logs10+2 console messages dropped, a distinct
counter from USART1 byte loss.

B (DWM3000 TX → DW1000 RX) has69 consecutive TX/DONE records (seq66–134),
with final DONE counter135 confirming earlier completions before capture.
Main RX counters plus finalseq136 establish113 valid receptions over0–136,
24 gaps; last STAT at135 has56 errors,2611 timeouts. The often-quoted100 valid /
19 gaps at118 is intermediate. Earlier duplicated/truncated RX sections are
not additive. B's DW1000 path cannot classify its aggregate errors by subtype.
Neither direction supplies a fully matched >=100-packet fixed-geometry capture.

DWM3000 status decoding uses the installed Qorvo API/register definitions:
`05800102` has SFD-timeout, `01801302` PHR-error, `01810F02` RS/sync-loss.
Last-error snapshots are sampled, not error-event counts. Ordinary51.28ms
frame-wait timeouts are expected with1Hz TX and do not measure lost packets.
No CRC errors are recorded in A; PHY-loss causes and repeatable asymmetry are
not isolated by these captures.

A's pre-PHY sensor session initially has124 status samples with no received
UART bytes, then PONG/STREAM/DATA recover. After PHY INIT and counter reset,
USART1 records six error callbacks,27 dropped/flushed bytes and six bad DATA
lines, followed by continued valid data and stream acknowledgements. These
are demonstrated receive faults, not solely USB log omissions; error subtypes
and exact cause are unavailable. B's34 snapshots have zero UART/parser/drop
errors, stable acknowledged100ms streaming and valid ages83–92ms. Its DATA
65428→1892 transition is normal16-bit dsPIC tick wrap (+2000ms modulo65536),
not a demonstrated restart. Console status sampling is not the full10Hz stream.

The unchanged `qorvo_spi.c` masks global interrupts through blocking HAL SPI;
fast and slow settings both use1.3125MHz. A32-byte transfer body takes~195us,
exceeding a115200-8N1 UART character's~86.8us. This provides a plausible UART
overrun mechanism; causation needs UART error subtypes, IRQ-off/service traces
and a matched UWB-off baseline. Do not infer a motor safety deadline guarantee
from these logs or silently change SPI speed/driver interrupt handling.
Current stream pending/confirmed flags describe different transaction states;
pending1 with confirmed1 alone does not show a failed stream.

Stage3F's **packet** gate can close and Stage3G may begin: valid reception in
both directions and >=100 TX completions/direction are evidenced, and losses /
errors / capture limits are documented. No minimum PDR was specified. Obtain
intact overlapping fixed-geometry captures before making comparative delivery
claims, and diagnose UART latency with motors disarmed before combined control
acceptance. Exact image hashes/flags, separation/orientation and host timing
were not supplied. Mixed-radio ranging, timestamp calibration, MUL portability
and localization remain unvalidated. No firmware, build settings, ESP32/dsPIC
code or central project document is changed by this documentation review.

Review checks: full-log arithmetic and SHA-256 provenance assertions pass;
three LPS log-analysis regressions, STM32 diagnostic codec/TX/RX/six activation
sanitizer cases,15 console and9 ESP scenarios pass. Whitespace and documentation
scope checks pass; the ten pre-existing user files retain their original hashes.
No rebuild is required for these documentation-only changes; earlier build
results remain implementation-time evidence, not flashed-image provenance.

# Stage 3F PHY diagnostic

Software diagnostic only: DW1000/DWM3000 reception is UNVERIFIED on these boards.
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
new timer or automatic ARM command is introduced. Existing HAL SPI blocking and
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
checks and restoration are maintained in the canonical LPS document's hardware
procedure. Do not flash ESP32/dsPIC or alter motor ownership to run this test.

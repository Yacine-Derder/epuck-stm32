# Stage 3F continuation

Project architecture/roadmap: ../MUL UWB FIRMWARE PROJECT.md (read-only for this task).
Current scope: reversible packet interoperability only, not a MUL port or ranging.
Baseline main: 9a46df0. Existing uncommitted streaming/control files are user work;
they were preserved byte-for-byte and are excluded from Stage3F commits.

CP0/CP1: plausible ch5 profile/diagnostic v1 codec and golden vectors established.
CP3 `f7f3e9f` pushed; CP4–CP6 final hardening/docs are the commit containing this
section. LPS CP1 `25bbc97`, CP2 `3f72be3`; validated LPS handoff `92a8759` (exact immutable hardware guide); latest peer
metadata-only hashes are reported in the final handoff.
All required software work is complete; user A/B logs now satisfy the Stage3F
bidirectional packet gate (review2026-10-20). Stage3G may begin.

Validation: codec, TX/RX mocks and all six APP_UWB0/1 x role0/1/2 cases pass
ASan/UBSan, including failure readiness, passive RX, CRC/framing priority, bounded
RX-enable retry, FCS, frame length, sequence/tick wrap and IRQ ownership. Existing
15 console and9 ESP regression scenarios pass; pre-existing control/streaming
files retain original checksums and are excluded from commits. Release default,
TX/RX builds pass. A clean exported committed-code tree (excluding user edits)
also builds all roles and legacy normal UWB-enabled/channel9. Workspace RAM28568
default/28992 RX; clean export28216 default/28640 diagnostic; no diagnostic state
linked when disabled. Service compiler frames264 TX/280 RX, not device high-water
or a hard latency guarantee. No hardware flashed or central/other-repo files edited.

Hardware review: A records85 valid packets/89 receiver-span positions,4 gaps,
103 errors,1634 timeouts, CRC0/PHR-RS8. B records113/137,24 gaps; last STAT at135
reports56 aggregate errors/2611 timeouts before final RX136. Both transmitters
show >=100 completions. Capture gaps/repeated startup prefixes and unequal
windows prevent synchronized PDR or reproducible directional comparisons.
Provenance and detailed accounting: docs/phy_interop.md and its canonical report.

Next milestone: Stage3G frame-interoperability gate is open; no Stage3G code
was started here. Carry forward intact overlapping >=100-packet fixed-geometry
captures and UART latency diagnosis. A has six UART error callbacks and27
lost/flushed bytes plus console queue loss; B has none. Existing slow SPI with
global interrupt masking is a latency risk requiring measurement, not a proven
root cause. Keep motors disarmed for diagnosis; do not optimize radio/driver
behavior merely from packet-gap percentages. Mixed-radio ranging/timestamps,
calibration, localization and combined control-loop timing remain unvalidated.
The central document needs its owner's status update to packet gate accepted
with these limitations; it remains untouched. This review changes documentation
only and preserves all pre-existing user firmware/control/streaming edits.


## Stage 3G — Portable core and opt-in MUL diagnostic

CP0/CP1 audit/design: canonical source will live in LPS `portable/mul_core`.
STM32 will vendor an exact published LPS commit with file hashes, verified by
its build; explicit export/check scripts prevent independently evolving copies.
A third remote/submodule adds avoidable administration and clone dependencies.
Local offline builds use checked-in pinned source; no sibling directory required.

Extraction: move only existing pure `mul_common` codec/address/sequence/40-bit
helpers, preserving APIs/wire behavior through LPS forwarding adapters. Keep
clock estimator, correction lifecycle, relations, TWR, observations and radio
scheduling in their current implementation. Introduce a separate type/version
for diagnostic messages with full identities and explicit raw timestamp metadata;
do not reinterpret Stage3E packets. Immediate TX reports actual hardware marker
on completion and carries the previous completed marker with its sequence in
the next packet, never a guessed current TX timestamp.

Platform boundary: channel5 configuration, CRC/FCS, SPI/IRQ, raw timestamp reads,
TX/RX ownership/rearm and logging remain platform-specific. No radio vtable or
dynamic allocation. LPS overlays mode-exclusive128-byte scratch; STM32 uses
bounded task-level polling/logging and optional timing instrumentation. Existing
SPI interrupt masking remains a measured-risk investigation, not a claimed fix.

Execution: CP2 core + host/golden tests; CP3 DW adapter; CP4 STM32 adapter and
pin; CP5 regressions/build/resource matrix; CP6 coexistence instrumentation and
assessment; CP7 exact hardware instructions and compatible Git SHA handoff.
Preserve all pre-existing user edits, default/Stage3E/Stage3F operation. Do not
flash, implement mixed-radio ranging/TDoA, or begin Stage3H. Stage3G hardware
acceptance remains pending user A/B/concurrent UART experiment logs.

CP4/CP6: STM32 opt-in TX/RX adapter uses the same shared diagnostic; core pinned
at LPS1b807c5c7507d67d802ee976ae4dfe5b27fd88e9 with offline per-file build checks.
Stage3F's proven profile is factored into one common setup without changing its
settings. Main-loop radio service is limited to one status pass per millisecond,
RX rearmed before log formatting. No new radio ISR or event queue. Existing USB
16x192-byte queue exposes drops; no heap. Optional MUL_TIMING_TRACE records DWT
cycle maxima for service/start gap and outer SPI IRQ-off windows; nesting/wrap
host tests pass. IRQ masking/speed remain unchanged; no claimed UART fault fix.
New core/TX/RX/six activation tests, nine interleaved UWB+ESP/sensor/safety cases
and timing tests pass sanitizers. Final clean-tree/build/resource/hardware guide
validation remains; full hardware acceptance awaits user experiments.

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

# Stage 3F continuation

Project architecture/roadmap: ../MUL UWB FIRMWARE PROJECT.md (read-only for this task).
Current scope: reversible packet interoperability only, not a MUL port or ranging.
Baseline main: 9a46df0. Existing uncommitted streaming/control files are user work;
they were preserved byte-for-byte and are excluded from Stage3F commits.

CP0/CP1: plausible ch5 profile and diagnostic v1 codec/golden vectors established;
canonical protocol and evidence: docs/phy_interop.md and linked LPS specification.
CP2 LPS diagnostic is implemented; CP3 is the commit containing this section.
Opt-in CMake PHY_INTEROP_ROLE=1/2 enables TX/RX;0 preserves existing default app.
Codec/radio and six UWB/role activation combinations pass ASan/UBSan; default,
TX/RX Release firmware builds pass. Existing console and ESP regression suites
pass against preserved streaming/control working tree. No physical tests performed.

Next CP4–CP6: final cross-repository command/reference audit, clean committed-tree
build excluding unrelated local edits, resource review and operator hardware guide.
Stage3G is blocked on user-demonstrated valid reception in BOTH directions and
>=100 transmitted packets per direction with counters/loss/errors reported.
Do not flash hardware; no ESP32/dsPIC/central-document edits are authorized.

# Stage 3F continuation

Project architecture/roadmap: ../MUL UWB FIRMWARE PROJECT.md (read-only for this task).
Current scope: reversible packet interoperability only, not a MUL port or ranging.
Baseline main: 9a46df0. Existing uncommitted streaming/control files are user work;
they were preserved byte-for-byte and are excluded from Stage3F commits.

CP0/CP1: plausible ch5 profile/diagnostic v1 codec and golden vectors established.
CP3 `f7f3e9f` pushed; CP4–CP6 final hardening/docs are the commit containing this
section. LPS CP1 `25bbc97`, CP2 `3f72be3`; validated LPS handoff `92a8759` (exact immutable hardware guide); latest peer
metadata-only hashes are reported in the final handoff.
All required software work is complete; user bidirectional hardware gate remains.

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

Next exact action: user runs A/B/C from docs/phy_interop.md's canonical guide,
collecting >=100 TX completions/direction at fixed separation, both raw logs,
counts/gaps/CRC/framing/timeouts and any asymmetry. Stage3G remains gated on
valid bidirectional OTA reception; compilation/mocks do not satisfy it. No further
firmware changes or repeat builds are justified merely while awaiting those logs.
Normal app switch/role/NVM behavior remains intact; no MUL port, ranging or
localization is implemented here. The central document can later be updated by
its owner to Stage3F software ready / physical gate pending; it was not changed.

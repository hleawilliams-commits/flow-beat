# AZ-MODS handoff for Claude, Grok and other collaborators

This branch saves Henry's development work from October 16, 2026 (America/Denver). Start with this file and [AGENTS.md](../AGENTS.md). The source is public so it can be reviewed without the original chat or Mac. A fresh clone does not contain the device system image, music library, SSH credentials or local restore point.

## Current state

For Henry and Jackson’s practice session, Grouped Deck Panels 0.1.0 has been staged on the physical FLOWMATIC USB after a verified backup. Read-only SSH status now confirms it is loaded on the physical deck; visual/selection validation remains pending. [Start with the practice guide](PRACTICE-NIGHT.md). The older key/phrase overlays and Set Clock are disabled but retained. The official core is unchanged. Grouped Deck Panels 0.1.0, Phase Strip 0.1.2 and MODS Shortcut 0.1.1 were published to the live GitHub catalog on October 17 as practice previews, with their tested source on main. [Release and install notes](RELEASE-2026-10-17.md) explain the physical install caveat. Other changes below remain emulator experiments. The stems issue is not fixed by these UI releases. **Phase Strip 0.1.3 now supersedes 0.1.2** after Henry requested the old layout back; [current update instructions](RELEASE-PHASE-0.1.3.md) use normal Update from the remotely installed 0.1.2.

| Work | Location | Verified state / limits |
|---|---|---|
| Grouped Deck Panels 0.1.0 | [grouped-panels](../grouped-panels/) | Actual 4 DECK emulator preview: title, original Camelot key and relative phrase beside native BPM; cyan LEFT/RIGHT selection. All four decks and selection pairs checked. Replaces three older overlays in emulator. Staged on FLOWMATIC and confirmed loaded through physical-deck status; visual/selection checks remain pending. |
| B2B USB browser switch research | [research and manual evidence](../experiments/usb-browser-switch/) | Two emulator USBs independently restore selected tracks through native switching. Direct USB1/USB2 buttons are deferred to the next update; Henry requested them in the old WAVEFORM heading area. They are not implemented. An exact-firmware observation probe captured both native source descriptors in the emulator; activation is still absent and the probe was unloaded. |
| MODS Shortcut 0.1.1 | [mods-shortcut](../mods-shortcut/) | Optional clean heading removes WAVEFORM. MODS still opens SOURCE for native Mod Menu selection and confirmation. Published 0.1.1; physical tap was accepted but cancelled before SOURCE injection. [Diagnosis-only work](../experiments/mods-touch-diagnosis/README.md) identifies what remains unknown; no button fix is claimed. It intentionally hides on BROWSE. |
| FLOW Neon theme | [theme experiment](../experiments/flow-neon-theme/) | Purple panels and pink native accents; sixth native theme in a separate emulator UI core build. Physical board unchanged. |
| Selected Deck Highlight 0.1.0 | [deck-focus](../deck-focus/) | Cyan lower-panel number and top edge follow left 1/3 and right 2/4 selection. All four pairs checked in emulator. 4 DECK view only; no waveform coverage. Reads native LED state, independent of PLAY/MASTER. |
| Stem Picks 0.4.1 | [harmonic-picks](../harmonic-picks/) | Header button disabled in the emulator at Henry's request; [verified screen and saved setting](evidence/stem-picks-hidden/README.md). Source and custom suggestions retained. It only opened the current native browser; automatic matching/loading remains unfinished. |
| Tag Mix 0.1.2 | [mix-bookmark](../mix-bookmark/) | Short TAG MIX button for tagging good combinations during practice. TAG COMBO saves two loaded tracks, approximate positions, BPM and manual stem notes. Saved-file checksums and UI verified across an emulator restart; older bookmarks browsed. No automatic audio changes or recall. |
| Phase Strip 0.1.3 correction | [phase-strip](../phase-strip/) | Restores the larger 0.1.1 geometry/placement controls with 3–1–2–4 order after Henry disliked 0.1.2 on hardware. [Emulator checked](evidence/phase-classic-0.1.3/README.md); physical test pending. Marker is independent of native waveform alignment. Published in the live catalog; [release/install instructions](RELEASE-PHASE-0.1.3.md). |
| Phrase Coach layout update | [phrase-coach](../phrase-coach/) | Counters moved into lower details headers; emulator checked with all four decks playing. Manifest still says 0.1.0: this is an unpublished source change and needs a distinct version before release. |
| Wider four-deck waveforms | Native UI | Collapse the left controls and right Beat FX panel. No waveform renderer patch or increase in vertical waveform height. Native waveform order 3–1–2–4; lower panels 1,2 / 3,4. |
| Set Clock | [set-clock](../set-clock/) | Disabled in the emulator at Henry's request; source retained. |
| Stems memory-refusal recovery | [experiment](../experiments/stems-memory-retry/) | Separate upstream core patch. Reproduced a plausible cause; each of four decks recovered in controlled emulator tests after memory returned and playback paused. A physical incident now has [read-only telemetry](evidence/physical-stems-2026-10-17/README.md): one deck unavailable while three have decoded buffers. Measured stem durations exceed available memory headroom in two captures, and the upstream failed-load cache explains missing retry. This is the leading mechanism; the original refusal log is absent. The patch remains emulator-only. |
| Camelot jog artwork | [experiment](../experiments/camelot-jog/) | Static artwork for two test tracks reached emulated jog displays. Stored key only; **does not follow Key Shift**. Not a finished community mod. |
| Recovery tooling | [tools/recovery](../tools/recovery/) | Verified backup and community-only restore algorithm, with tests on temporary folders. Private USB registration and real backups remain on Henry's Mac. |
| Chop Deck / Ableton | [performance experiment](../experiments/chop-deck/) | Playable browser audio demo and separate Live Session generator; FLOW Beat Link dependency checks pass. Actual AZ MIDI/audio, timing and Flow VJ coexistence remain unverified. No physical install. |
| Original SDK starter | [experiment](../experiments/sdk-starter/) | Template learning mod, not a published product. Author/source placeholders remain. |

Existing FLOW Beat Link, Key next to BPM, Mashup Helper, Stem Badge, Set Log and Deck Meters source is retained. Existing release history and catalog are separate from this preview branch.

![Main screen with STEM PICKS hidden](evidence/stem-picks-hidden/main-screen.png)

## What to do next

1. Preserve this baseline. Check repository/branch status before changing anything. Read each mod's README and the evidence before claiming a feature works.
2. The selected Grouped panels design is now an [actual emulator preview](evidence/grouped-panels/README.md). Review its native layout and metadata limits before hardware testing. The [earlier browser mockups](designs/main-screen/README.md) remain design studies; the implementation preserves native BPM, time and overview controls instead of replacing the whole lower screen.
3. Physical testing is still pending. Before any USB change, use Henry's existing local recovery kit, verify its backup and preserve its selected restore point. Test one community mod at a time away from a live set. Keep Kyle's official updater and core intact.
4. For stems, current Deck 4 buffer absence is confirmed. Follow the measured capacity and failed-load retry findings in the physical incident note. When practice permits, a controlled unused-deck memory release and different-track/return test can distinguish capacity from decode failure. Do not silently install the experimental engine.
5. Preserve earlier versions, give changed packages distinct versions, build/validate, exercise the emulator, then make an explicit hardware-test/release decision.

Other requests still open: touch scrolling on the core Community Mods page; a live Key-Shift-aware Camelot display on physical jog screens; fully automatic one-button harmonic matching and custom result loading; further four-deck BPM/key/phrase layout refinement. Acapella filtering exists in the custom suggestion page but does not load tracks. CDJ integration was discussed; no integration patch was made. Replacing SINGLE/CONTINUE, SMART CUE or AUTO CUE has not been implemented.

Latest screen: [STEM PICKS hidden using its existing setting](evidence/stem-picks-hidden/README.md), with the [clean heading from MODS Shortcut 0.1.1](evidence/clean-heading/README.md). The WAVEFORM title area is blank and input-transparent. [Grouped panels and original MODS shortcut evidence](evidence/grouped-panels/README.md) records the original-key/relative-phrase limits and emulator rollback. The old Key next to BPM, Phrase Coach and Selected Deck Highlight packages are retained but unloaded in this preview.

Previous layout fix: [separated header shortcuts](evidence/header-layout-fix/README.md), checked in 2, 2/4 and 4 DECK views.

Latest demo evidence: [Mix Bookmark and Stem Picks](evidence/mix-demo/README.md). Both are emulator-only community builds; this turn did not change the existing experimental UI/stems cores or physical USB.

## Build and inspect

Pinned upstream URLs/commits are in [dependencies.json](dependencies.json). Clone the SDK next to this repository (`az-mods-sdk/` beside `flow-beat/`) and check out the recorded revision. Follow its official toolchain setup. From a mod folder:

```sh
python3 ../../az-mods-sdk/tools/azmod-pack build .
make test AZ_SDK=../../az-mods-sdk
```

The ARM64 build uses Docker. macOS host harness runs cover plain, ASan/UBSan and TSan but do not measure the no-I/O/no-allocation gates. Focused test commands are in each mod README. Do not mistake successful host tests for physical-device validation.

For a fresh emulator, follow the pinned [upstream emulator guide](https://github.com/Kyle-Hosman/xdj-az-emu/blob/06fc981707e69982c0554bb508e0c646c005a979/docs/guide.md). It requires a legitimate system copy from the user's own deck. Firmware, ROMs, boot images, music and databases are deliberately absent here. Henry's local emulator is already set up.

Original emulator automation recipes are in [tools/emulator-recipes](../tools/emulator-recipes/). They are development records, not general installers: read their prerequisites and paths before running. They alter emulator UI/playback and some perform memory-pressure tests or emulator library edits.

## Evidence and rollback artifacts

- [MODS Shortcut 0.1.1](../archives/mods-shortcut/0.1.1/): corrected blank-heading source/package, checks and [actual 4 DECK emulator evidence](evidence/clean-heading/README.md). Previous 0.1.0 remains available below.
- [Grouped Deck Panels 0.1.0](../archives/grouped-panels/0.1.0/) and [MODS Shortcut 0.1.0](../archives/mods-shortcut/0.1.0/): community packages, source snapshots, hashes and [actual emulator evidence](evidence/grouped-panels/README.md).

- [Selected Deck Highlight 0.1.0](../archives/deck-focus/0.1.0/): source snapshot, package, hashes, test notes.
- [Harmonic Picks 0.3.0](../archives/harmonic-picks/0.3.0/): source/package, 204 shortcut and 55 matching assertions, SDK results and emulator evidence. Earlier 0.1.0 and 0.2.0 artifacts retained; unshipped 0.2.1 prototype under experiments.
- [Phase Strip 0.1.3](../archives/phase-strip/0.1.3/), [0.1.2](../archives/phase-strip/0.1.2/) and [prior 0.1.1](../archives/phase-strip/0.1.1/).
- [Screenshots and historical reports](evidence/).
- [Stems patch/results](../experiments/stems-memory-retry/): 46 focused checks and four-deck recovery test; no core binaries included.
- [Artifact inventory](artifact-sha256.json): SHA-256 for archived files and selected evidence.

Historical reports preserve what was true at their test time, including “GitHub not changed.” This handoff now shares that work on a development branch. Archived `.entry.json` files originally contained prospective URLs. Grouped Panels 0.1.0, Phase Strip 0.1.2/0.1.3 and MODS Shortcut 0.1.1 are now published; other prospective entries still do not prove a release exists. See the October 17 release note for the exact promoted artifacts. Some archived instructions refer to the original local folder layout; use this index for the current paths.

## Collaboration

Use this branch and its commit as the shared baseline. Claude/Grok do not receive this chat automatically: give them the link to this file, or connect their GitHub integration and select this branch. If a tool only reads the default branch, provide the branch-specific URL or upload a source archive. Keep updates in commits with clear validation notes; use separate branches for independent experiments. Do not have multiple agents edit the same files concurrently without coordination.

Read [SESSION-NOTES.md](SESSION-NOTES.md) for emulator details and known constraints. Read [AGENTS.md](../AGENTS.md) for the user's recovery requirements. Never upload the private system image, SSH keys, pairing/access configuration, databases or complete USB backups.

# Phase Strip 0.1.3: classic layout restored

[Phase Strip 0.1.3 is published](https://github.com/hleawilliams-commits/flow-beat/releases/tag/phase-strip-0.1.3) in the existing FLOW Beat Link community catalog. It restores the larger 0.1.1 meter and saved Top/Band/Low placement settings, retaining **3–1–2–4** lane order. The shared marker is independent of native waveform alignment. The overlay is input-transparent.

The exact package passed ARM64 build, SDK validation, 123 focused paint scenarios, SDK harness checks, and [actual emulator review](evidence/phase-classic-0.1.3/README.md) with four decks playing. Physical testing is pending. See the [publication receipt](evidence/publication-phase-0.1.3/receipt.json) for hashes and source/main revisions. Eleven unrelated catalog entries and prior releases were preserved.

## Install

Open the existing FLOW Beat Link repository in Community Mods, refresh its list, then update **4-Deck Phase Strip** to **0.1.3**. This is a normal update from the 0.1.2 Henry installed remotely. The earlier remove/reinstall note concerned manually copied folders before that installation and is not the current instruction. Retain the previous package and verified USB backup. Reload when playback is stopped; Reload unloads tracks.

Top uses the original y46–92 placement and may overlap the first part of the waveform band. Band/Low may overlap other content. The larger meter can also overlap the separately unpublished TAG MIX emulator preview; that mod is not bundled here. See the [source README](../phase-strip/README.md) for layout limits.

## Still unresolved

MODS Shortcut 0.1.1 intentionally hides on the native browser and appears on the waveform screen. Its physical touch handler accepted a request but cancelled before the SOURCE pulse. The precise cancellation reason remains unknown; a diagnosis-only experiment is saved separately and has not been installed on the board. No MODS fix is claimed by this phase release. A later missing-button report on the actual waveform screen was separately traced to the **MODS source shortcut** toggle being OFF. Turn that row ON in SOURCE → MOD MENU to restore visibility; no Reload is required.

USB shortcuts, FLOW Neon, the stems core experiment and the Deck 2 phrase report remain separate. Publication did not modify the physical board or USB.

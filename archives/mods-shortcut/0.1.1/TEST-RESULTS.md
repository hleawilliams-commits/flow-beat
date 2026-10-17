# MODS Shortcut 0.1.1 — local preview

The WAVEFORM heading is blanked with an input-transparent surface. Its fill starts from stock black so the official SDK theme pass maps it once, matching the native ground. There is no replacement diagram or deck-selection state; MODS navigation is unchanged.

Host validation passes: 264 SOURCE-input assertions and 232 component assertions in plain and ASan/UBSan; official macOS plain/ASan/TSan harness passes; Linux plain harness passes all 11 checks including no-I/O and no-allocation. The packaged `.azmod` passes SDK validation.

The exact build is visually verified on the actual **4 DECK** emulator screen with Flow Neon. All five inside/adjacent sample pixels are RGB `(9,6,17)`. Staged and emulated-USB binaries match the archive hash, and the UI/stems core hashes are unchanged. No 2 DECK visual claim is made. Physical testing and publication remain pending. See [source-path analysis and validation notes](../../../docs/evidence/clean-heading/README.md). The input-filter limitation of the official host harness is documented there.

`build-info.json` pins source, SDK, binary and package hashes. `.entry.json` contains prospective release URLs, not a published release. Version 0.1.0 remains available alongside this preview.

# Stem Picks and Mix Bookmark emulator demo

Built 2026-10-16. No physical deck or physical USB installation.

- Mix Bookmark 0.1.0: 164 focused ASan/UBSan assertions pass. Version/checksum validation, truncated files, invalid metadata, same-deck rejection, stable identity, reopen persistence, full storage, stale temporary files and symlinks covered.
- SDK harness: 8 checks each plain, ASan/UBSan and TSan. Requires the isolated worker-directory harness fix included under the mod tests; the original harness failure is retained here. No-I/O/no-allocation are not measured on macOS.
- Stem Picks 0.4.0: 55 matching/input assertions and 204 native-shortcut assertions pass under ASan/UBSan. Unmodified SDK harness passes 8 checks in each of its three modes.
- ARM64 build and package validation succeed for both mods.
- Emulator: top STEM PICKS opens native browser. Searched and loaded actual Confetti audio onto deck 2 alongside Access Denied; both report stems available. Pair is a storage fixture, not a recommendation or listening test.
- Saved three bookmarks, including distinct tracks with nonzero positions after briefly playing/pausing. Next saved mix displayed an earlier pair. Restarted EP147 with final build; all three records retained their hashes and the latest restored in UI.
- Initial missing SDK data directory correctly refused storage. Added this package to the core's current community-mods folder because the old emulator helper only mirrors the legacy community folder. Saved records live only on the emulated USB.
- Closing the panel returns to the native waveform; shortcut stays above the waveforms.

Existing emulator UI core SHA-256 remains `f8d4ba67ccad9e3386c78dc2ca377bf3263221875d65578a1d893e9c2f3f5668`; existing experimental stems engine remains `cd95ce64bd1e4cbd4c7710aa77adc7546623e9782baf8fa9bd8d4acbe562a1d3`. This demo does not validate or promote those older core experiments.

Source/package hashes: `archives/mix-bookmark/0.1.0/build-info.json` and `archives/harmonic-picks/0.4.0/build-info.json`. Prospective release URLs in package entries are not published releases.

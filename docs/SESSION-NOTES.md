# Reproduction notes

## Recorded environment

XDJ-AZ firmware 2.00 / EP147 `ep147-9e9454e357389ece`, AZ-MODS SDK/core 1.2.3. Pin full upstream revisions from dependencies.json. Emulator source itself was unchanged. The Mac setup used Homebrew, Colima/Docker and a separate Python environment for the QEMU build. Machine-specific dependency repair details and compiled tools remain local.

Emulator SSH is localhost:2222; QMP automation localhost:4446. Use upstream azemu/az-drive helpers, one QMP client at a time. Screen screenshots are 1280×800; az-drive touch coordinates use 2560×1600. A 0.7-second deck-select press worked reliably; very short pulses could be missed. UI methods depend on the current screen.

The emulator USB has library metadata for many tracks but audio for only Access Denied, Confetti and Backchat feat. Mystic State. Missing-file errors for other results do not demonstrate a harmonic matching bug. No music or database files are in this repository.

Current emulator loaded mods at the last check: flow-beat 0.2.1, harmonic-picks 0.3.0, key-next-to-bpm 0.1.1, phase-strip 0.1.2, phrase-coach 0.1.0 local layout build, deck-focus 0.1.0. Set Clock is off. Other original mods are retained but disabled. Enabling everything together previously hit the core's global 16-surface limit; budget surfaces when redesigning.

## Geometry and selection

- Native waveform rows: 3,1,2,4. Lower information panels: 1,2 / 3,4. Do not use the waveform order to index those panels.
- PICKS: x12, y8, width156, height36.
- Phase Strip: x48, y46, width680, height34; native left playhead x396. It does not follow the optional centered playhead.
- Phrase Coach deck-details placement: x320 + column×640, y566 + row×120, 196×26.
- Deck Focus badge: x10 + column×640, y561 + row×120, 44×46. Cyan edge: x54 + column×640, same y, 574×3. Click-through surfaces, four total.
- Deck Focus reads native ErP LED callback bytes 0x0c/0x44. Low-nibble 6 means left1/right2; 9 means left3/right4. Unknown/stale state hides. All 256 byte values covered by focused tests. It does not change physical LEDs or input.

## Stems distinction

The physical USB's original engine hash was `3e0205320a0dda69a72e1de5070be82c89fb9e73ec21ce3f561c1dbb23d60ea6`.

Emulator experimental azstem.so: `cd95ce64bd1e4cbd4c7710aa77adc7546623e9782baf8fa9bd8d4acbe562a1d3`.

Emulator azmod.so before FLOW Neon: `5f444898d258cb92bd78499b7d4a0631d9f29279cc47ae5d46df7b7d0c8efc84`.

The memory retry experiment preserves the 400 MiB reserve. It waits for sufficient memory, no other load in flight, and a known playhead still for roughly two seconds. While playing it keeps native audio. It cannot fit unlimited stems into RAM, and a memory-refusal reproduction does not diagnose every missing-stems incident. The published patch and evidence are for review; engine binaries and system images remain local.

## Local-only material

Henry's original workspace contains sibling SDK/emulator clones, build outputs, the verified deck system copy, private test library, and recovery-kit backups. The shareable recovery algorithm is in tools/recovery; its registration/restore-point data stays with the original local kit. Do not reconstruct or replace that restore point from this Git repository.

## FLOW Neon theme update

The emulator now uses the separate UI-core theme build recorded in `experiments/flow-neon-theme/build-info.json`, with `themes = on` and `theme = flow-neon`. The stems engine and community binaries are unchanged. The original UI core and settings are backed up in the private local workspace. Follow the theme experiment README for scope and rollback.

## Stem Picks / Mix Bookmark demo

- Harmonic Picks keeps ID `harmonic-picks`, now 0.4.0 and displayed as Stem Picks. This reuses the native playable browser shortcut and harmonic ranking; it does not introduce audio analysis or an automatic custom-result loading bridge.
- New `mix-bookmark` 0.1.0 uses one surface: shortcut (950,46,260,34), expanding to (16,90,1248,680), plus a registered Mod Menu page. Recipes are manual notes because SDK 1.2.3 does not expose individual stem mute states.
- Saved three emulator-only test bookmarks; checked distinct tracks, positions after playback, older-record browsing and restart persistence. Private `.book` files are not in Git. Screenshots identify the tracks used to demonstrate storage; they are not an auditioned harmonic recommendation.
- Upstream emulator push helper writes `MODS/community/`; current core SDK storage looks for `MODS/community-mods/`. Copied this demo's manifest/binary/icon to the latter as well, preserving data, so data_dir resolves correctly. No core changes.
- `mix-bookmark/tests/harness-worker-directory.patch` is a narrowly scoped test-harness fix used in an isolated SDK copy. Its reason, original failure and final passes are in the demo evidence.
- Both builds are archived with source. Live catalog/release tags unchanged; the previous 0.3.0 Harmonic Picks archive remains the rollback baseline. Removing Mix Bookmark can delete its data, so back that up first.

## Header shortcut spacing fix

Stem Picks 0.4.1 and Mix Bookmark 0.1.1 now reserve separate header rectangles. Four-deck: PICKS (746,50,184,30), BOOKMARK (950,50,260,30), with 20px between them. Two-deck / 2/4-deck: compact PICKS (1064,50,100,30), MIX (1174,50,96,30), with 10px between, right of the native phase display. Rectangles update when deck_count changes, and closing Bookmark restores the current layout. Top-left WAVEFORM is visible again.

Both builds installed only in emulator. Visuals checked in all three native layouts; both touch targets checked in two- and four-deck modes. Existing SDK harnesses pass all three sanitizer modes (8 gates each, same isolated directory-worker harness correction for Bookmark). Original core and bookmark record hashes unchanged. Previous 0.4.0/0.1.0 packages retained.

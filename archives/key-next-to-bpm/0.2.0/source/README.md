# Key next to BPM

An AZ-MODS community mod for the XDJ-AZ: the loaded track's musical key as a small badge on each deck, so the key tray on the left can stay collapsed.

- Key from the stick's rekordbox library (read-only, on the mod's own thread), looked up by the loaded track's id. When a future AZ-MODS core fills `deck_info.key`, that is used directly.
- Mod Menu rows: on/off, notation (as exported, Camelot, classic), size, position (right or left of the waveform, screen top), nudge up or down.
- 2/4 DECK view with decks 3 or 4 in use: badges carry their deck number and sit in one strip at the top, never over the wrong row.
- Services only, `"firmware": ["any"]`. No hooks, controls, LEDs, audio, files or network.

Build: `azmod-pack build .` in the SDK container, or `make CC=<aarch64 gcc 7 / zig cc -target aarch64-linux-gnu.2.27> AZ_SDK_INCLUDE=<sdk>/include`.
Test: `make test AZ_SDK=<sdk>` (host harness, plain + asan + tsan).

Status: 0.2.0, run in the emulator in 4 DECK (each lane gets its own song, key and BPM; lanes stack 3-1-2-4). Not yet run on a deck.

0.2.0: Show row, default Song+key: the song, key and BPM at the start of each waveform lane, in the lane's top strip; Camelot by default.
0.1.2: correct 4 DECK lane positions; keeps running when other mods hold some of the 16 shared surfaces.

License: GPL-3.0-only.

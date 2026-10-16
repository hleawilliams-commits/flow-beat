# 4-Deck Phase Strip

An AZ-MODS community mod for the XDJ-AZ: a phase meter for all four decks. One lane per deck, each playing deck's beat ticks scrolling under one shared playhead line. Ticks lined up across lanes = decks in phase.

- Works in the 4 DECK view, which the official Phase Meter does not cover, without hooking the deck's layout.
- Mod Menu rows: on/off, show only in 4 DECK or always, position (top strip, band above the waveforms, bottom).
- The deck's grid has no bar position, so every tick is a beat.
- Services only, `"firmware": ["any"]`. No hooks, controls, LEDs, audio, files or network. Redraws at most 25 times a second while a deck plays, nothing when idle.

Build and test: as for key-next-to-bpm.

Status: 0.1.0, built and passed the SDK validator and harness. Not yet run on a deck; the strip position is chosen in the Mod Menu because the 4 DECK layout is not documented yet.

License: GPL-3.0-only.

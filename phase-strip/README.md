# 4-Deck Phase Strip

An AZ-MODS community mod for the XDJ-AZ: a phase meter for all four decks. One lane per deck, each playing deck's beat ticks scrolling under one shared playhead line. Ticks lined up across lanes = decks in phase.

- Works in the 4 DECK view, which the official Phase Meter does not cover, without hooking the deck's layout.
- Compact 680 × 34 px strip above the waveforms, in native 3–1–2–4 lane order. Leaves room at the right for Set Clock.
- Mod Menu rows: on/off and show only in 4 DECK or all deck layouts. Version 0.1.2 ignores the old position setting so a saved Band/Low placement cannot put this meter over a waveform.
- The deck's grid has no bar position, so every tick is a beat.
- Services only, `"firmware": ["any"]`. No hooks, controls, LEDs, audio, files or network. Redraws at most 25 times a second while a deck plays, nothing when idle.

Build and test: as for key-next-to-bpm.

Status: 0.1.2 emulator preview. ARM64 build and SDK validation passed; all eight measurable harness checks passed in plain, AddressSanitizer/UBSan and ThreadSanitizer runs. macOS does not measure the no-I/O/no-allocation gates. Physical-deck testing is still required. Previous source, package and emulator binary are retained with the preview under phase-strip-preview/releases/0.1.1. The phase marker matches the native left-position playhead; it does not follow a change to the native centered-playhead setting.

License: GPL-3.0-only.

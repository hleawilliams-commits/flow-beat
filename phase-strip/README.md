# 4-Deck Phase Strip

An AZ-MODS community mod for the XDJ-AZ: one beat-phase lane per deck, with beat ticks scrolling under a shared marker. Ticks aligned across lanes indicate matching beat phase. Every tick is a beat; the SDK grid does not supply a phrase or downbeat position.

Version 0.1.3 restores the larger 0.1.1 meter after the compact 0.1.2 layout was difficult to read on the physical board. It retains Henry's requested **3–1–2–4** lane order.

- Classic geometry: x212, width838, height47, with 10 px beat ticks. The shared marker is centered at x631.
- Mod Menu: on/off, `4 DK` or `All`, and the original `Top`, `Band`, `Low` position choices. The `phase-strip:position` key and its values are unchanged from 0.1.1, so a saved choice takes effect again after being ignored in 0.1.2. Missing or invalid values use Top.
- Top spans y46–92. This restores the old placement and can overlap the first 12 px of the native waveform band observed in the emulator. Band starts at y72 and Low at y752; those also can overlap native content. These are the original placements, not newly certified clear areas.
- The meter is an independent ruler. Its marker **does not automatically track** native left/center playhead settings or panel collapse; the public SDK exposes neither setting. Alignment between its colored lanes is meaningful even when its marker is separate from the waveform playhead.
- The display surface has no input callback, leaving underlying native touch targets available. No hooks, controls, LEDs, audio, files or network. `"firmware": ["any"]`; redraws at most 25 times per second while a deck plays.

Build and validate from this directory:

```sh
python3 ../../az-mods-sdk/tools/azmod-pack build .
make test AZ_SDK=../../az-mods-sdk
./tests/run-focused.sh
```

The focused tests call the actual registered renderer and tick callbacks. They check classic bounds, color/deck mapping in 3–1–2–4 order, each saved position, absent/invalid settings, idle/missing-grid decks, visibility choices and an input-transparent surface. Plain and ASan/UBSan runs pass. The SDK harness passes eight measured checks in each of plain, ASan/UBSan and TSan; no-I/O and no-allocation are unmeasured on macOS. ARM64 build and SDK validation pass. The exact binary was reviewed in the emulator with four playing decks in 3–1–2–4 order, hidden in 2 DECK by default, and visible again on return to 4 DECK. See ../docs/evidence/phase-classic-0.1.3/. Physical testing remains pending. The separate TAG MIX preview overlaps the restored ruler’s right edge; it is not bundled in this release.

Previous 0.1.1 and 0.1.2 source and packages remain in `../archives/phase-strip/` for comparison and recovery. This change does not modify official AZ-MODS or the stems engine.

License: GPL-3.0-only.

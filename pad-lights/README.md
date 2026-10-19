# Pad Lights 0.1.0

The pads as a light show when you're not using them.

**Mod Menu → Pad lights:**
- **Idle show** (default): pads glow in a slow colour wave on a side whose deck is stopped or empty. The side you're playing keeps its own hot-cue colours.
- **Beat pulse:** the playing side's pads flash on every beat, coloured by the track's key (each Camelot number has its own colour).
- **Phrase countdown:** the pads fill over 32 beats (4 per pad) from when the track started, and the last bar flashes red. Same anchor idea as Grouped Panels' phrase counter.
- **Off:** every pad back to normal.

Beat pulse and Phrase countdown replace hot-cue colours on the playing side while on; switch to Idle show or Off when you need to see your cues. The mod's page (Community Mods → Installed → Pad Lights) previews both sides' pads live.

Changes pad colours only: no controls, audio or library writes. Colours are computed on the UI thread; the LED filter only copies them.

[Evidence](../docs/evidence/pad-lights-0.1.0/README.md)

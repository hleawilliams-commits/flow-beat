# Pad Lights 0.3.0

The pads as a light show when you're not using them.

**Mod Menu → Pad lights:**
- **Idle show** (default): pads glow in a slow colour wave on a side whose deck is stopped or empty. The side you're playing keeps its own hot-cue colours.
- **Beat pulse:** the playing side's pads flash on every beat, coloured by the track's key (each Camelot number has its own colour).
- **Phrase countdown:** the pads fill over 32 beats (4 per pad) from when the track started, and the last bar flashes red. Same anchor idea as Grouped Panels' phrase counter.
- **Off:** every pad back to normal.

- **FLOW lights** (0.2.0): the pads mirror FLOW VJ's light rig (the Shocker's 8 segments), sent from the Mac's light bridge on UDP 9101.

## VJ pads (0.3.0)

**Mod Menu → VJ pads → On**, then press **HOT CUE + BEAT JUMP together** on a side: its pads become a VJ page (the four mode buttons turn violet).

| Pad | Press | With SHIFT |
|---|---|---|
| 1-8 | FLOW Look 1-8 (the set pack: visual + desk look + pixel effect) | 1 WHITE HIT · 2 STROBE (while held) · 3 FULL SHOCK · 4 TOWER BURST · 5 COLOR BOMB · 6 SHOCK SWEEP · 7 ON THE DROP on/off · 8 BLACKOUT on/off |

Press any pad-mode button on its own to give the pads back to the deck (that press works as normal). On the VJ page the deck never sees the pads, so SHIFT + pad can't delete a hot cue. The running Look's pad pulses on the beat; SHIFT shows the hit colours; a held pad is white.

Presses go to FLOW over UDP 9100 the same way as FLOW Beat Link (broadcast; `pad-lights:host` / `pad-lights:port` in `MODS/azmods.cfg` to aim it). FLOW needs AZ BEAT on (it is whenever the deck's beat drives FLOW) and FlowVJ with `vj-pads.js` loaded.

Beat pulse and Phrase countdown replace hot-cue colours on the playing side while on; switch to Idle show or Off when you need to see your cues. The mod's page (Community Mods → Installed → Pad Lights) previews both sides' pads live.

Changes pad colours only, except on the VJ page, where the pads go to FLOW instead of the deck. No audio or library writes. Colours are computed on the UI thread; the LED filter only copies them.

[Evidence](../docs/evidence/pad-lights-0.1.0/README.md)

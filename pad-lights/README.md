# Pad Lights 0.5.0

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

## LIGHTS pads (0.4.0)

Same row (**Mod Menu → VJ + lights pads → On**), second combo: press **HOT CUE + BEAT LOOP together** and that side's pads play FLOW's lights desk, the same way the Maschine MK3's bank pads do (the four mode buttons turn amber). To go from one page to the other, press the other combo.

| Pad | Press | With SHIFT |
|---|---|---|
| 1-8 | PARTY · RED ROOM · HIGHWAY · SPLIT COLOR · TUNNEL · PULL WIDE · COLOR SLAM · SUNRISE | 1 BLINDER (hold) · 2 STROBE (hold) · 3 BLACKOUT (hold) · 4 DROP · 5 BUILD 8 · 6 ON THE DROP on/off · 7 FULL SHOCK · 8 COLOR BOMB |

Those 16 are FlowVJ's defaults. Change them in `deck-lights.json` next to `desk.html` on the Mac (each pad is an MK3 bank letter + pad number, like `"C5"`), then reload the lights desk. Tap a look = latch, hold = bump, as on the MK3; SAFE MODE blocks the same things.

Presses go out as `lightpad` / `lightpage` on UDP 9100 (same fields as `vjpad`). The lights desk takes them straight from the AZ feed, so it needs the lights desk open with AZ BEAT on, not the FlowVJ page.

## SAMPLES pads (0.5.0)

**Mod Menu → Sample pads → 1** (hits land on the next beat; **2** = next 1/2 beat, **4** = next 1/4 beat, **No** = off), then press **HOT CUE + SLIP LOOP together** on a side: its pads play your own sounds (the four mode buttons turn green).

| Pad | Press |
|---|---|
| 1-4 | play sound 1-4 of the current bank (the files named `kick`, `snare`, `clap`, `hihat`) |
| 5-8 | switch to Bank 1-4 (lit blue = current, dim = on the USB, dark = missing) |

The sounds are AZ-MODS's own Drum Roll banks, so they live on any USB in the same folder AZ-MODS uses:

```
DRUM ROLLS/
  Bank 1/  kick.wav  snare.wav  clap.wav  hihat.wav     <- pads 1-4 (any sound: the name only picks the pad)
  Bank 2/  ...
  Bank 3/
  Bank 4/
```

WAV (16/24-bit PCM or float) or Ogg Vorbis, mono or stereo, best at 44.1 kHz, up to 8 seconds each, with the sound starting on the first sample. The AZ-MODS desktop app writes these folders for you (Drum Roll section). Full rules: Kyle-Hosman/xdj-az-mods `docs/DRUM-ROLLS-FORMAT.md`.

How it plays:
- Each press is one hit. On a playing deck with a beat grid it lands on the next beat (or 1/2, 1/4, from the row); on a stopped or empty deck it plays at once.
- The sound mixes into that side's deck before the mixer, so the **channel fader must be up**, and that channel's EQ, filter and Beat FX shape it. An empty deck's channel works as a dedicated sample channel.
- One sound at a time per deck: a new hit cuts the previous one with a short fade. A sound longer than the gap to the next hit is cut there too.
- **MIC 2 HI** = sample volume, **MIC 2 MID** = pitch (AZ-MODS's Drum Roll controls).
- Pressing a bank pad loads that bank in the background (well under a second for short sounds). The first sound press uses `pad-lights:bank` from `azmods.cfg` (default 1).
- Don't arm Kyle's Drum Roll (SLIP LOOP + pad 8) on the same deck while using this page: they share the deck's one sampler voice.

Mods can't play audio themselves, so this page drives the Drum Roll engine inside AZ-MODS's `azstem.so` through its exported `azstem_drum_*` functions. Those are not part of the SDK and could change in a future AZ-MODS release; if they are missing the pads 1-4 glow dim red and stay silent, and nothing else is affected.

Beat pulse and Phrase countdown replace hot-cue colours on the playing side while on; switch to Idle show or Off when you need to see your cues. The mod's page (Community Mods → Installed → Pad Lights) previews both sides' pads live.

Changes pad colours only, except on the VJ, LIGHTS and SAMPLES pages, where the pads go to FLOW or to the Drum Roll sampler instead of the deck. No library writes. Colours are computed on the UI thread; the LED filter only copies them.

[Evidence](../docs/evidence/pad-lights-0.1.0/README.md)

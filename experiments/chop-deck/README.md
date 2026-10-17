# Chop Deck — Ableton performance experiment

A computer-first demo for playing short vocal fragments over a beat and blending them with a crossfader. This is a separate Ableton project and playable browser audition, not an installed XDJ-AZ community mod.

## What is prepared

- A browser audition with eight actual audio pads, keys 1–8, hold-repeat at 1/8 or 1/16, an equal-power crossfader and a short automated performance. Start Beat then tap pads; Stop All stops playback. Audio starts only after a gesture.
- A separate Live 12 Session set with one instrumental loop on track A and eight one-shot vocal clips on track B. The clips share a track and interrupt each other. Global launch quantisation is None. Playback starts stopped; tracks are unarmed.
- A native AZ crossfader mapping for MIDI channel 5, CC11 is embedded in the Live set. Physical mapping is untested. Live clip launches use their mouse triangles initially; keyboard/AZ pad mappings must be assigned in Live. Browser keyboard controls are already implemented.
- The existing FLOW Beat → Ableton Link bridge has been checked with its real dependency. See [FLOW-BEAT-LINK.md](FLOW-BEAT-LINK.md). It provides timing only; chops, MIDI controls and audio routing are separate.

The private local audition uses short extracts from Henry's existing player dave — Confetti stems at approximately 110 BPM. These are manually selected rhythmic fragments, not automatic word slicing. Audio, complete generated Live files, installed Ableton templates and music-containing browser previews are intentionally excluded from Git. The repository contains the source needed to reproduce the workflow with local audio.

## Build with local assets

Requires Python 3, ffmpeg/ffprobe, and a locally installed Live 12 Suite. The audio builder expects an existing `vocals.ogg`, `drums.ogg`, `bass.ogg`, `other.ogg` stem set and an original track with the BEATGRID metadata used by this project. Select a phrase and loop appropriate to your own track.

```sh
python3 experiments/chop-deck/tools/prepare_audio_demo.py \
  --stems /path/to/private/stems --track /path/to/private/track \
  --output /path/outside/repository/chop-assets
python3 experiments/chop-deck/tools/build_live_set.py \
  --assets /path/outside/repository/chop-assets \
  --project '/path/outside/repository/Chop Deck Project'
python3 experiments/chop-deck/tools/build_preview.py \
  /path/outside/repository/chop-assets/manifest.json \
  /path/outside/repository/chop-preview.html
```

The browser check is `tools/test_preview.cjs`; supply a rendered standalone preview and a private results directory. It requires Playwright; `CHROMIUM_EXECUTABLE` optionally selects an already installed Chromium.

The Live builder refuses to overwrite an existing project. It derives the document/clip schema from the user's installed templates and copies the nine demo WAVs into the new project's Samples/Imported directory. The preview is an inline fragment with private embedded audio; its source template is `demo.html`.

## Live controls

Open the generated set. Click START BEAT / 4 bars, then any CHOP launch triangle. Use the main crossfader to blend backing A and chops B. For keys, enter Cmd+K, select each CHOP launch triangle and assign keys 1–8, then exit Cmd+K. Turn off Computer MIDI Keyboard if it conflicts.

For hardware testing, connect the AZ by USB, choose one intended software-control deck, enable its MIDI input Remote in Live, and map the chosen pads with Cmd+M. Verify which pads and channel are arriving before assigning. Route Live audio to an intended mixer channel and test monitoring and crossfader behavior at low volume. Avoid controlling both Live's crossfader and the same audio's hardware crossfader unless that double attenuation is intentional. Hybrid standalone playback, MIDI isolation, routing and latency remain unverified.

## Validation and limits

- WAV/MP3 assets decoded successfully; no clipping. All WAV endpoints were faded. The rendered 26-second audio sketch demonstrates original phrase, rearranged fragments, then crossfade/cuts.
- Headless Chromium exercised actual Web Audio: backing and pad output, crossfader endpoints, keyboard repeat/release, Stop All, automated-demo startup, and responsive layouts down to a 288px content width. No JavaScript errors were observed. This measures signal output, not subjective musical quality or physical latency.
- Generated Live files passed gzip/XML round-trip, unique global-ID checks, all nine sample paths/hashes and WAV format checks. The first native load rejected an invalid SourceContext/Value wrapper; the generator now uses the native empty SourceContext and asserts it. Mac file references use Type 2.
- The corrected private project loaded successfully in Live 12.4.2 (End ExchangeDocument recorded). The final copy selects Session view. Playback inside Live and physical MIDI/audio operation remain unverified; the audible interaction checks were performed in the browser preview.
- The Link dependency passed nine checks with networking disabled. A passive listener received no current FLOW packets. No claim of live AZ sync or simultaneous Flow VJ reception is made.
- No physical USB, deck, official core, firmware or existing FLOW Beat configuration was changed. Nothing is published to the community mod catalog.

## References

- [Ableton sample slicing](https://www.ableton.com/en/live-manual/12/live-instrument-reference/)
- [Ableton MIDI mapping](https://www.ableton.com/en/live-manual/12/midi-and-key-remote-control/)
- [XDJ-AZ MIDI message list](https://downloads.support.alphatheta.com/software_info/all-in-one-dj-systems/XDJ-AZ/XDJ-AZ_MIDI_Message_List_E1.pdf)
- [Live XML mapping observations](https://github.com/Beennnn/ableton-als-wire/blob/main/docs/als-format-notes.md)

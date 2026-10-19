# Reel Marks 0.1.0

Mark the moments worth a clip while you play, then cut them on the Mac.

## On the deck
Press **SHIFT + TAP** (the Beat FX TAP button). A banner confirms: *Marked #3 - Confetti / Backchat*. Each mark saves the time, the minutes into the set, and every loaded deck's track, artist, position, BPM and whether it was playing, to:

`MODS/community-mods/reel-marks/data/marks-<date>.csv` on the USB.

SHIFT + TAP never reaches the deck; TAP alone still taps the Beat FX tempo. Turn it off with **Mod Menu → Reel marks**. The mod's page shows how many marks you've taken.

## On the Mac
Copy the CSV off the USB, then:

```bash
python3 reel_cut.py marks-20261024.csv IMG_4410.MOV
```

It reads when the video started from the file itself (phones record that), cuts a clip around every mark inside the video (20 s before, 10 s after; change with `--before` / `--after`) and writes `clips.md` with the time and tracks for each clip. If the phone's clock was off, use `--offset SECONDS` or `--start "2026-10-24 21:03:15"`. Several videos can go in one command. `--copy` cuts faster without re-encoding.

The deck's clock is used for marks; check it once against your phone (Moment Cards' page shows it).

## Checks
Emulator: two SHIFT + TAP presses with two decks playing wrote two marks with both decks' tracks, positions and BPMs; the banner showed. `reel_cut.py` placed both marks at the right second of a timestamped test video and cut 30 s clips. SDK harness plain/ASan/TSan; ARM64 build + validation. Not yet run on the physical deck.

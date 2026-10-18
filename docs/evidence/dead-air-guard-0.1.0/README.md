# Dead-Air Guard 0.1.0: emulator evidence (2026-10-18)

Access Denied (3:15) on Deck 1, channel fader 1 set through the mixer bus, warning time 60 s for the test:

| File | State |
|---|---|
| `dg-0-playing.png` | Playing, fader up: nothing shown |
| `dg-1-fader-down.png` | Fader pulled down while playing: DEAD AIR (red edge bars + label) |
| `dg-2-fader-up.png` | Fader back up: cleared |
| `dg-3-load-next.png` | 54 s left, nothing else loaded: LOAD NEXT 0:55 (amber) |
| `dg-4-ending.png` | 7.6 s left: ENDING 0:08 - nothing cued (red) |
| `dg-5-after-end.png` | Track ended: DEAD AIR |

Track length came from the library (the deck reports none to mods). QUIET wasn't triggered live (no near-silent passages in the test tracks); it's covered by the unit tests. Host: 21 decision checks (cued vs same-track duplicates, two audible decks, fader-down cueing, unknown lengths/faders, mic suppression, dead-air window, quiet, ending beats quiet), SDK harness plain/ASan/TSan, ARM64 build + validation.

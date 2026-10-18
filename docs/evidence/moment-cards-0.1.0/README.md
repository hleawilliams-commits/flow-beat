# Moment Cards 0.1.0: emulator evidence (2026-10-18)

A live MOMENTS.txt on the emulated stick, with moments set a minute or two ahead in Mountain time (the guest clock matched UTC):

| File | State |
|---|---|
| `card-loaded.png` | Card 55 s before "First Dance"; Confetti loaded on Deck 2 (via Crate Remote), shown as LOADED on Deck 2 |
| `badge.png` | Countdown badge after the 12 s card: "First Dance in 0:35", top centre, waveforms clear |
| `now-card.png` | NOW card at the moment (pink) |
| `after.png` | Cleared 20 s later |
| `card-final.png` | Final build: accented title (Beyoncé), typographic quotes and dash converted, NOT LOADED, "Then: Last Song" |

Found and fixed during the run: surface `draw_text` renders Latin-1, not UTF-8 (a UTF-8 "·" showed as "Â·"), so text is converted before measuring and drawing.

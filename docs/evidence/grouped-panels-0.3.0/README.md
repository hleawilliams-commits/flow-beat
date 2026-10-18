# Grouped Deck Panels 0.3.0: mix hints (emulator, 2026-10-17)

Builds on 0.2.0's 2 DECK / 2/4 DECK support. Everything sits inside the existing headers; **Mix hints** in Mod Menu turns it off.

| Hint | Rule | Seen in emulator |
|---|---|---|
| Key color | Against the other playing deck that started first. Green: same, ±1, or relative major/minor. Amber: ±2, or ±1 with a mode change. Red: anything else. | Dm (7A) vs Ebm (2A): both red (`hud-4deck-2.png`) |
| Time warning | The deck number block turns amber at ≤30 s left and red at ≤10 s, only while playing | Not reached in this run; host-tested |
| Stems dot | Filled = stems engine playing this deck; hollow = stems found, not playing | Filled dots on decks 1 and 2 (`hud-4deck.png`, `hud-4deck-2.png`) |
| BPM gap | `+0.4` style, only within 8 BPM of the reference; `±0.0` when matched | Hidden at 76.2 vs 154.8, as intended |

LEFT/RIGHT text was removed; the cyan top border and number block already show selection.

Host: 1,184 mix-hint rule checks (every Camelot pair both ways, alert thresholds, reference ordering, gap limits); 1,584 paint scenarios with every hint on at once and no overlap or drawing outside the header or into the native BPM box; layout and data suites; SDK harness plain/ASan/TSan; ARM64 build + validation.

Not yet run on the physical deck.

# Pad Lights 0.1.0: emulator evidence (2026-10-18)

Captured from the panel bus the deck program writes (`/dev/subucom_ctrl`, ERP LED frames), rendered as strips: one band per pad, left side (decks 1/3) | grey | right side (decks 2/4), time downwards.

| File | Mode | What it shows |
|---|---|---|
| `pl-idle.png` | Idle show, both decks empty | A colour wave across both sides, drifting over time |
| `pl-phrase.png` | Phrase countdown, Confetti playing on Deck 1 | Left: pads fill blue bar by bar, current pad flashing orange on the beat. Right (idle): the wave |
| `pl-beat.png` | Beat pulse, Confetti (2A) on Deck 1 | Left: all pads flash on each beat in the key's orange. Right: the wave |

Host: 19 colour/mode checks (off/idle pass-through, idle on every mode, beat brightness decay, key colours, phrase fill/last bar/wrap, beat counter), SDK harness plain/ASan/TSan, ARM64 build + validation. Not yet run on physical pads.

# Dead-Air Guard 0.1.0

Warns before the room goes silent. Thin bars at the top and bottom of the screen and one small label above the deck panels; nothing otherwise.

| Label | When |
|---|---|
| **LOAD NEXT 0:24** (amber) | Every deck you can hear ends within the warning time (30 s by default) and no other deck has a different track loaded |
| **ENDING 0:08 - nothing cued** (red, flashing) | The same, within 10 s |
| **QUIET 5s** (amber) | Every audible deck has been near-silent (below about -50 dBFS) for 4 s, e.g. a long silent outro |
| **DEAD AIR** (red, flashing) | Nothing audible for 1.5 s with the mics off; clears itself after a few seconds |

- **Audible** = playing with its channel fader up. A deck you're cueing in headphones (fader down) doesn't count, and counts as "cued".
- A **mic or TALK OVER** switch on silences DEAD AIR and QUIET, for speeches.
- Mod Menu: **Dead-air guard** on/off, **Warn before the end** (20/30/45/60 s), **Fader down = not heard**, **Quiet alarm**.
- Never draws over the Mod Menu. Read-only: it watches the mixer input and each deck's audio level and changes nothing.

Track lengths come from the USB library (the deck doesn't report them to mods); a track it can't find simply doesn't get end warnings. The crossfader isn't considered.

[Evidence](../docs/evidence/dead-air-guard-0.1.0/README.md)

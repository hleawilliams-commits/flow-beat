# Moment Cards 0.1.0

A wedding run-of-show on the deck. A minute before each moment, a big card fills the screen: the moment, a live countdown, the song and whether a deck has it loaded, the MC line, and what's next. Tap it and it shrinks to a small countdown at the top. At the moment, a short **NOW** card shows, then it's gone.

## Set it up

1. Make `MOMENTS.txt` and put it at the **top level of the USB** (next to `Contents` and `PIONEER`). One line per moment:

   ```
   19:15 | First Dance | Ed Sheeran - Perfect | Couple to the floor, everyone circle up
   7:40pm | Toasts | | Mic to the maid of honor, then the best man
   ```

   Fields are time (24-hour, or `7:40pm`) | moment | song (optional, `Artist - Title`) | MC line (optional). Lines starting with `#` are ignored. See `MOMENTS.example.txt`.
2. On the deck: **Mod Menu → Moment cards** rows:
   - **Time zone:** the venue's zone (default Mountain), with US daylight saving.
   - **Card appears:** 1, 2 or 5 minutes ahead.
   - **Show test card:** shows the next moment for 15 seconds.
3. **Community Mods → Installed → Moment Cards** shows the deck clock and the moments it read. Check the clock matches the room before guests arrive.

The file is re-read every few seconds, so a changed timeline is picked up without a reload.

## How it behaves

| When | On screen |
|---|---|
| Card time (1/2/5 min before) | Full card for 12 s, then a countdown badge at the top until the moment |
| Tap the card | Shrinks to the badge |
| Tap the badge | Back to the full card |
| At the moment | NOW card for 10 s (tap to hide) |
| Mod Menu open | Nothing drawn over it |

"LOADED on Deck N" means a deck's loaded title (and artist, if you gave one) matches the song field, ignoring capitals and anything in brackets.

## Limits

- **It uses the deck's clock.** If the deck has never been online it may be off; the mod's page shows it.
- Up to 40 moments; times are to the minute.
- Read-only: no controls, audio or library changes.

## Build and checks

```sh
python3 ../../az-mods-sdk/tools/azmod-pack build .
make test AZ_SDK=../../az-mods-sdk
clang -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror tests/test_moments.c -o /tmp/mc && /tmp/mc
```

41 checks: time formats, parsing and bad lines, US zones and daylight-saving edges, the card/badge/NOW schedule, tapping away, midnight, song matching, and Latin-1 text conversion (surface text isn't UTF-8). SDK harness plain/ASan/TSan; ARM64 build + validation. Emulator: card, badge, NOW and cleared states captured with a live moment, LOADED/NOT LOADED, accents. [Evidence](../docs/evidence/moment-cards-0.1.0/). Not yet run on the physical deck.

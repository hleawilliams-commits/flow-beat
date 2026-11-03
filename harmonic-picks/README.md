# Harmonic Picks

Test build 0.2.1: harness-tested and host-tested, not yet run on a deck. 0.2.1 takes Crate Remote 0.4.0's navigator fix: a second load in a row used to land on the wrong row.

A Mod Menu page (Mod Menu > Harmonic picks, its own row in the main list) that lists tracks
from your **whole USB library** (every track on both sticks, not just one playlist) that mix
with the deck that started playing last:

- same key (green), a neighbour key, or the relative key on the Camelot wheel
- within the BPM range row (3, 6 or 10%, default 6%), counting half and double time
- up to 40 picks, closest first; VOX marks acapellas

Turn the knob to move the cursor. **Press a pick to load it**: the mod finds the track in a
playlist it is in, then presses SOURCE (when it needs the other stick), PLAYLIST, the knob and
LOAD for you, the same way Crate Remote's iPad Load does. It loads on the other deck of the
pair (1 and 2, 3 and 4) or else any stopped deck, never on a playing deck, and touching any
control stops it. Press the top row to switch between all tracks and acapellas only.

Mod Menu rows:

- **Harmonic picks**: opens the page.
- **Press loads a pick** (on): off = the press only highlights the track in the browser.
- **BPM range**: 3%, 6% or 10%.

Limits: the browser can only be steered through the PLAYLIST tree, so a pick that is in no
playlist (or only past row 250, or more than 7 folders deep) says "search for it" instead.
The landing assumes the playlist is sorted by its # column, as Crate Remote does.

Build and test like the other mods in this repository:

    make AZ_SDK_INCLUDE=<sdk>/include
    make test AZ_SDK=<sdk>
    cc -I<sdk>/include -o t tests/test_load.c -lpthread && ./t   # fake-core load test
    cc -I<sdk>/include -o n tests/test_nav.c && ./n                  # navigator, repeat loads

`nav.h` is a copy of `crate-remote/nav.h` as of Crate Remote 0.4.0 (dedf4a0); keep the two in step. `tests/test_nav.c` is its test.

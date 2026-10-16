# Harmonic Picks

Test build 0.1.0: validated and harness-tested, not yet run on a deck.

A Mod Menu page (Mod Menu > Community Mods > Installed > Harmonic Picks) that lists tracks on
your USB sticks that mix with the deck that started playing last:

- same key (green), a neighbour key, or the relative key on the Camelot wheel
- within 6% BPM, counting half and double time
- up to 40 picks, closest first; VOX marks acapellas

Turn the knob to scroll. Press to switch between all tracks and acapellas only.

Build and test like the other mods in this repository:

    make AZ_SDK_INCLUDE=<sdk>/include
    make test AZ_SDK=<sdk>

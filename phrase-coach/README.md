# Phrase Coach

Local layout update based on 0.1.0: built, SDK-validated and harness-tested.
Verified in the emulator with all four decks playing; not yet tested on hardware.

A bar counter on each deck, so a mix lands on the start of a phrase:

- `5/8` and one cell per bar, the current bar filling beat by beat
- amber in the last bar before the next phrase, a green flash on its first beat
- `?` when the count was lost

The deck's beat grid gives the beat but not the bar, so the count starts on the beat nearest
the moment the deck first plays after a load. Start tracks from a cue on the first downbeat
and the count is right. If it is off, **Phrase starts now** in the Mod Menu sets bar 1 on the
deck you started last. Hot cues, loops and beat jumps keep the count.

Mod Menu rows: on/off, phrase length (8 or 16 bars), position (deck details or screen top),
Phrase starts now.

The local layout update puts counters in the track-details headers below the scrolling
waveforms and above the overview waveforms. In 4 DECK view these panels are ordered
1,2 / 3,4, independently of the scrolling waveform order (3,1,2,4).

Build and test like the other mods in this repository:

    make AZ_SDK_INCLUDE=<sdk>/include
    make test AZ_SDK=<sdk>

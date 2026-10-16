# Mashup Helper

An AZ-MODS community mod for the XDJ-AZ. One line for the two decks you are mixing (the two that started playing last): both keys in Camelot, then SAME or MATCH in green (same key, a neighbour, or the relative key), BOOST in amber (two steps round the wheel) or CLASH in red, and the pitch gap between the two tracks' original tempos, half and double time included.

Keys and tempos come from the stick's rekordbox library, read on the mod's own thread. Mod Menu rows: on/off, pitch gap on/off, top or bottom.

Reads deck state and the library through the AZ-MODS core only: no hooks, controls, LEDs, audio, files or network.

Test build: validated and run under the SDK harness (plain, ASan, TSan), not yet run on a deck. Services only, `"firmware": ["any"]`, no hooks. GPL-3.0-only.

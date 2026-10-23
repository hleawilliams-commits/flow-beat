# Stem Badge

An AZ-MODS community mod for the XDJ-AZ. A small badge on each deck: STEMS in green when the track has stems and the stems engine plays them, STEMS in grey when it has stems but they are off, NO STEMS in red when there are none, so you never reach for a stem pad on a track without stems.

Mod Menu rows: on/off, every deck or only tracks with no stems, and on the deck's row or at the screen top. With decks 3 or 4 in use in the 2/4 DECK view the badges carry their deck number and sit at the top.

Reads deck state through the AZ-MODS core only: no hooks, controls, LEDs, audio, files or network.

Test build: validated and run under the SDK harness (plain, ASan, TSan), not yet run on a deck. Services only, `"firmware": ["any"]`, no hooks. GPL-3.0-only.

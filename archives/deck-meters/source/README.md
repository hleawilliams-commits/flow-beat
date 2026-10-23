# Deck Meters

An AZ-MODS community mod for the XDJ-AZ. A level meter for each deck with a clip warning, and a flash in the deck color when a drop hits.

One bar per deck from the deck's own output before the mixer: green, amber near the top, and a red mark held for a second when the track hits full scale. When a playing deck's energy jumps after a quieter stretch, a band in that deck's color flashes across the top of the screen.

Mod Menu rows: meters on/off and position, drop flash on/off and its sensitivity.

Reads each deck's audio through the core's audio tap, read-only: no hooks, controls, LEDs, files or network.

Test build: validated and run under the SDK harness (plain, ASan, TSan), not yet run on a deck. Services only, `"firmware": ["any"]`, no hooks. GPL-3.0-only.

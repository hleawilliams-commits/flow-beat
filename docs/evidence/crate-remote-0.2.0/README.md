# Crate Remote 0.2.0: send a pick to the deck (emulator, 2026-10-17)

## How the deck gets there
SDK only, no hooks. The mod plays the inputs a DJ would through the mixer input filter:
1. If the pick is on a USB other than the last one it selected, or SOURCE was pressed since: SOURCE, BACK (re-open SOURCE if that closed it), USB row, Local Library.
2. PLAYLIST (pressed again if that closed the playlist screen), then BACK ×8 to the top of the tree (BACK climbs a level and does nothing at the top).
3. For each level: one frame to counter 2 (clamped at the top), one frame to 2 + row, knob push.
4. Load only: LOAD n, only if that deck isn't playing at that moment. The deck must then report the track's content id within ~8 s.

## Findings that shaped it
- The deck reads the browse knob byte as an absolute counter and moves by the plain difference between frames (no wrap). One frame can move many rows; net travel per list is ~250 rows, so targets come from playlists (first 250 rows, sorted by #), not the 10k-track list.
- A big up-turn right after a list opens is clamped at its top: this resets the counter for the next list.
- After a load the deck closes the browser on its own; the next pick waits 3.5 s.
- PLAYLIST reopens the last list visited; BACK ×8 reaches the root.
- TRACK list title order (for reference): case-insensitive, leading A/An/The ignored, letters < digits < symbols, spaces after letters.

## Results
| Test | Result |
|---|---|
| Load Backchat → Deck 2 from a fresh start (selects USB 1 itself) | Loaded, confirmed by content id, ~25 s |
| Load Confetti → Deck 3, same USB | Loaded, confirmed, ~12 s |
| Load onto Deck 2 while it plays | Refused, nothing pressed |
| BACK pressed mid-script | "Stopped: you used the controls", nothing loaded |
| Show row 42 of "01 NEW / New arrivals" right after a load | Landed on 042 Midnight City (`show-landed-row42.png`) |
| Load while the browser was deep in another playlist | Loaded, confirmed |
| iPad sheet: Show / Load 1–4, playing deck disabled | `ipad-deck-sheet.jpg` |

Host: 28 navigator checks (presses, knob values, cancel, playing-deck refusal, knob continuity, PLAYLIST toggling), 29 server checks, SDK harness plain/ASan/TSan, ARM64 build + validation. The emulator USB has audio for only 3 tracks, so loads were confirmed on those; landing on other rows was checked on screen. Not yet run on the physical deck.

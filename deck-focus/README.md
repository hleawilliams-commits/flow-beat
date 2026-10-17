# Selected Deck Highlight

Version 0.1.0 — emulator preview; not installed or tested on the physical AZ.

In the **4 DECK waveform view**, the deck currently selected for each physical side gets
a large **cyan number** and a cyan line across the top of its lower information panel.
Left selects 1 or 3; right selects 2 or 4. The unselected panels keep their stock appearance.
The native orange MASTER indicator remains separate. Selection works even on paused or
empty decks and does not depend on which tracks are playing.

The highlight follows the native deck-select LED output, including at startup. It doesn't
change controls or LEDs. No extra top labels, waveform overlays or audio changes are used.
Other waveform layouts and non-waveform screens hide the highlight. Unknown selection or
an LED stream that stops for one second also hides it rather than retaining a guessed state.

Disable **Selected deck highlight** in this mod's settings to restore the stock indicators.
This is a new independent community mod: disabling or removing `deck-focus` restores the
previous appearance without replacing another mod or the official core.

## Implementation and checks

The documented SDK `on_frame` callback reads ErP LED bytes 0x0c (left) and 0x44 (right).
Each byte holds two 2-bit selection LED levels: primary in bits 0–1, secondary in 2–3.
Observed combinations are primary=2/secondary=1 for decks 1/2 and the reverse for decks 3/4.
Other combinations are unknown. Four click-through SDK surfaces draw two small number
badges and two thin panel edges, strictly below the waveform area at y=561 or y=681.

The worker/bus callback only reads bytes and publishes atomic state. The UI thread owns
surface placement and painting. There are no firmware hooks, library writes, file access,
network access, dynamic loading, or dependencies on private core symbols.

Build and test from this directory:

```sh
python3 ../../az-mods-sdk/tools/azmod-pack build .
make test AZ_SDK=../../az-mods-sdk
clang -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I../../az-mods-sdk/include tests/test_selection.c -o /tmp/test-deck-focus
/tmp/test-deck-focus
```

The focused test covers all 256 LED-byte values, all four selected pairs, unchanged input
frames, wrong bus/short frames, geometry, stale/unknown states, layout and visibility gates.
The official SDK harness also runs `tests/scenarios.c` to exercise actual surface drawing.
macOS harness runs do not measure no-I/O or no-allocation checks. Native button behavior
and geometry must still be verified on physical hardware before promotion.

The development harness lacks the newer `frame_filter` service. The implementation uses
the original documented `on_frame` LED callback, supported by both the running core and
harness, rather than a test-only stub.

Follow the parent AGENTS.md and recovery-kit backup requirements before physical USB changes.

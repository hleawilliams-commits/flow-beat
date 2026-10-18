# MODS Shortcut 0.1.3: emulator evidence (2026-10-17)

## Root cause of the dead button

The core's `sdk_input.c` times every frame-filter call (200 µs budget, about 1000 calls a second on the deck) and switches a filter off for the session after **8 overruns, counted cumulatively** (`SDK_FILTER_STRIKES`). A preempted call counts. On a busy deck, one always-on filter eventually collects 8 strikes. The button stays visible, but taps do nothing. That matches the 0.1.1 physical capture (`off=1`, `strikes=8`). 0.1.2's lighter idle path makes strikes rarer but can't prevent them.

## What 0.1.3 changes

- **Four input lanes.** The mod registers up to four frame filters. Each has its own strike count. Only the active lane runs the shortcut; spares do one relaxed store per frame.
- **Failover.** About 4 times a second, the UI thread checks each lane's frame count. A lane that stalls on two looks in a row while another lane advances has been switched off by the core, and the shortcut moves to the next live lane.
- **No silent dead button.** If every lane is quiet for 2 s, the button greys out, and a tap shows "MODS is off until restart. Use SOURCE > MOD MENU". Any later frame clears this, so a stalled bus is not mistaken for dead lanes.
- **Tap log.** Each tap logs its outcome: SOURCE opened, panel input cancelled it (bytes 31/32/33 + knob), the screen changed (view code), timed out, ignored, or refused (all lanes off). Lane registration, switch-offs and moves are logged too. Lines only appear while Mod Menu logging is on.

## Emulator run (ep147-9e9454e357389ece, AZ-MODS 1.2.3)

| Step | Result |
|---|---|
| Load | `MODS: 4 input lanes registered`; four `mods-shortcut` filters, all `off=0` |
| Tap, all lanes live | SOURCE opened (`02-tap-lane1.png`) |
| Set lane 1 `off=1` (what 8 strikes does) | `MODS: moved to lane 2 of 4` (`06-lane1-killed.json`) |
| Tap | SOURCE opened via lane 2 (`07-tap-after-lane1.png`) |
| Set lanes 2–4 `off=1` | `MODS: no input lane is running; button off until restart` (`09-all-killed.json`) |
| Screen | MODS greyed out (`10-dim-button.png`) |
| Tap | Banner shown, nothing sent, `MODS tap: refused, every input lane is off` (`11-dim-tap-banner.png`) |

The `off` flag was written through `/proc/<pid>/mem` on the localhost emulator only, to reproduce the core's own switch-off. EP147 was restarted afterwards; 0.1.3 remains loaded in the emulator.

Observation: on the emulator's SOURCE screen with Cloud Log In highlighted, the SDK reports view 2 (BROWSE), so the "Tap MOD MENU" banner is skipped and the log records a screen change. SOURCE did open. This is cosmetic.

## Host checks

385 shortcut and 299 component assertions pass plain and ASan/UBSan. 35 concurrency assertions pass plain, ASan/UBSan and TSan. The SDK harness passes plain/ASan/TSan. The ARM64 build passes SDK validation.

## Limits

Four lanes multiply the button's lifetime; they don't make it permanent. Only a core change fixes that (see `docs/UPSTREAM-FILTER-STRIKES.md`). Not yet run on the physical deck.

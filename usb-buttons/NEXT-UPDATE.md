# USB 1 / USB 2: next update

Henry selected two buttons in the former **WAVEFORM heading**, then deferred them to the next update while using the already published community packages. This is the implementation handoff, not a released feature. No new USB-button UI, native activation, emulator installation, or physical-deck change was made during this review.

## Placement and behavior

Use the heading rectangle **x=0, y=0, width=174, height=46**. This is the exact rectangle currently covered by MODS Shortcut 0.1.1's optional `hide-heading` surface. A starting layout is USB 1 at `(4, 5, 78, 36)` and USB 2 at `(90, 5, 78, 36)`, leaving a gap between targets. These are proposed coordinates, not a rendered or touch-tested result. Keep both buttons above y=46, where Phase Strip begins; leave native tabs, view controls and waveforms outside the surfaces.

Resolve ownership of the heading before enabling the buttons: the current full heading mask can cover a later surface depending on core surface ordering. Do not make correctness depend on community-mod load order. Either coordinate the mask through a supported service or revise its geometry/ownership in a separately versioned mod. Disabling USB Buttons must restore the appropriate heading behavior without leaving a dead input region.

A USB button must activate that USB's native library and retain its native browsing context. Merely opening SOURCE does not meet the request. Labels stay USB 1 and USB 2; user names and volume labels are not reliable port identities. Do not add a second Browse button, a hidden LOAD action, USB STOP, blind row numbers, knob steps, or fixed touch automation.

## Static findings from the pinned firmware

These findings were checked against Henry's private local EP147 ELF for `ep147-9e9454e357389ece`, already identified in the [research](../experiments/usb-browser-switch/README.md). Raw firmware and disassembly are not copied into this repository. Static inspection supports the next observer; it does not validate runtime object ownership, calls, or a finished shortcut.

### Native activation guard and owner relationship

The native select action at `0x1b296c0` reads a mode word at parent controller `+0x1e8`. At `0x1b29734` it dispatches to the source-list activation helper only when that word is zero, loading the child source-list controller from parent `+0x308` before calling `0x1c324e0`. Nonzero modes take other native controller paths. Checking the SDK's broad SOURCE view alone would bypass this real native guard.

The child constructor call at `0x1b2db28` receives its third argument from **parent +0x198**. The child constructor stores that argument at **child +0x60** (`0x1c31520`). Thus `*(child+0x60)-0x198` is a candidate owner relationship available from the already captured child. This is **not permission to dereference an arbitrary reverse-derived pointer**: first establish the exact listener/owner vptrs, verify `*(parent+0x308)==child`, demonstrate lifetime ownership on the native message thread, and reject mismatch or destruction. The current observation probe does none of these parent checks.

The selected-row activation helper reads the current native ListBox selection and derives its descriptor; it does not accept a requested USB port. A future transaction must resolve a fresh, unique live descriptor, select that row through the native API, then re-read and compare the descriptor before one guarded activation. Never call the helper merely because the controller pointer is nonzero.

### Committed browser source candidate

The browser `MediaChangeEvent` handler at `0x1ffb6c8` receives its object in x0 and uses that object's media identity at `+0x98` (valid byte) and `+0x9c` (two identity bytes). It reads the browser-data object from event `+0x58` and looks up per-medium state through that object `+0x200`.

At `0x1ffbae0` through `0x1ffbb0c`, the normal change path copies that identity into the browser-data object's **`+0x1e0` valid field and `+0x1e4/+0x1e5` identity bytes**. It also clears a state flag at `+0x1ba`. Its already-current branch compares the same fields and returns 1 when the identity matches and that flag is clear. The normal completed update path returns 0; invalid media/missing browser data have -1 paths. This is a stronger candidate for observing actual browser source than SOURCE row focus/active presentation flags.

A next observer must validate this event's ABI, expected prologue, native thread, lifetime, return values and browser-data provenance before using it. Capture the post-call identity only while its native lifetime is owned; do not retain an untracked browser-data pointer. A callback entry or target descriptor alone does not prove the browser accepted the switch. Keep a completion sequence/generation so an old successful manual switch cannot satisfy a new request. Require both the requested committed identity and visible BROWSE state before reporting success, and verify those observations in the emulator against the native USB header and retained playlist/row.

## Remaining implementation and verification

1. Extend the **emulator-only observer** with validated parent mode and committed browser identity. Confirm SOURCE selected row and committed source can differ. Exercise destruction/unload and reject unsupported threads.
2. Connect the existing bounded SOURCE helper to one queued USB request. Run native selection/activation on the verified EP147 message thread, never the overlay UI thread. Cancel on genuine navigation, knob movement, LOAD, timeout, generation changes, missing/ambiguous target or leaving the expected view. Recheck immediately before activation and permit it once, without automatic retries.
3. Resolve the exact observed USB identities from current source descriptors; labels and row numbers may move. Require exact firmware and live uniquely mounted target. Reject USBs absent at tap time or removed before activation; do not fall back to the other stick.
4. Verify manual-equivalent context retention and true completion for both ports, same-port requests, no USB, one USB, removal, rapid presses, identical labels, navigation interruption, mod disable and reload. Test 2/2-4/4 DECK layouts and heading ownership separately.
5. Only after those gates pass: version and build a complete package, retain the prior archive, run SDK validation, publish the next release and catalog entry, then perform a separate backed-up hardware test while stopped. Keep Kyle's core and firmware unchanged.

The existing observer's source-row descriptors, hook acceptance and manual context-retention checks remain useful evidence. They do not yet satisfy these activation gates. No automatic USB switching success is claimed.

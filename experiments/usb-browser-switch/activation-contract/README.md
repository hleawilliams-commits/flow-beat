# USB activation contract: value-only intent, native adapter gated

2026-10-17. This follow-up identifies a fresh native callback that could receive USB requests without caching a controller or borrowing a physical knob press. It also adds a tested, pointer-free request broker. **No native activation adapter or USB buttons are implemented.** The newly identified timer/view retention and final Local Library argument contracts must be resolved before activating anything from that callback.

This is a narrower goal than final asynchronous readiness: one deliberate action requests the chosen USB's actual native browser source. Native browsing then retains its normal loading/error behavior. A pending request, selected SOURCE row, opened Local Library pane, copied commit snapshot or execute return 0 must not be displayed as a completed switch. [V4](../missing-boundary-investigation/README.md) already establishes why return 0 alone is insufficient.

## New native boundary

All anchors use the exact firmware SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`, tag `ep147-9e9454e357389ece`. The [verifier](verify-static.py) checks 36 minimal anchors plus vtable/RTTI facts; [results](static-validation.json) contain no firmware dump. Pinned upstream remains core `3e0c5cb010454228f4ee63226b60d060494eeaae`, SDK `6563e8e657a4ef113696de9d0f9327a6c8d0fab7`.

| Anchor | Established fact |
|---|---|
| SourceSelectView RTTI `0x2d5a3a0` | Its tenth zero-based base entry is `StateRefreshCycleTimer::IListener`, offset **`+0x1b8`**. |
| `0x2d5b018`, slot `+0x18` at **`0x2d5b030`** | This listener's method is **`0x1b2e538`**, exactly one `RET`. It is a candidate for a guarded vtable claim, not an inline hook over a four-byte function. |
| `0x1b2d3cc..0x1b2d3f8` | Constructor builds the StateRefreshCycleTimer in the view at **`+0x348`**. |
| `0x1b2ddf4..0x1b2de98` | Constructor appends the view's `+0x1b8` listener to that timer's listener collection at view `+0x370` (timer `+0x28`). |
| `0x1ae8cb0..0x1ae8cc8` | Timer compares the current pthread with JUCE's message-thread singleton. The other path queues native task work; no custom borrowed closure is needed. |
| `0x1ae9098..0x1ae90a8` | Timer ID 1 dispatches the listener through slot `+0x18`, supplying `x0=listener`. Its return is unused. The original no-op consumes no input registers. |
| **`0x1ae90b4`** | Dispatcher reads the timer again after the listener returns. The live callback argument alone does **not** prove it is safe to navigate/destroy the containing view during that callback. |
| `0x1b2cb30`, `0x1b2cbd0..0x1b2cbd4` | View destruction removes its listener and destroys its embedded timer. |
| `0x1af1ec8..0x1af1ef0` | A native factory constructs SourceSelectView, increments its native count at `+0xd8`, and stores an owning view pointer at factory `+0x90`. This identifies an owner but does not prove its retention over reentrant dispatch. No reference count is changed by this work. |

This is a concrete **entry-only observation** candidate with a legitimate live argument. Its actual invocation frequency, hidden-view behavior and runtime thread identity have not been measured. A future observer must use the SDK claim service, preserve the no-op original, refuse an unexpected firmware/slot, copy only values while the native argument is live, and make no post-return reads.

The callback offers a possible alternative to SOURCE plus synthetic knob clicks. It avoids attributing a later unrelated physical click to a queued request. It is not yet permission to add activation inside the timer loop.

## Source focus is only part of the operation

Tracing the actual native route exposed an important additional gate:

1. `SourceListWidgetController` row listener **`0x1c32f40`** compares the requested row with the currently selected row. A different row selects and previews it through the native ListBox. An already-selected row publishes the descriptor and calls the SourceSelectView listener.
2. That listener **`0x1b29458`** toggles parent mode `+0x1e8`; it can focus the Local Library pane. It does not by itself establish the browser MediaChangeEvent observed by V4.
3. The next parent `selectListItem` call **`0x1b296c0`** chooses `0x1c324e0` in mode 0 or, after the normal overlay checks, **`0x1be8a20`** in mode 1.
4. The mode-1 method reads the selected DeviceLibrary row's kind at `+0x14`, obtains a library value through **`0x1be5798`**, then calls **`0x1be7e38(controller, kind, &value)`**. That native resolution includes multiple local/cloud library cases. Its complete argument/value ownership contract has not been established here.

Consequently, “select USB row” is an insufficient backend. A real adapter must select that USB's **Local Library** and reach the native browser source request. It must resolve from live descriptors/rows, never fabricate a MediaID, reuse a remembered row number, or accept matching USB names as identity. No partial implementation is exposed as a button.

## Tested intent broker

[intent.hpp](intent.hpp) implements only the cross-thread policy. A single lock-free 64-bit word contains enable state, lifecycle, USB port, monotonically distinct request number and request time. No native object, borrowed reference, callback, string or container is stored. [Tests](tests/test_intent.cpp) passed **731 checks each** in plain, ASan/UBSan and TSan runs; [validation](validation.json) pins their sources. Those tests are policy/race coverage, not firmware activation tests.

The intended flow is:

- The UI queues USB 1 or USB 2. It reports pending, not switched. Another request while pending/entered returns Busy instead of replacing an in-flight native action.
- At a separately proven native entry scope, an adapter obtains a **fresh** live target snapshot and verifies view/mode, descriptor uniqueness, media availability and its dispatch ownership preconditions. An unavailable adapter produces Unavailable; it must not fall back to merely opening SOURCE.
- A successful `enter(ticket, now, Ready)` CAS is the dispatch linearization point. The adapter may invoke exactly one already-proven native action within that same ownership scope. The broker neither supplies nor proves that scope.
- Disable/cancel/navigation/eject winning before `enter` prevents the action. Disable after `enter` does not pretend to undo a native operation already dispatched. A newer deliberate input may supersede it through the native UI's own rules; this broker does not add an asynchronous cancellation API.
- The caller uses stack-local `Finish` to release the broker token on return or exception. `Finished` means only **the broker is free**; it is neither native admission nor final completion. Reentrant requests remain Busy until that release.
- Missing/ambiguous targets fail once. Pending work expires after two seconds, including a backwards or ambiguous clock delta. Wrap over the 32-bit clock is covered for the normal short request interval. The adapter must poll or cancel within a clock epoch and cancel on clock reset/reinitialization; the broker cannot distinguish a whole unobserved 32-bit clock wrap from a short interval. There is no automatic retry when a USB later appears, and exhausted request sequence space refuses new work rather than reusing a token.

Race tests cover competing dispatchers, disable versus entry, old cancellation against a new token, timeout/wrap, exceptions, reentrant requests and missing/ambiguous/unavailable targets. The mailbox contains no native calls, so it cannot make an unsafe binding safe. There is no ARM/community build because no native mod is supplied.

## Next bounded proof and implementation plan

1. **Resolve the retention gate first:** identify a native synchronous owner/retain API whose lifetime covers the entire timer dispatch, or use another live entry whose native caller already permits the final navigation operation. Do not infer that factory ownership or the message thread prevents destruction; do not copy reference-count fields or carry a borrowed pointer to a later timer.
2. **Resolve the final native action:** map the Local Library row/value contract through `0x1be5798 → 0x1be7e38`, including missing media and its existing guards. Confirm that an actual requested USB MediaID reaches the native event; mere source-row focus is not enough.
3. If a new boundary observer is approved, claim only the exact timer-listener slot and copy bounded source/controller/value snapshots at entry. Check SOURCE visibility, mode 0/1, both mounted ports, hidden views and native navigation. No activation, destructor mutation, retained pointer, custom native call or final-completion claim belongs in that observation run.
4. Implement the adapter only after those facts are reviewed. Test deliberate USB1 → USB2 → USB1, distinct saved native selections, absent/ejected media, ambiguous descriptors, disable before/after dispatch, timeout, rapid requests, reentrancy and exceptions. A canceled pending intent must never activate when a later USB or view appears.
5. A heading button is eligible only after the adapter actually selects the requested USB's native browser source in the emulator and restores normal per-media context. UI feedback can honestly stop at “request sent” while the native browser shows its actual loading/result state; final asynchronous readiness is not a prerequisite for this narrower feature.

No emulator or physical device was accessed, no core or older experiment changed, and no release or catalog entry was created.

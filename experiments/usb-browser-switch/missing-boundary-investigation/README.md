# Missing USB boundary investigation

Static, host and bounded emulator follow-up, 2026-10-17. V3 observed a committed identity only on the third switch after restart. The native **empty-list request path bypasses V3's helper**, and a failure on that path can still produce outer execute return code 0. A separate V4 observer now distinguishes that path using only supplied scalar values. Its new emulator run reproduced the sequence: first USB1 and USB2 selected the empty path, while returning to USB1 reached the copied commit boundary. This directly explains the missing observation in the new run; final transaction completion remains UNKNOWN. [Runtime evidence](runtime-evidence/README.md).

V3 source and evidence are unchanged. This work adds no activation, USB buttons, core changes or physical access. [V3 runtime baseline](../completion-contract/runtime-evidence/README.md), [static verifier](verify-static.py), [sanitized static result](static-validation.json), [V4 implementation](observer-v4/).

## Exact branch and ABI

Evidence uses private EP147 SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`, firmware `ep147-9e9454e357389ece`. No firmware or disassembly dump is included. Native addresses are tied to that exact image.

| Native anchor | Consequence |
|---|---|
| `0x1ffbaf8..0x1ffbb0c` | The handler writes BrowserData's identity and clears busy, before either later path. Intervening calls can re-enter; dominance is not a live identity snapshot. |
| `0x1ffbc90..0x1ffbc98` | Loads list bounds at `+8/+16` and compares them. Unequal bounds select V3's `0x2014a88` helper; equality branches to `0x1ffbea0`. |
| `0x1ffbea0..0x1ffbea4` | Reads the **live MediaChangeEvent** request-agent identity at `+0x28`; a null agent skips the request helper. The new entry copy uses exactly this known field, inside the verified `0xa0`-byte event. It never follows the copied pointer. |
| `0x1ffbea8..0x1ffbec8` | Packs request type in bits 0–31, request identity in 32–47, and opaque native category byte in 48–55. Bits 56–63 are cleared. |
| `0x1ffbecc` | Calls `0x1fd3a18` with `x0=agent`, `x1=packed request`. Exact native return address is **`0x1ffbed0`**. |
| `0x1fd3a18`, FDE `0x2c8` | Ordinary request-agent method. Diagnostics identify `MusicLibraryRequestAgent.cpp`. Wrapper ABI is **`int(void *, uint64_t)`**: its caller consumes `w0`, and it consumes `x0/x1`. |

The normal CFG verifies that both the final identity store and the empty-list equality edge dominate this call. Only that equality branch enters the candidate block. This excludes ordinary entry from V3's nonempty branch; it does not prove that identity remained unchanged through every intervening native callback.

The ABI verifier checks all 178 helper instructions and 145 normally reachable instructions: no incoming `x2..x8` or `v0..v7` is explicitly consumed before overwrite or a call clobber. The first calls are the already reviewed two-argument diagnostic string constructor `0x24a1180`, or communication adaptor method `0x1fc5590`. For the latter, the helper sets **its own** `x8=sp+0x50`; the adaptor consumes `x0` (adaptor), `x1` (packed request), and that newly supplied result address. It does not forward a hidden result register from the observed helper's caller. Its first validation call uses the locally stored packed request. Diagnostic and adaptor paths do not consume incoming FP arguments. Prologue, argument-load and packing/call bytes are checked before claiming the hook.

The ordinary hook island uses `B` then `LDR X17; BR X17`, preserving native LR. V4 gates the new entry with `0x1ffbed0` and a valid, depth-bounded stack-local TLS event scope. It compares the native agent pointer **as a value** with the entry-time copy and compares only the packed MediaID portion with the copied MediaID. Bits 48–55 come from the first byte of the native per-media object. The helper uses that byte and rejects zero, but its enum meaning and expected per-request value are not established. V4 preserves it verbatim, including zero, and reports `category-match=unknown`. `EMPTY_PATH_MEDIA_AGENT_MATCH_CATEGORY_UNKNOWN` compares only the complete MediaID and agent identity; it is never a full packed-argument match. Native admission is reported only as a separate returned scalar. No agent, event, BrowserData or remembered pointer is dereferenced by this added hook.

## Failure and replacement semantics

The new observer keeps three questions distinct: which branch ran, what its native request method returned, and whether the overall browser operation completed. Only the first two are observed here.

| Native behavior | Evidence and limit |
|---|---|
| Request helper returns `-1` | Missing adaptor, invalid packed request, or unsuccessful immediate adaptor result reach `0x1fd3ad8` / `0x1fd3bc8`. This is local request failure, not an outer transaction result. |
| Request helper returns `0` | The returned status byte is zero; the native code copies its request-result fields into the agent and sets agent `+0x38` to 1 at `0x1fd3b28`. This records admission/state, not populated browser rows or asynchronous completion. |
| Outer execute can return `0` after helper `-1` | `0x1ffbed0` branches on zero. The nonzero path emits the root-request diagnostic, rejoins at `0x1ffbf68 → 0x1ffbcf8`, and can reach an explicit zero return at `0x1ffbe7c` / `0x1ffc0ac`. **Outer rc 0 is therefore not a success test.** |
| Already-current path | Identity comparison plus busy==0 can return 1 at `0x1ffbfe4`, bypassing both observed helpers. A missing boundary on this path also remains UNKNOWN in the observer. |
| A newer media event may replace an active one | The MediaChangeEvent constructor supplies type 0; base constructor stores it at `+0x78`. The replacement predicate `0x1ffb4d0` explicitly permits incoming types 0/1, including while request-agent state is active. The executor transfers the new event into handler `+0x10` and invokes the old event's deleting destructor at `0x201fd78`. |
| Destruction releases agent ownership | Base event destructor clears `+0x28/+0x30` and releases its shared control starting at `0x200f144`. This does not establish when all external request work has cancelled or whether a later reply is stale. |

No completion record should be produced merely because the request method returned zero, because an earlier copied identity matched, or because the outer execute returned zero. An activation design still needs an owned, generation-correlated final state or a deliberately narrower behavior contract. Async reply/cancellation ownership and stale-result handling remain open.

## V4 contract and checks

The separate `usb-observer-v4` retains V3's three observation hooks and adds the ordinary entry at `0x1fd3a18`. Its new records are:

- `EMPTY_PATH_ENTRY`: exact caller branch reached; immutable outer entry sequence, full supplied packed value, and explicitly partial MediaID/agent-pointer match status with category unchecked. It is explicitly **path-only**, with no committed identity claim.
- `EMPTY_PATH_RESULT`: only the returned integer, correlated with both the path-entry record and outer event entry. A native throw produces no scalar return record. It performs no post-original native reads.

Mismatch is retained as a mismatch. Invalid/no/nested-overflow scope, wrong caller, or wrong thread cannot borrow an outer context. Originals are called exactly once; all parameters and return values are forwarded. The exception/unwind flags and RAII TLS restoration remain. No destructor hook, polling, reference-count mutation or native activation call was added.

The focused suite passed **1,122 assertions per pass** in plain, ASan/UBSan and TSan. New coverage includes helper `-1` with outer 0, argument and agent-identity mismatches, opaque category values 0/1/2/255 without a full-match claim, upper packed bits, wrong/no/invalid scope, nested request correlations, depth overflow, exceptions, frees inside the helper and concurrent destruction after outer return, and partial fourth-claim failure. Existing V3 tests remain. ARM64 SDK build and validation passed. [Source/build results](validation.json). [Earlier validation](validation-before-category-clarification.json) is retained as superseded; its artifacts and source snapshot remain private in `observer-v4/out-history/`.

The bounded emulator check completed one ordinary native USB1 → USB2 → USB1 sequence after an intentional emulator process restart. Four hook claims were accepted. Two empty-path pairs returned helper rc 0 and outer rc 0 with supplied category byte 1, explicitly unchecked; the return to USB1 produced one copied commit observation. No custom activation was invoked. The 124-file baseline, tar-derived modes, approvals, both core hashes and loaded-mod list were restored. The source/build validation JSON above is the preserved pre-runtime snapshot; [runtime results](runtime-evidence/runtime.json) record the separate test. Missing-media, failure-injection and cancellation matrices remain untested.

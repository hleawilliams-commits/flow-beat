# USB source commit observation contract

Static follow-up and bounded emulator check on 2026-10-17. The native caller does **not** provide an independently pinned event through an outer wrapper’s return. A narrower ordinary function entry can observe the committed identity while native code supplies a live event argument. The V3 experiment is observation-only and explicitly reports a boundary observation, not transaction or browser readiness.

## Pinned evidence

Firmware `ep147-9e9454e357389ece`, EP147 SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`, MD5 `8daa1de55d7328f458f6d855cdc3cdde`. Inspection used the private local ELF, without publishing firmware/disassembly dumps. Core hook ownership was checked against source `3e0c5cb010454228f4ee63226b60d060494eeaae`. The [V2 runtime check](../observer-v2/runtime-evidence/README.md) separately verified its two existing claims and observed main-thread callbacks.

The executor at `0x201f5d8` transfers the caller’s unique event pointer into mutable handler `+0x10` at `0x201fd60..0x201fd68`, then deletes the replaced event at `0x201fd78`. It has no extra local strong event owner spanning execute. Its replacement predicate is MediaChangeEvent vtable `+0x50`, `0x1ffb4d0`; it checks request-agent/browser/creator state, and several missing/idle-state paths return true. It is not a universal executing/reentrancy lock. The wrapper at `0x2020688` sees its owning stack pointer cleared after transfer. Therefore post-original event reads remain excluded.

## Smaller function-entry boundary

| Anchor | Finding |
|---|---|
| `0x1ffbaf8..0x1ffbb0c` | MediaChangeEvent writes BrowserData’s 32-bit type and two identity bytes, and clears busy at `+0x1ba`. |
| `0x1ffbc90..0x1ffbc98` | Native list-state bounds choose the following helper for a nonempty list; an empty list takes `0x1ffbea0`. |
| `0x1ffbc9c..0x1ffbca4` | Sets `w1=0`, passes `x0=x19` (the current native event), and calls `0x2014a88`. Exact return address: `0x1ffbca8`. |
| `0x2014a88..0x2014aa8` | Ordinary function prologue preserves its event argument and reads event `+0x58` as BrowserData. Its later calls consume that live browser’s current committed identity. |
| Event `+0x58/+0x60` | The same strong BrowserData pair V2 validates: exact control vptr `0x317c028`, data `control+0x10`, positive atomic strong count. The native callee supplies event lifetime at entry; validation is not a refcount acquisition. |
| `0x2014ab0` | Calls `0x2018870` with the actual BrowserData argument. That callee reads committed identity and resolves the current per-media object. This second helper is unnecessary for the minimal observer. |
| `0x1ffbecc` | Empty-list path calls `0x1fd3a18` with a request agent and packed requested identity. It does not supply the event/BrowserData pair needed by the selected narrow observer; leave this path UNKNOWN. |

A normal-control-flow analysis of the full MediaChangeEvent FDE (`0x1ffb6c8`, extent `0x20dc`) reaches `0x1ffbca4`, but cannot reach it with the final identity store `0x1ffbb0c` removed. The normal return path cannot revisit those identity writes. The verifier resolves the non-returning throw/unwind PLT symbols; treating those calls as returning had produced a false loop in an earlier conservative analysis. This is dominance of the store on ordinary native paths, not analysis of asynchronous destruction or exception landing-pad entry. The callee has many callers, so exact return-address gating is required in addition to event type and active context. [Reproducible verifier](verify-static.py), [sanitized result](static-validation.json).

There are native calls between the commit and this entry: disposal of old pages/records, string-like cleanup, and deallocation. They are not assumed non-reentrant. V3 must revalidate the live native argument, current request, owned browser identity and committed value at the later helper entry. Nested media handlers receive their own thread-local context; an inner request cannot borrow an outer request’s token. Mismatch, missing context, unexpected thread, changed owner, no observed helper or exception gives no success claim.

Later code invokes more native helper/creator callbacks and can fail, throw, or cause another source change. The helper-entry observation therefore means only **“this request’s identity matched committed BrowserData at this exact native boundary.”** It is not an end-of-handler completion, successful final return, current state after return, or fully populated BROWSE UI. No cached native object is followed later.

## Helper ABI and unwinding

The wrapper forwards `int(void *event, unsigned mode)`. The exact native caller sets `x0` to its current event and `w1` to zero; the instruction at `0x1ffbca8` consumes the returned `w0`. In the helper's complete FDE (`0x2014a88`, extent `0x844`, 529 instructions), `x0` is saved as the event, its BrowserData is read at `+0x58`, and `w1` is masked to a byte at `0x2014aac`. Return setup at `0x2014c60` places the scalar result in `w0`.

The verifier tracks incoming `x2..x8` and `v0..v7` across the helper's normal control flow. None is explicitly consumed before overwrite or an ordinary call clobber. The two first-call routes with still-unused argument registers are independently checked:

- `0x2014ab0` calls `0x2018870` (FDE `0x90`) with BrowserData only. That callee uses `x0`, then supplies its own `x0/x1` for the tail call at `0x20188b4` to the per-media map lookup `0x1fb2ef0`. The lookup saves map/key from `x0/x1`; comparator paths set `x2` from the key and overwrite `x3` with the comparator before calling it. The alternate first calls resolve to the standard `__cxa_guard_acquire`/release ABI. No additional incoming FP argument is used.
- `0x201504c` calls the diagnostic string constructor `0x24a1180` (FDE `0x20`) with destination/string in `x0/x1`. It sets its own indirect-result `x8` from `x0`, stores `x1` locally and calls `0x24d9300`; that callee immediately replaces `x2` from the supplied string, uses the newly assigned `x8` as destination, and uses no incoming FP argument. Thus `x8` is not an unforwarded return-buffer argument to our selected helper.

These anchors justify the selected two-argument entry wrapper, not arbitrary signatures at other firmware addresses. Calls and exception landing pads remain outside any claim of complete transaction ownership. The build keeps `-fexceptions -fasynchronous-unwind-tables` and frame pointers, links with the deck's C++ runtime, and uses a stack-local RAII scope to restore the prior TLS context when a native exception passes through. Host tests exercise nested unwinding and native failure after a copied boundary. No native memory is read after either original callback returns; RAII restores only the observer's own stack context.

## Implemented V3 and bounded runtime plan

The separate emulator-only [usb-observer-v3](observer-v3/) implements the two V2 hooks plus one ordinary function-entry claim at `0x2014a88`. No mid-function instrumentation, destructor hook, refcount mutation, activation call or core change is included. Existing V2 stays untouched.

1. Media execute entry copies its immutable sequence, requested MediaID and diagnostic event/browser identity, then pushes a stack-local RAII context into thread-local storage. Nested calls restore the previous context on return or exception; observation is bounded by a depth limit.
2. The additional entry wrapper observes only when native caller return address is exactly `0x1ffbca8`, its supplied `self` equals the active event identity, and the thread/firmware gates pass. It validates the live argument and BrowserData pair, compares current request/browser against the copied entry values, and copies committed identity/busy. A match emits `COMMITTED_AT_ENTRY`; mismatch emits no completion. It forwards the original exactly once and performs no native reads afterward.
3. Execute result remains a separate scalar-only record. A commit observation followed by native failure or exception remains evidence of a transient boundary, never overall success. Paths with no helper observation remain UNKNOWN.
4. Host tests cover exact/wrong return address, no/invalid/nested/overflow scope, changed request/owner, committed mismatch/busy, native success/failure/exception, reentrant source changes, frees during or immediately after native originals, and partial third-claim failure. Existing V2 parent/queue/guard tests remain. All **620 assertions per pass** succeeded in plain, ASan/UBSan and TSan; ARM64 SDK build and directory validation passed.
5. The bounded isolated emulator run below established the third hook’s actual acceptance and one matching call-site gate during ordinary native USB selection, then restored the full baseline. It added no activation or missing-media matrix. Runtime evidence is recorded separately; the static and host results alone make no runtime claim.

The core function-hook island uses X17 as scratch, which makes an ordinary C wrapper at the mid-function `0x1ffbb10` commit point unsuitable without a separate register-preserving instrumentation contract. The selected function-entry boundary avoids that ABI problem. Claim acceptance remains subject to the SDK’s normal ownership and relocation refusal; no forced takeover is permitted.

The island also preserves the native link register: `inline_hooks.c` installs `B island`, then `LDR X17,#8; BR X17` (`0x58000051`, `0xD61F0220`), rather than BL/BLR. Thus the ordinary function-entry wrapper can gate `__builtin_return_address(0)` against the native `0x1ffbca8`. The V3 runtime check below observed that gate accepting exactly the intended native boundary.

## Validation result

[Source/build hashes](validation.json) pin the 620-assertion host passes and ARM64 build. The [bounded emulator result](runtime-evidence/README.md) accepted all three hooks and captured **one** correlated `COMMITTED_AT_ENTRY` on a native return to USB1, plus three separately correlated scalar results. The other two requests had no candidate record and remain UNKNOWN. All baseline files, modes and cores were restored after the run. This establishes the narrow callback-entry observation, not a transaction-completion or activation API. USB button activation remains deferred.

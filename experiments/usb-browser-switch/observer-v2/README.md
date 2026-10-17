# USB browser observer v2 — emulator only

This is a separate, unpublished observation experiment. It records the native source selector's parent mode and the browser's already-committed media identity at entry to a native media-change callback. The native return value is logged separately, correlated to that entry snapshot. **It does not activate a USB, select a row, navigate, load a track, inject input, or change library state.** The existing `../probe/` is untouched.

Host validation and an ARM64 SDK build pass. **No v2 emulator run or physical installation has been performed; runtime readiness is unproven.** In particular, the media-change callback's thread and these hook claims have not been proven at runtime. A safe completion observer and activation path are still unresolved. This is not the USB 1/USB 2 button implementation.

The [completion ownership follow-up](COMPLETION-OWNERSHIP.md) identifies the exact native caller, queued route and owning handler. It narrows the remaining reentrancy/destruction questions and explains why the core's existing browser snapshots are not switch-completion results. It adds static evidence only; v2 remains unchanged.

## Exact static anchors

All addresses refer to firmware tag `ep147-9e9454e357389ece`, EP147 MD5 `8daa1de55d7328f458f6d855cdc3cdde`, SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`. Static inspection used the private local ELF; no firmware, disassembly dump, music, database, or key is included here.

| Anchor | Evidence and use |
|---|---|
| `0x1b296c0` | Native source-parent select callback, ABI `void(void *self)`. At `0x1b29734` it reads parent `+0x1e8`; only mode **0** calls source activation helper `0x1c324e0`, using child `+0x308`. V2 observes at entry, then forwards the original exactly once. It never invokes this function itself. |
| Parent primary vptr `0x2d5ab98` | The RTTI names `SourceSelectView`. Constructor `0x1b2d248` and destructor `0x1b2c9e0` install this primary vptr. The prior research's “SourceSelectController” label refers to this source-selection parent. |
| Parent listener `+0x198`, vptr `0x2d5af50` | Constructor stores this vptr at `0x1b2d30c`. Listener entries include `0x1b29458` and `0x1b29038`; they change parent mode. Child `+0x60` must point back to this exact listener. |
| Parent child `+0x308` | Parent constructor passes `parent+0x198` as the child's listener at `0x1b2db28`, then stores the child at `0x1b2db30`. Therefore a child-constructor return is **too early** to require the parent's backlink. V2 uses the already-live parent callback instead. |
| Child/model vptrs `0x2e0b3e8` / `0x2e0b900` | Same controller/model identities as v1; model is child `+0x80`. Rows pointer `model+8`, capacity `+0x10`, count `+0x18`, stride `0x28`. V2 copies only kind, media-valid byte, MediaID, focus and source-active. It does not read labels or undefined row padding. |
| `0x1ffb6c8` | `MediaChangeEvent` handler, ABI `int(void *self)`, primary vptr `0x3163ae8`, slot `+0x48`. Native return is **0** after its update path, **1** for already-current media, **-1** on shown invalid-input/error paths. V2 forwards the exact return. Return 0/1 is recorded as RESULT_ONLY, never as proof of committed state. |
| Event `+0x98` / `+0x9c` | Requested media's 32-bit native type/validity field and 16-bit identity. V2 copies defined native fields separately, not the trailing padding. Native validity checks the type's low byte and identity low five bits. |
| Event `+0x58` / `+0x60` | Strong `shared_ptr<BrowserData>` data/control pair. Exact inplace control-block vptr is `0x317c028`; data must equal `control+0x10`, and the strong count must be positive. BrowserData itself is not treated as a polymorphic object. |
| Browser `+0x1e0`, `+0x1e4`, `+0x1e5`, `+0x1ba` | Commit at `0x1ffbae0..0x1ffbb0c` copies requested type and two identity bytes to BrowserData and clears the state/busy byte at `+0x1ba`. Already-current path `0x1ffbfc0..0x1ffbfe4` compares all identity fields and requires that state byte clear. These offsets explain which fields represent committed identity. V2 reads them only at callback entry, before that callback changes them; it makes no completion claim. |
| `0x200f0d0` | Base UpdateEvent destructor, ABI `void(void *self)`. MediaChangeEvent's primary/adjusting deleting and non-deleting destructor paths reach it before the owned browser reference is released. Static lifetime research only; **not hooked by v2**. |
| `0x209e598` | BrowserData inplace control-block dispose, ABI `void(void *control)`, vtable slot `0x317c038`. It destroys data at `control+0x10`. Static lifetime research only; **not hooked by v2**. |

The observer claims only two callback bodies: parent select, then media-change handler. No destructor/dispose hooks or active-object scopes are retained. It does not claim the already-core-owned per-media lookup `0x1fb2ef0`, the timer body, any vtable slot, or source activation helper. Exact prologues and relevant vtable entries are checked before any claim. Any refused/partial claim leaves installed wrappers inert and forwarding; no forced takeover or unpatch exists.

## Lifetime and thread contract

Native object reads are permitted only inside a callback entered by native code. Parent/child reads occur **before** forwarding the parent callback. No parent pointer is retained for later dereference; its destructor therefore needs no hook. Published addresses are diagnostic values only.

The event's owned browser pair is checked and copied before forwarding. Its strong count is read atomically because other shared owners may update it; this is a structural check, not a reference acquisition. The observer relies on the native callback's live argument and its owned objects during entry, just as the native handler does. It does not extend that lifetime. Published addresses are never followed later.

**No native object is read after the original callback returns.** The wrapper publishes only the copied entry sequence and scalar native return value. If native code throws, the entry snapshot remains without a result record and the exception unwinds normally. If native code destroys the event or browser, copied values still remain usable, but no post-call state is inspected.

A review found that a destructor notification plus an atomic “invalid” flag would **not** protect post-call reads: another thread could start destruction after the flag check and before a dereference. No executor-held event reference spanning the wrapper return was proven. V2 therefore omits both those destructor claims and the post-call reads. It neither guesses a native refcount increment nor treats unexpected-thread detection as a lifetime pin. A completion observer needs a separately proven ownership/serialization contract, or a native service that returns a copied committed-state result.

Numeric pointer-range checks do not establish mapping or lifetime. There is no heap scan, `/proc` read, refcount mutation, getter call through a discovered vtable, or arbitrary pointer call.

All observing callbacks must run on Linux TID == PID, as v1's source callbacks did. An unexpected thread makes v2 inert and records the TID without reading native objects. **MediaChangeEvent's execution thread is unproven:** a refusal there is useful evidence, not permission to weaken the guard. Establish its actual native scheduling/ownership before considering a separate event-thread design.

Callbacks copy bounded records into a single-producer/single-consumer queue. They do not log, explicitly allocate, or wait on locks. A worker logs up to 128 records; queue overflow is counted. Missing SDK ticks suspend observation after about 2.5 seconds. All original callbacks continue unchanged. Native exceptions unwind using the deck's existing C++ runtime.

## Build and host validation

From the repository root:

```sh
sh experiments/usb-browser-switch/observer-v2/tests/run-focused.sh
python3 ../az-mods-sdk/tools/azmod-pack build experiments/usb-browser-switch/observer-v2
```

The focused tests include the actual production wrappers, replacing only native originals, thread identity, prologue checks and SDK plumbing. On 2026-10-17, **299 assertions passed per pass** with Clang plain, AddressSanitizer/UBSan, and ThreadSanitizer. They cover parent/listener/backlink rejection, model bounds, both media identities, no label dereference, exact result forwarding, separate entry/result correlation, native failure returns, synchronous native frees, changed ownership, exceptions, nested callbacks, wrong-thread refusal, queue concurrency/overflow, absent emulator opt-in, firmware/prologue mismatch, worker failure and both partial claim failures. These are host checks, not runtime firmware evidence.

The `concurrent-free` regression uses a test-only barrier immediately after the native original returns. A separate thread frees both event and BrowserData/control allocations before the actual production result path resumes. That path completes under ASan/UBSan and TSan; the instrumented native-read count remains exactly the count at original return. This explicitly covers the post-return check/free window instead of depending on a destructor callback being delivered in time. No production synchronization or lifetime pin was added. The regression makes no claim about native callback scheduling before the entry snapshot.

The SDK's ARM64 container build and directory validation also passed. It links against the deck's C++ runtime and exports only the SDK entry point. Local `out/`, `out-tests/`, and package files are ignored; no generated binary is intended for Git or the community catalog.

## Deferred emulator review/run

The mod ID is `usb-observer-v2`, version 0.0.1. Startup also requires the exact environment variable `AZ_USB_OBSERVER_V2_EMULATOR=1` **inside EP147's guest environment**. This is an explicit operator opt-in, not automatic proof of an emulator. Do not set it on a physical deck. Do not publish this experiment in the live catalog or install it on a physical USB.

After code review and an isolated emulator baseline backup, a later operator can build/package through the SDK, launch an emulator with that opt-in, and install only this experimental mod. Coordinate ownership of the emulator first. Capture exact core/mod hashes and claim logs. Verify normal UI continues; on refused claims or an unexpected thread, stop the experiment and preserve the logs.

Using only native controls, open SOURCE and use the native browse knob/confirmation path to select each USB and its library. The parent-select observation is tied to that native path; a touch-only action can take another path and need not emit a parent record. Do not synthesize a button press or call an activation helper to obtain evidence.

For each USB, collect these distinct pieces of evidence:

1. A valid parent record with exact child/listener ownership, mode **0**, and the currently mounted USB descriptor among its bounded rows. A valid record with nonzero mode proves only the observed identity, not that source activation is allowed.
2. An event-entry record with the requested MediaID and the **previously committed** identity/state at entry. This is not the result of that event. A later independently-entered event may provide another entry snapshot; do not invent one by calling native pointers.
3. Its separately correlated native result (`entry=<sequence>`, `rc=...`, `post-read=none`). Neither return 0 nor return 1 is labeled a verified commit. Dropped records or an exception can leave unmatched entries.
4. The actual native BROWSE view and retained playlist/selected record. Neither these screenshots nor the native return code substitutes for the missing safe post-commit identity observation.

Record callbacks' actual PID/TID, missing-media behavior, transitions and disable/re-enable. Manual native switching may still load browser metadata; this experiment adds no selection/activation operation. A future completion observer must first prove the exact executor's ownership and serialization or use a supported native copied-state service. That remaining gate blocks custom activation; do not progress directly from this probe to an activation call. Physical hardware and native activation from a custom button remain untested.

Status codes in `UV2` logs: `1 VALID` (entry snapshot only), `2 BAD_PARENT`, `3 BAD_CHILD`, `4 BAD_MODEL`, `5 BAD_BOUNDS`, `6 BAD_EVENT`, `7 BAD_OWNER`, `8 RESULT_ONLY`, `9 NATIVE_FAILURE`. No status means a USB switch was verified complete. These are historical callback observations, never callable activation permissions or a persistent browser-state API.

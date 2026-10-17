# Media-change execution and completion ownership

Static research, 2026-10-17. **The normal native owner and caller are identified; a lifetime contract permitting observer reads after the original execute callback returns is still unproven.** Observer v2 remains unchanged and makes no completion claim.

This investigation used only the existing local ELF and source. It did not execute native functions, add hooks, change reference counts, access the emulator or physical deck, or install anything. The evidence below is a set of minimal anchors, not a firmware dump.

## Exact inputs

- Firmware tag: `ep147-9e9454e357389ece`, MD5 `8daa1de55d7328f458f6d855cdc3cdde`, SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
- Core source reviewed: `3e0c5cb010454228f4ee63226b60d060494eeaae`. Unrelated local stems changes were present; the browser files referenced here were unmodified.
- Public SDK header reviewed: `include/azmod-mod.h` at `6563e8e657a4ef113696de9d0f9327a6c8d0fab7`.

Addresses below are native firmware virtual addresses, not runtime addresses in a community mod.

## Caller and execution thread

The exact public-facing native caller is `BrowserEventFacade::setTargetMedia` at **`0x2046278`**. Its facade RTTI is `0x316b2a8`; the queued closure RTTI at `0x316c0b8` explicitly names `BrowserEventFacade::setTargetMedia(MediaID const&)` and its lambda.

| Anchor | Observed behavior |
|---|---|
| `0x2046298..0x20462b0` | Compares the calling pthread against the singleton's stored thread at `+0x10`. The first helper, `0x249b0f0`, resolves through its PLT relocation to `pthread_self`. |
| `0x2503090`, `0x2502dd0` | The singleton getter loads `0x40443a8`; its constructor records `pthread_self()` at `+0x10` and uses the literal `JUCE Message Thread`. This supports identification as the JUCE message-thread gate. |
| `0x204654c..0x2046558` | On the matching-thread path, reads the handler from facade `+0x58` and calls media wrapper `0x2020688`. No local handler-control increment appears in this call sequence. |
| `0x2046400..0x2046414`, `0x2046464` | The other path copies MediaID and the raw facade pointer into an `AsyncTaskBoxBase::AsyncTask` object and submits it through `0x2377330`. |
| `0x2046ed0`, call at `0x2047018` | The queued task's execute body obtains that facade pointer from task `+0x20`, obtains its handler at `+0x58`, and invokes the same `0x2020688` wrapper. |

This establishes the native direct-or-queued routing. It does **not** establish that Linux TID equals PID in a running instance, prove all destruction uses that thread, or exclude nested execution. The observer's runtime thread gate remains necessary and untested.

## Event creation and owner transfer

The media wrapper is **`0x2020688`**. It uses factory **`0x2018f48`**, which allocates a `0xa0`-byte MediaChangeEvent, calls constructor `0x1ffb678`, and returns an owning event pointer through the AArch64 indirect result register. The wrapper keeps that pointer in a stack slot and passes the slot's address to **`0x201f5d8`**. If the executor has not consumed it, the wrapper deletes it after the executor returns.

**`0x201f5d8` is the ListUpdateHandler executor.** Its function extent is corroborated by the ELF FDE (`0x10b0` bytes), and its diagnostics name `ListUpdateHandler.cpp`. Its first argument is the handler; its second is the address of the caller's owning event pointer. The precise C++ method name is not recovered.

| Anchor | Ownership consequence |
|---|---|
| `0x201f5f4..0x201f610` | If handler `+0x10` already holds an event, the executor first asks its virtual slot `+0x50` whether it may proceed. That predicate's complete reentrancy contract is not proved here. |
| `0x201fd60..0x201fd78` | Clears the caller's pointer, places the new event in handler `+0x10`, then invokes the previous event's deleting destructor. This is an owning-pointer transfer, consistent with unique ownership. |
| **`0x201fd80..0x201fd8c`** | Loads the current event's vptr and calls **slot `+0x48`** with the event as `this`. For MediaChangeEvent, that slot is `0x1ffb6c8`. This is the exact executor call observed by v2. |
| `0x201fd90..0x201fd94` | Checks the returned result against `-1`. The shown non-error paths retain the event in handler `+0x10`. |
| `0x201fe84..0x201fe98` | On the error path, clears handler `+0x10` and deletes the current event **after** the virtual call has returned. |
| `0x201ecf8..0x201ed20` | The handler destructor itself clears and deletes handler `+0x10`. |

The request-agent shared controls allocated in this executor are **not** a second shared event reference. The agents receive event pointers; the event's own BrowserData pair at `+0x58/+0x60` is separately strongly owned. Neither fact creates an independent event lifetime pin for an observer.

The handler also has normal shared owners. Construction at `0x2096d38..0x2096da4` creates its inplace control (`vptr 0x317c0d0`), stores a data/control pair in the constructing owner, and assigns the facade's `+0x58/+0x60` pair. Control dispose `0x2098368` reaches the handler destructor. Facade destruction at `0x20426b8..0x20426e4` releases its handler control. **The presence of those owners is not proof they cannot be released or replaced during a nested callback.**

## What is still missing

On the ordinary non-reentrant path, the native executor retains the event across its virtual call; an error deletes it only after that call returns. That is stronger evidence than an unowned raw event pointer. It is still insufficient to authorize a post-original read in our wrapper:

1. The executor does not take an independent stack-held event reference before calling it. Its ownership remains in mutable handler `+0x10`.
2. Another accepted invocation on that handler can replace and delete the previous event. This research has not proved that callbacks reachable from `MediaChangeEvent::execute` cannot re-enter the executor, trigger handler destruction, or yield to nested dispatch.
3. The direct and queued media callers use the facade's handler pointer without acquiring an additional local handler owner. Their complete lifetime relationship to facade/model destruction and AsyncTaskBox cancellation has not been established.
4. Message-thread routing alone does not exclude reentrancy. Nor does a destructor notification or a successful atomic flag check prevent destruction beginning immediately afterward on another thread.

**The unresolved question is therefore exact:** what native ownership or serialization rule prevents the active handler/event pair from being replaced or destroyed from execute entry until the wrapper has finished its own return path? Until that rule is proved, or a service returns an owned value copy, v2 must retain its entry-only reads and scalar-only result record. Do not infer a reference-count operation from the visible shared-control layout.

## Existing core and SDK routes

| Route | What is available | Why it is not currently a completion API |
|---|---|---|
| SDK `view()`, `deck_info()`, `deck_grid()` | Copied/atomic view and deck values. | No committed browser MediaID or correlated USB-switch result is exposed in the reviewed header. Deck source identity would describe a loaded deck, not the browser. |
| Core `playlist_editor.c` `plsnap_take` / `plsnap_get` | `lsfind_hook` copies selection, media and playlist values during native list-state lookup; `plsnap_get` returns a value copy under a mutex. Definitions are at lines 568–648; fields are in `azmod_internal.h` around line 555. | It captures lookup context. It contains no committed BrowserData identity comparison, media-change event generation, or completion status. A mutex makes the stored copy consistent; it does not make that lookup the current committed browser source. It is internal, not an SDK service. |
| Core `mediaobj_find_hook` | Records which per-media object native lookup returned for each MediaID, later joined to list-state lookup by object identity. Firmware lookup is `0x1fb2ef0`; list-state lookup is `0x1fb25f8`. | MediaChangeEvent calls the per-media lookup at `0x1ffb7cc`, **before** committing BrowserData at `0x1ffbaf8..0x1ffbb0c`. A requested-media lookup is not evidence of a successful switch. The list-state search in that section is inlined; this note does not claim `plsnap_take` itself necessarily fires before that commit. |
| Core `folder_preview.c` `fp_lower_reply_hook` | Already owns MediaChangeEvent's lower-list reply slot `+0x88`, among six event classes. Original is `0x2016070`; the hook can reach the event's BrowserData and native display creator. | This is a reply boundary, distinct from execute slot `+0x48`. It currently publishes pages when needed; it exports no copied committed browser state. It does not establish an ownership contract for reads after an unrelated execute callback. |

An **entry-time copy inside the already-owned lower-reply path** is a possible direction for future core research, because native code is then supplying a live event argument. It is not an implemented or fully proved completion service: the reply's cancellation/lifetime rules, association with the requested switch, stale/replaced events, missing replies and already-current/error paths still need an explicit contract. No additional hook claim should be inferred from this note.

`ep_mem.h` also makes the limitation explicit: `own_q` is an aligned direct load whose caller must already know that the object is live. It is neither a lifetime pin nor a fault-safe process-memory reader. Reusing it does not resolve the missing ownership proof.

Here, “committed identity” means the MediaID fields at BrowserData `+0x1e0..+0x1e5`. It does not mean all browser rows, artwork or previews have finished loading. MediaChangeEvent writes those fields before the later page/extras reply lifecycle described by `folder_preview.c`; even a safely copied matching identity would need a separate readiness definition.

## Result and next proof boundary

No currently supported or already-exported core route was found that returns a copied **committed browser identity plus a correlated switch completion**. The identified native executor, thread gate and internal snapshot sites narrow the next investigation. They do not authorize activation or restore post-call dereferences.

A future investigation should establish the AsyncTaskBox/facade/model teardown and nested-callback contract, or design a core-owned value snapshot at an independently owned native callback entry with an explicit stale/cancelled/unknown state. It should leave native activation deferred until both parent-mode validation and completion correlation have been proved. This note adds no runtime validation; the existing host/ARM64 checks in [README.md](README.md) still apply only to the unchanged v2 observer.

# USB browser observation probe 0.0.1

**Emulator only. This build cannot activate USB1 or USB2.** It uses the eventual `usb-buttons` mod ID so later revisions can replace it after a process restart instead of creating competing hook owners. Do not load this beside another `usb-buttons` build or copy it to a physical USB.

The standalone entry is `usb-browser-probe.c`; reusable code is `usb-backend.c` and `usb-backend.h`. No core files were changed. The manifest, descriptor and runtime check all require `ep147-9e9454e357389ece`. Four expected native prologues must match before the first claim.

## Native calls and lifetime

All four hooks claim through the SDK from init; all always forward to the SDK-provided original trampoline. The SDK publishes that trampoline before making its branch reachable. Partial claim failure returns an init error, stops the worker, and leaves already installed hooks forwarding until EP147 restarts. No forced patching or unpatching is attempted.

| Address | ABI preserved | Observation |
| --- | --- | --- |
| `0x250b0e0` | `void(void *self)` | Calls native timer body first, then handles a bounded observation request. The core's existing timer vtable hook remains intact. |
| `0x1c32f40` | `void(void *controller, void *model, int row)` | Captures the callback's live controller/model before forwarding all three arguments. It does not dereference after the original, which may destroy the object. |
| `0x1c32130` | `void(void *controller)` | Clears a matching cached controller before the native destructor runs, including while disabled. |
| `0x1c313c8` | `void(void *self, void *widget, void *a2, void *a3, void *a4, void *a5)` | Forwards all six arguments; captures only after normal constructor completion. The mapped caller retains self separately and ignores the return register. |

Constructor capture depends on load timing. If construction happened before hook installation, a native SOURCE row interaction supplies the controller. This build does not search heap memory for it.

**Precondition:** native GUI callbacks and controller destruction belong to EP147's message thread, whose Linux TID equals its PID. The probe records observed TIDs and becomes inert on a mismatch. That detection is not a memory-safety guarantee if the native program violates its ownership contract by destroying the same object concurrently on another thread. Such a run is unsupported and must not progress to activation.

Native pointers are dereferenced only on that message thread, using live callback provenance or the destructor-tracked controller. Vptr, model relationship, row-count and allocation bounds are checked. At most 16 rows and 63 label bytes per row are copied. Undefined row padding at `+0xe/+0xf` is never read. Native row labels are diagnostics, not owner names or USB identity proof; a fixed row index or matching label must never select a port.

## Public backend interface

- `usp_backend_init(core, mod_id)` is called once from the owner's init, after its other fallible setup. Zero means all hook claims succeeded; runtime observation is still unverified.
- `usp_backend_tick()` is called by the owner's tick. Its absence idles observations after at most 2.5 seconds; installed hooks still forward.
- `usp_backend_enable(0/1)` changes observation immediately. It does not remove hooks.
- `usp_backend_snapshot(&snapshot)` uses trylock: `USP_OK`, `USP_BUSY`, or `USP_NOT_READY`. Copies contain values only. Guard failures and lifetime/generation changes immediately clear old rows even before the next timer publication.
- `usp_backend_request_observation()` requests a fresh message-thread sample.
- `usp_backend_request_port(1/2)` **always returns `USP_OBSERVATION_ONLY`**. Other ports return `USP_BAD_PORT`. There is no selection or activation implementation in this build.

A snapshot includes callback TIDs/counters, capture generation, status, row count and descriptors (`kind`, `media_valid`, `media_id`). `last_clicked_row` is only the last observed callback argument. Focus/active flags describe source-row presentation; none of these alone confirms the active browser library. The synthetic test's media IDs do not establish physical port mapping.

## Diagnostics and validation

The worker requests observations every 500 ms while tick is alive. It logs changes only, capped at 128 batches per process, with at most 16 row lines per batch. No logging, file I/O or heap allocation is performed in the native observation callbacks. Their first thread-identity check uses one cached `gettid` syscall per thread.

Core `C->log` messages appear in `/tmp/azmod-community/usb-buttons.log` when `/home/root/settings/azmod-logs.on` exists **inside the emulator**. The core checks this flag at load and about once a second. Enable it before the run to retain initial observations. Hook-claim/load errors are logged by the core even when ordinary diagnostics are off. This repository does not include a helper that changes the flag or installs the probe.

From this folder:

```sh
python3 ../../../../az-mods-sdk/tools/azmod-pack build .
./tests/run-focused.sh
```

The ARM64 build and official SDK package validation pass. Focused tests pass 11 scenarios under plain, ASan/UBSan and TSan: **4,295 assertions per pass**. They cover original argument forwarding, partial claims, firmware/prologue/worker rejection, destruction during a callback, immediate stale-snapshot invalidation, row bounds, activation refusal, and a real concurrent snapshot reader during 2,000 timer publications. The host test mocks native objects and SDK hook installation; it does not run the diagnostic worker or native firmware, and TSan does not establish native thread ownership or hook safety. The generic SDK host harness cannot execute these fixed ARM64 native addresses.

Runtime hook acceptance, constructor capture timing, real port descriptors and actual callback TIDs remain for the parent task's emulator test. No emulator installation, restart, controls, physical USB writes, library writes, release or catalog publication were performed for this probe. The existing two-USB manual retention result is in the parent research README; it does not validate this new adapter.

## Local runtime observation, October 17

The parent task loaded this exact build into the emulator, then removed its ID from both emulator enabled lists and restarted to unload its hooks. Diagnostics were restored to off; the existing core hashes were unchanged. [Runtime result](evidence/runtime.json) records the scope.

All four hook claims were accepted. Constructor capture worked at startup; observed constructor, timer and source callbacks shared the process/main-thread ID. Destruction was not exercised. Native source rows reported USB1 as kind 1 / valid 1 / MediaID `0x0102`, and USB2 as kind 1 / valid 1 / MediaID `0x0122`. Manual taps followed by Local Library reached the matching USB browser. These observations are not an implementation of automatic activation or proof of physical port mapping.

In particular, a row's presentation flags followed SOURCE selection while the native checkmark could still identify the other active browser source. A future adapter must verify actual browser activation separately. There is still no button UI or activation path, and nothing from this probe was copied to the physical USB.

# Waveform knob entry observer — unpublished experiment

This probe observes the exact-firmware Waveform handler identified in [the static boundary review](../direct-knob-boundary/). It is for the localhost emulator only. It does not open a menu, consume or inject input, call a navigation function, or modify either core. It has not been installed on the physical deck.

The handler at `0x1b3ab60` has four live integer arguments; its return class remains opaque. [The AArch64 entry wrapper](abi/entry.S) saves incoming argument registers, vector arguments, the indirect-return register, LR, stack position and status state, calls a nonthrowing scalar-copy helper, restores the state and tail-branches to the original. Native return values and exceptions go directly to the native caller. The original target pointer is loaded with acquire ordering. `x16` is ABI-permitted transfer scratch; the core's island also uses `x17`. This is not a promise to preserve non-ABI uses of those registers.

Only the process main thread can write the fixed SPSC record queue. Unexpected thread entry disables observation; native forwarding continues. Records contain opaque `x0`, `x1`, `w2`, `w3` and caller values. The observer never dereferences a native argument and never reads a native object after a call. The logging worker drains at 500 ms intervals, stops at 128 records, and suspends observation after five unchanged tick samples. This heartbeat is a diagnostic lifecycle limit, not a menu cancellation or input-ownership contract.

Initialization requires exact opt-in `AZ_WAVEFORM_KNOB_OBSERVER_EMULATOR=1`, the exact SDK firmware tag, size-gated SDK services, matching immutable entry instructions and vtable slot, a worker, and an accepted exclusive hook claim. The pinned late-hook installer copies only the first position-independent instruction through a branch island. It refuses a late install without an island; the conditional branch at entry +8 is not relocated. A failed claim does not retry, replace another hook or weaken any watchdog.

## Verification

- [Installation-boundary verification](install-boundary-result.json) binds the immutable firmware/table to pinned core source and the one-word late-hook path.
- [Host results](host-results.txt): 12,314 assertions per plain, ASan/UBSan and TSan mode. This includes 2,000 concurrent queue transfers, overflow, unmapped opaque argument values, unexpected threads, heartbeat suspension/resumption, budget exhaustion and initialization failures. These tests do not execute the assembly.
- Actual AArch64 assembly execution and instruction/unwind audit are recorded separately under [abi/](abi/).
- [SDK build result](build-result.json) records the ARM64 binary and source hashes. A successful build is not runtime or physical timing validation.

Run the host checks with `AZ_SDK=/path/to/az-mods-sdk sh tests/run-focused.sh`; run the actual ARM64 fixture with `python3 abi/run-tests.py` using its pinned local Docker image. The normal SDK `azmod-pack build` compiles and validates the probe. It is not in a release or the catalog.

## Gate for a direct menu

A controlled native knob sequence may establish which arguments correspond to direction and which dispatch path reaches Waveform. It cannot establish counter-update ordering, queued-event ownership, click/BACK release behavior or safe handback. No input may be consumed from this observation alone.

Independent source review identified two additional schedules a future direct-menu implementation must test: `mixer_browse_knob` samples the old watchdog count before the same frame refreshes it, and `ov_take_menu_handoffs` currently applies pending movement/click before consuming a close request. Test threshold crossing/recovery and simultaneous navigation/click/turn in both orders. Preserve native byte 35 behavior, counter wrap, held releases, modal precedence and SOURCE-row ownership separately.

Private backups, native screenshots, raw logs and runtime scripts stay ignored under `runtime-private/`; generated binaries stay ignored under `out/`. Any runtime evidence must record full baseline restoration, same-process observations and limits before this probe is removed.

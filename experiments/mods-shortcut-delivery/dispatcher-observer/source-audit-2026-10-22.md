# Dispatcher observer negative-result source audit

October 22, 2026. This read-only follow-up found **no identified source or generated-code defect that explains the absent records**. It does not verify that the installed entry was executed. The [runtime result](runtime-result.json) remains inconclusive: accepted claim and initialization, no captured entry, and later reported observer-only status. This audit used saved files and local Git objects only; it made no emulator, physical-deck or network contact and repeated no test loops.

## Inspected identities

The development baseline was `c3e4228`. The core checkout is pinned to `3e0c5cb010454228f4ee63226b60d060494eeaae`; the three core files below were compared byte-for-byte with `git show` of that commit. Existing unrelated upstream changes were not read as substitutes for these files or modified.

| Input | SHA-256 |
|---|---|
| `observer.cpp` | `5a47397574facad309bffda37129aaaab4bfa696661e4f0105468380f73bbaa8` |
| `abi/entry.S` | `a1f1cf42cc587c3222d299ac9dd31bfd0944f1ea51ec98a19d74ba40088a5034` |
| Saved observer shared object | `c791d1f289cc0bad1b912774476e192c1b1614d3f9fa9da42899f679f49edc5e` |
| `src/azmod/inline_hooks.c` | `f141b383c25e66c8acb1839280832a612a6ee0e25e9ada59f374ccbef8f81908` |
| `src/azmod/community.c` | `4b2b72e2b623dcb1ecb0ef108a473815adab45a516a6aa13adb6cce512b80866` |
| `src/azmod/sdk_ui.c` | `5ce1d9356a45fc27bfc3f8957f0cf7d85a0e7ff1496f2130aa93fc3f538d68df` |
| Original UI core in the saved baseline archive | `f8d4ba67ccad9e3386c78dc2ca377bf3263221875d65578a1d893e9c2f3f5668` |

The saved original core hash matches the runtime baseline receipt. That ELF has no named function symbols available for this audit, so this is an audit of the pinned core implementation, not a new byte-for-byte identification of every installed core function. The complete observer ELF was disassembled locally in memory; it matches the recorded build hash. No firmware/core dumps or binaries are included here.

## Acceptance, forwarding and lifecycle

In pinned `community.c:1226–1273`, `cm_claim_hook` enforces init-time ownership, firmware permission, executable range and overlap checks, serializes installation, and reports success only after `azm_install_hook` returns `1`. It is not merely a queued claim. `inline_hooks.c:274–342` constructs and seals the original trampoline, writes and flushes the island, publishes `orig`, writes the function-entry branch and flushes its instruction cache before returning success. Late installation refuses the four-instruction fallback when no island is available. The selected prologue's first instruction is position independent; the island path copies only that instruction, so its fourth-instruction ADRP is not relocated on this path.

The publication sequence is specifically a relaxed store to `orig`, a release store to the entry branch, and instruction-cache maintenance (`inline_hooks.c:334–337`). The wrapper acquire-loads `orig`. This is the core's intended machine-code installation protocol; it is not a matching C++ release/acquire pair on `orig`, and this audit supplies no separate concurrent-installer or instruction-cache proof. The retained run did not save a readback of the patched entry, island target or trampoline. Acceptance must not be promoted to proof of executed forwarding.

The built wrapper has a direct call to the local observation helper before restoring inputs and tail-branching through the local original pointer. Its five inputs and LR use the reviewed shape; incoming `w4` remains intact and original LR enters helper argument six. The linked helper contains acquire gate loads, a signed-normalized Linux `gettid` syscall, explicit mailbox/queue writes and `errno` restoration. The symbols are local to the shared object; the source and compiled code provide no alternate exported helper that could silently capture elsewhere. The native result is not interpreted. These are static properties of the actual build, not a runtime invocation check.

`observer.cpp:184–195` sets the core and PID, starts the worker, claims the hook, verifies the original pointer, then release-publishes `state=1`. An entry during the narrow claim-before-ready interval can legitimately see state zero and forward without recording. A failed claim sets state negative and returns failure. The core marks the mod loaded only after successful init (`community.c:1846–1863`). Libraries remain mapped after `dlopen`, including init failures (`community.c:1804–1808`); normal menu visibility changes do not unload this observer.

The SDK worker service creates a detached normal-priority thread and returns success only after thread creation succeeds (`sdk_ui.c:286–339`). Its wrapper calls the observer worker directly. No per-mod frame-filter quarantine is applied to this worker or inline hook. A successful thread-start receipt still does not prove subsequent worker progress.

## What a completed capture would leave behind

The following deductions assume the saved status reads addressed the intended module correctly and the values have not wrapped. They are not independent verification of those reads.

| Observer path | Persistent evidence after the gates |
|---|---|
| `state != 1` or `live == 0` (`observer.cpp:66–67`) | Silent return; no attempted-entry counter exists |
| Failed TID (`:68–69`) | Increments `tid_failures` |
| First valid off-main TID (`:70–81`) | Mailbox transitions from EMPTY through WRITING to READY, later CONSUMED; never returns to EMPTY |
| Later/contending off-main TID (`:72–74`) | Increments `off_skipped` |
| Busy/reentrant main producer (`:84–87`) | Increments `nested_dropped` |
| Main producer (`:94–100`) | Increments the 64-bit sequence before queue-full checking; overflow additionally increments `dropped` |

Thus ordinary off-main delivery alone cannot explain every reported field remaining zero: the one-shot mailbox intentionally covers it. Queue overflow, normal draining, log suppression and a worker that never drains also cannot erase a completed main capture's sequence or reset the off-thread mailbox. The worker consumes queue indices but never resets the sequence. The queue uses release/acquire publication and reuse indices; the mailbox uses release/acquire publication and has no reset/reuse path. No missing publication edge was identified within these contracts.

The helper accesses `errno` before its gates and uses thread-local lookup/gettid after them. There is no unconditional entry counter before those operations. First-use TLS/runtime cost and progress were not measured. This leaves an instrumentation gap; it is not evidence that TLS or the syscall failed during the run.

## Tick and logging do not fill the historical gap

The core calls loaded mods' tick functions at the overlay's approximately 33 ms cadence (`community.c:2102–2130`). This observer's tick only increments heartbeat (`observer.cpp:210`). The worker samples it every 500 ms and silently sets `live=0` after five unchanged cycles; later progress restores `live=1` (`observer.cpp:140–171`). Consequently, a progressing heartbeat proves recent tick activity, not worker progress or the gate state at earlier encoder input. `live` also starts at one before the worker runs. The status receipt cannot exclude an earlier quiet interval followed by recovery.

Ordinary SDK logs require the shared logging flag, a permitted caller and available file capacity (`community.c:1174–1222`); write failures can also suppress them. A ready log shows logging worked at initialization, not necessarily at every later instant. Nevertheless, logging failure by itself would not explain zero sequence/mailbox/error state after a completed capture. The entry budget is applied while draining, after capture; it likewise cannot erase those traces.

## Status-sampler verification limit and load-bias trap

The sampler was an inline local command, and no standalone implementation or saved map listing was found in the retained experiment files. Only its receipt is available here. **This audit did not reproduce or independently verify its live reads, load-bias calculation, widths or byte decoding.** The reported changing heartbeat is consistent with the intended field but is not a substitute for that missing provenance.

Static inspection of the exact observer ELF establishes the following offsets; these are ELF-relative values, not runtime addresses:

| PT_LOAD | File offset | Virtual address | Flags |
|---|---:|---:|---|
| Text | `0x0` | `0x0` | RX |
| Data | `0x1d50` | `0x11d50` | RW |

An offset-zero RX mapping supplies the load bias directly. The RW segment has `p_vaddr - p_offset = 0x10000`: blindly using its map start minus file offset as the bias yields a value too high by `0x10000`. Any future reader must match mappings to PT_LOAD segments, account for page alignment, then read `bias + st_value`; section/file offsets are not interchangeable with symbol virtual values.

The sampled state, live, heartbeat, queue indices, off-thread state and four diagnostic counters are ordinary local **4-byte** objects in data/BSS, not TLS objects. Main sequence is an **8-byte** local BSS object. TID caching is separate TLS and was not part of the reported status. Sequential scalar reads remain noncoherent even when address calculation and widths are correct.

## Consequence for the next investigation

No source defect or normal queue/logging behavior found here closes the negative-result gap. Evidence still needs to distinguish input modified or consumed before native dispatch, a different native route, a silent observer gate, and an instrumentation/installation uncertainty. The native owner-thread predicate is separate from Linux PID/TID equality, as the [thread-predicate review](thread-predicate/) explains; its live result remains unobserved. This audit does not choose a new runtime hook, establish input provenance or authorize native input consumption. The historical negative result and completed cleanup remain unchanged.

# Encoder dispatcher observer: accepted claim, no captured entries

October 22, 2026. `encoder-dispatch-observer` 0.0.1 is an unpublished passive entry observer for the exact `ep147-9e9454e357389ece` firmware. The host logic, actual AArch64 wrapper and SDK build pass their separate checks. **One localhost emulator claim succeeded, but the bounded empty-Waveform input sequence produced no entry records.** The [runtime evidence](runtime-evidence.md) records that limit and verified restoration of the fresh 146-file baseline. The earlier refused [Waveform observer](../direct-knob-observer/) is unchanged.

The normal SDK hook request targets function entry `0x1adeee0`. Initialization checks its first four instructions and the immutable secondary Waveform slot `0x2d61d98`; it does not claim or modify the slot. [The binding verifier](verify-boundary.py) passes 11 checks against the exact private ELF and [existing dispatcher boundary evidence](../dispatcher-boundary/). That evidence separately establishes instruction-policy eligibility. The subsequent emulator run accepted this one exclusive claim and reported one installed hook. That establishes this installation only; it does not establish which native input route invokes the entry. Refusal still ends initialization without retry, takeover or policy changes.

## Capture and forwarding

[observer.cpp](observer.cpp) receives five scalar inputs (`x0`, `x1`, full-width `x2`, low `w3`, low `w4`) and original LR from [abi/entry.S](abi/entry.S). It copies those values with thread identity and a lane-specific sequence. It never dereferences those native arguments, inspects a native object after return, or interprets a scalar as knob identity, delta, accepted input or completion. Only initialization reads fixed immutable firmware text/table bytes.

The generic dispatcher can serve different views and can dispatch synchronously or queue a task. A captured entry therefore does not establish Waveform identity, live knob provenance, counter ordering, target execution, view lifetime or event ownership. Native execution always tail-forwards through the original target, including when observation is disabled or dropped. There is no return-side observer or guessed native return type. The [ABI contract and tests](abi/README.md) describe register, stack, return and unwind coverage and its limits.

## Bounded observation lanes

The process main thread is identified by `gettid() == getpid()`. The separate [58-anchor predicate review](thread-predicate/) proves that native dispatch instead compares `pthread_self()` with an owner object's stored identity; no equivalence between that native identity and the observer's main-thread test is established. Its 32-slot SPSC queue holds at most 31 records. A nonblocking atomic producer guard drops a nested observation instead of changing the queue or sequence concurrently. Sequence numbers belong only to this lane; overflow can leave gaps. Dropping observation never consumes native input.

A different valid thread can win one independent mailbox using `EMPTY -> WRITING`. The winner copies all record fields before release-publishing `READY`; the worker acquire-loads that state before copying and then marks it `CONSUMED`. This mailbox is never reset or reused, so later writers cannot replace its identity or race the worker copy. Contending and later writers only increment a skip counter. Its `main-seq=0` has no main-thread or global ordering meaning. The worker drains this mailbox before the main queue; log order across lanes is not event order.

A failed or out-of-range signed TID result becomes zero, drops that observation and remains uncached so a later call can retry. Successful per-thread IDs are cached. The helper saves and restores `errno` on every path, including failed TID lookup, queue overflow, mailbox contention and nested observation. Independent review caught the original unsigned conversion of syscall `-1`; the corrected production normalization helper is directly exercised by the host tests.

The worker checks the mod tick heartbeat every 500 ms. Five unchanged cycles suspend capture; later tick progress resumes it. The two lanes share a lifetime budget of 128 logged entry records, plus at most eight changed-counter diagnostic summaries. The next popped record after that entry budget disables capture and ends the worker. Initialization and the budget-stop message are separate bounded messages. A non-EINTR sleep failure also disables observation. The source does not register a frame filter or change a watchdog.

There is no explicit heap allocation, SDK log call or blocking wait in the production observation path. **Allocation is not instrumented by the host tests:** the full-queue test checks logs and `errno`, not allocation, despite its compact source comment. First-use dynamic TLS, `errno` access and `gettid` syscall/runtime costs are not bounded or excluded. Lock-free atomics and bounded copied data do not establish device timing, signal safety or whole-process lifetime safety.

## Validation and reproduction

[Host results](host-results.txt) and [source-bound metadata](host-result.json) record **16,686 passing assertions in each of plain, ASan/UBSan and TSan**, across 21 fresh-process scenarios. Tests include lifecycle/refusal gates, full-width scalar/LR capture, seeded `errno` with deliberate helper/TID clobbers, signed TID failures and recovery, queue overflow, eight-writer mailbox contention, a deliberately paused partial mailbox write, no mailbox reuse, busy/reentrant producers, later main capture, heartbeat and logging limits, and 2,000 concurrent SPSC transfers. SDK services and TID lookup are modeled; the production normalization, observation, queue/mailbox and worker-cycle logic are exercised.

The separate actual AArch64 fixture passes **55 assertions**. Three strict mutated assembly controls distinguish missing register restoration, the old LR-in-`x4` capture and truncated `x2`; they produce exactly 8, 7 and 2 expected failures. Those tests exercise the wrapper with synthetic native targets, not the SDK island or real dispatcher.

[The SDK build](build-result.json) produced and validated an AArch64 shared object with SHA-256 `c791d1f289cc0bad1b912774476e192c1b1614d3f9fa9da42899f679f49edc5e`. The [sanitized build log](build-log.txt), source hashes and image identity are retained. The binary is excluded from Git. Host binaries, raw build/test snapshots and runtime-sensitive data remain in ignored directories. Build and [independent-review metadata](independent-review.json) describe the pre-install state; the later [runtime record](runtime-result.json) is separate. Root independently reproduced the full host matrix with identical results and source fingerprints. A first binding-verifier invocation used an incorrect local ELF path and stopped before verification; the corrected absolute-path invocation produced [boundary-binding.json](boundary-binding.json), without source changes.

From this directory, with the existing SDK available:

```sh
AZ_SDK=/path/to/az-mods-sdk sh tests/run-focused.sh
python3 verify-boundary.py --elf /private/path/EP147 --result /private/path/binding.json
python3 /path/to/az-mods-sdk/tools/azmod-pack build .
```

The ABI runner has its own reproduction command in its README. No command above installs a mod or connects to a deck.

## Runtime result and remaining scope

The explicit opt-in is `AZ_ENCODER_DISPATCH_OBSERVER_EMULATOR=1`; it is a safeguard, not proof that a process is an emulator. Any future run must separately establish localhost-only emulator identity, ownership and a fresh preservation/restoration plan. Readiness requires successful current loaded status and accepted hook evidence; finding the mod ID as a substring of an error is insufficient. Original target publication and module lifetime depend on the normal SDK contract; removal requires the process to be stopped. Raw runtime logs containing copied addresses remain private.

The accepted hook produced zero entry or diagnostic records before input, after navigation, or after one clockwise and one balancing counterclockwise detent. The host-injected counter returned from 51 through 52 to 51; that is not a sampled native counter. Later sequential reads of only this observer's own status showed capture enabled and heartbeat advancing, with empty queues and zero recorded drops/errors. Those reads are not a coherent snapshot or proof that capture was enabled at each earlier input. The result does not distinguish an unused secondary entry, an upstream input gate or another delivery path. Trace the actual input registration/call route and distinguish primary from secondary dispatch before another probe; do not repeat the same empty-Waveform sequence unchanged. No native argument, view or queued-task pointer was dereferenced.

This source adds no menu, native input suppression, focus handback, view cancellation, USB activation, stems recovery or community release. It neither repairs filter quarantine nor weakens any protection. Native event ownership, caller/thread/input provenance, queued completion and cancellation, interaction with other hooks and hardware timing remain open.

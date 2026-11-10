# Offline terminal fault-attempt fixture

This separately versioned C11 fixture models one process-lifetime fault attempt. The [reviewed plan](../activation-fault-attempt-plan/README.md) and [source ledger](source-ledger.json) define its scope. **All live gates remain CLOSED. Ownership is not fault causation.**

One strong CAS admits a winner into ActiveIncomplete before any authored provider event. A suppressed caller does not touch the winner's working result. Only after every selected branch call returns and its receipt is recorded does the winner copy the final payload and release-publish OrdinaryReturned. Public observation acquires state once and exposes payload only after completion. There is no reset, application/provider retry, reopen or reclaim path in the candidate.

The winner copies a resolved Community index, process identity and canonical zero-padded 33-byte ID, or accepts NoCommunity. These are authored values. One-character and 32-character IDs test bounded copying; this is not SDK ID approval. Native ownership selection, native frames, module/DSO lookup and generated-range attribution are not executed here. NoCommunity invokes only an opaque authored event.

Open fd zero is valid. An ordinary failed open completes with its selected owner and no later calls. A short or failed write still proceeds to fsync and close after each ordinary return. Known failure and short write are independent flags. A successful value has no valid failure errno even if its authored script carries a stale sentinel. Errno values are authored sentinels, with no explicit EINTR selector or measured native errno. These classifications do not acknowledge persisted blame or native file success.

Each event has separate entry, raw-value receipt and authoritative record steps. Withholding a step leaves ActiveIncomplete permanently admission-closed; this does not execute a signal, process crash, cancellation or nonlocal exit. The raw oracle is separate from the authoritative working receipt. Tests inspect private progress only while their serial driver is stopped.

All executions use the explicit FA_FIXTURE scheduling seam. The ordinary synchronous fa_run configuration is compile-only. Competing and nested requests are deterministic value schedules on one thread, not concurrent C executions or actual nested callback stacks. The harness writes private evidence reports through stdio; those files are not modeled crash-file providers.


The first completed matrix uses GCC 7.5.0 in the pinned Linux AArch64 image, in plain and ASan/UBSan modes; leak detection is enabled in the sanitizer mode. Each mode passes **269 summed assertions, 55 distinct names, across 52 fresh-process candidate selectors**, plus eight exact safe controls. All 120 processes use authored scheduling; the two standalone objects are compile-only.

The 52 candidate selectors cover 19 ordinary branch/schedule cases, 24 fresh stops at six event kinds and four boundaries, one final prepublication stop, and eight dedicated control baselines. They cover both actual A/B value orders, requests delayed before CAS, nesting at every selected boundary, one- and 32-byte copied IDs, no owner, fd zero, returned errors/short writes, terminal suppression and state-only observation. An independent prebuild review caught that the original reverse-order harness changed only actor labels; before freezing/building it was corrected to reverse actual script inputs and expected owner, retaining both scripts.

| Controlled defect | Required singleton failure |
| --- | --- |
| Admit a stale second contender | terminal-admission-single-winner |
| Reopen an unknown attempt | unknown-attempt-stays-terminal |
| Borrow the caller's ID instead of copying it | selected-owner-copy-survives-input-change |
| Clear ownership after a write error | selected-owner-independent-of-io-outcome |
| Invent an unreturned write receipt | armed-write-without-return-remains-unknown |
| Retry a short write | short-write-retains-single-write-sequence |
| Skip fsync and close after write error | returned-write-error-still-fsyncs-and-closes |
| Treat stale errno as valid after success | successful-return-has-no-valid-failure-errno |

Every control exits with its exact expected assertion failure and empty stderr. The copied-ID control keeps the source array alive and canonical; the two admission controls stop before a second provider sequence. The invented-return control changes the authoritative working receipt while the separate raw oracle remains unavailable. No control executes undefined concurrent behavior or native fault effects.

The [source lock](review-lock.json), [independent source review](source-review.json), [contract](test-contract.json), [source ledger](source-ledger.json) and [sanitized result](result.json) preserve exact tested inputs and outcomes. The packet contains 21 inputs, 237 commands, 474 output streams, 172 build artifacts, 120 reports and 1,271 files including the current checkpoint and both final result projections. The runner's narrower 1,269 count ends at the private final result and excludes its public projection and checkpoint. Raw commands, streams, binaries and reports remain private. There was no compiler/runtime failure or predecessor matrix replay.

[Selected retained codegen](codegen-review.json) shows one logical strong CAS lowered to LDAXR/STLXR with an exclusive-monitor retry loop. The losing branch precedes working initialization. The final payload copy precedes STLR publication, and observation performs one LDAR before the state-2 payload copy. This is **not a bounded or wait-free machine admission path**. Normal direct dependencies include memcpy/memset. The source has no application CAS retry loop, but that does not eliminate the compiler's machine retry.

The evidence does not establish real signal/reentry safety, concurrent admission/publication, lock-free progress, transitive libc/interposition behavior, native errno/file/frame outcomes, arbitrary loader/slot custody, callback quiescence or recovery. Defensive malformed-input paths are source-visible and remain outside this valid-script matrix. The ordinary no-seam entry is not runtime-tested. No attribution, DSO, installer, generated transfer or patched target/replacement code is invoked. Existing consumers, receipts, handlers, quarantine, protections, packages and prior failure packets remain unchanged. No firmware emulator, physical deck, live observer or release action occurred.

Independent [retained-byte verification](verification.json) passed on its first invocation. The frozen auditor was copied before execution and remained unchanged afterward. Its 494,731 structural assertions cover input/command/artifact/report closure, selected raw facts, 56 ELF files and the exact sanitized projection; they are not new runtime cases or a signal-safety proof. No passing suite or prior audit was replayed.

Next make a separately scoped source-only attempt/attribution composition plan. Reuse the retained immutable module/generated-range readers and terminal-attempt implementation to specify one admission authority before selection, exact copied identity handoff, per-slot visibility and no-owner behavior, and private versus completed outcome ownership. Identify a separately named adapter boundary without calling or modifying the frozen volatile-guard consumer or silently adding a second once guard. Define genuinely new composition cases and exact safe controls before any implementation; preserve first-attempt suppression and all quarantine policies. Preserve the compiler CAS retry and libc dependency limits. Keep frame/file providers authored, and defer actual DSO/installer invocation, handler/signal/nonlocal-exit paths, concurrent/native publication, production integration and live observer composition. Do not replay this passing matrix, its byte audit or predecessor suites unchanged; all live gates remain CLOSED.

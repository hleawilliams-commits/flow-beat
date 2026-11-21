# Offline ordinary-entry witness

This separately versioned C11 fixture executes the ordinary synchronous `ac_run` driver against stable authored data. Its first matrix passes **103 summed assertions per mode, 31 distinct names**, over **15 candidate selectors per mode** (ten ordinary cases and five control baselines), plus **five exact singleton controls per mode**. The two modes are plain and ASan/UBSan with leak detection, for **40 fresh processes**.

The predecessor's successful winning entry was compile-only; this experiment supplies focused execution of that ordinary entry. It does not supply native providers or signal safety. **Every live gate remains CLOSED.**

## Scope and construction

The [retained plan](../activation-ordinary-entry-plan/README.md) was frozen before this implementation. [source-transform.json](source-transform.json) covers every preserved and replaced core/header byte. [source-ledger.json](source-ledger.json), [review-lock.json](review-lock.json) and [source-review.json](source-review.json) freeze the implementation, harness, controls and runner before the first build.

`AO_WITNESS_SETUP` exposes exactly five module/generated/cell setup functions. Private stepping, inspection, snapshot-refusal, auxiliary and diagnostic APIs are excluded. Old fixture/control configurations are rejected by the header. Both commit functions add a relaxed Virgin eligibility load before existing bounds, staged/used checks and release publication. This is a serial setup rule; it creates neither a second admission authority nor a concurrent writer protocol.

All selected authored slots and cells are prepared before entry. Request/model storage remains stable through return. The engine retains one terminal admission, the ordinary driver, six exact reader fragments and the public acquire observer. The harness records actual entry returns separately from public state/payload and compares fields rather than padding. A generated binding of 0 resolves module 3 with full process identity 0x100000001 and all 33 canonical ID bytes.

## New cases and controls

| Group | Ordinary evidence |
| --- | --- |
| G1 | Direct module, generated owner ahead of another module LR, and one authored exact-eight-byte frame pair complete synchronously. |
| G2 | NoCommunity completes with SELECTION/UNBLAMED even when unused I/O values are outside their domains. |
| G3 | Genuine NULL and open 0/write 33 return INCOMPLETE and expose only Active state. Private partial receipts are source facts, not runtime observations here. |
| G4 | Returned open refusal and short-write/sync error finish ordinarily; the latter retains close and both independent failure/short flags. |
| G5 | Completed/incomplete first entries suppress a second call; post-return caller storage mutation does not change copied completion. Both unused setup commits are refused. |

The five controls omit driving, stop after selection raw return, mis-map published or halted returns, or omit both new setup guards. Each has exactly one declared failed assertion and exits1. Both C5 commit calls execute and are recorded separately before their conjunction. C5 publishes only authored unused slots after completion, then observes the unchanged public result without another lookup. Baseline controls pass unchanged. [test-contract.json](test-contract.json) fixes every name, report field and singleton failure set.

All attempt-state observations use `ac_observe` before entry or after ordinary return. No private in-call instrumentation is enabled. Reporter stdio writes evidence only; it is not a crash-file provider. Repeated-entry input mutation occurs after the first ordinary return.

## Evidence and custody

The pinned isolated image supplies GCC 7.5 Linux AArch64 C11. It has no network, a read-only root and only owned build/report mounts. Two standalone and two setup-only objects compile first; [codegen-review.json](codegen-review.json) binds their fresh method bytes, direct dependencies, setup symbol surface and diagnostic outcomes before runtime. Standalone objects remain compile-only. Candidate links reuse the same packet's setup object; each mode compiles one macro-independent harness object reused by its five controls.

[result.json](result.json) is the sanitized final projection. [verification.json](verification.json) records the first independent byte audit: **28 inputs, 111 commands, 222 streams, 164 build artifacts, 40 reports and 685 packet files**. Its **186,853 structural assertions** are not additional runtime cases. The auditor independently reconstructs authored setup/public reports and checks complete source transformations, exact reader reuse, ELF metadata, command/stream custody, same-packet reuse and both checkpoint projections. It executes no fixture, compiler, prior collector or prior audit.

The auditor was copied before invocation and its hash stayed unchanged. No source file changed after first build. Root and independent peers reviewed the source and method evidence. The core author independently reviewed the harness/runner; a different reviewer covered core/header transformations. That distinction clarifies the combined reviewer-role wording in the frozen source-review input.

## Preserved review history

Before first build, source review corrected the harness's G3 open value from inherited 3 to the planned 0 and corrected an earlier snapshot-member typo/scope literal. Runner review corrected report schema/status mismatches, prior-artifact custody before additive inventory refresh, exact directory closure, timeout partial-output retention and lock cleanup. Earlier runner/ledger/lock and G3 harness drafts remain private. No compiler/runtime/audit failure occurred in this first packet.

Seven sanitizer-object readelf commands produced the known exact FINI_ARRAY metadata warning. Each was classified against its own section, symbol, relocation and 32-byte compiler-finalizer extent using the previously reviewed narrow policy. This is not a general warning allowance, runtime sanitizer finding or tool-defect attribution. All other diagnostics remain refused. The predecessor's initial FINI_ARRAY refusal and empty-directory auditor failure remain preserved and were not replayed.

## Limits and next boundary

Retained codegen shows result copy before STLR and state LDAR before completed payload access. Strong CAS lowers to an LL/SC exclusive-store retry loop. No bounded/wait-free machine admission follows. Normal libc dependencies, canary-failure edges and sanitizer instrumentation remain; neither their transitive behavior nor signal safety is proved.

The fixed authored setup is not arbitrary DSO/slot custody, a coherent module snapshot, native frame lifetime, actual file persistence, concurrent publication or recovery. There is no actual callback/nesting, DSO/installer invocation, signal/nonlocal exit, firmware emulator/device operation, production integration or live observer. Ownership is not fault causation. Quarantine, official cores, previous packages and all live gates remain unchanged.

Next make a separately scoped source-only ordinary-provider boundary plan. Reuse this frozen engine and exact readers to specify a fixed authored callback table and context with stable lifetime through one synchronous entry, admission before request/provider access, exact enter/raw-return/record ordering and terminal suppression of a nested ordinary call. Distinguish callback return, captured error values, authoritative receipts and public completion; define new provider-call and nested-loser witnesses with exact safe controls before implementation. Keep the setup-only surface and single terminal admission, and preserve all existing bytes and failure packets. This next slice is source-only: no callback or nesting execution, native frame/crash-file provider, DSO/installer, signal/nonlocal exit, concurrent publication, production integration or live observer composition. Preserve LL/SC retry, libc/canary limits and quarantine. Do not replay this passing matrix or audit; all live gates remain CLOSED.

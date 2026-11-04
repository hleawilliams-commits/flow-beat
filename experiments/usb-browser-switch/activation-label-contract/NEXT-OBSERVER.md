# Next passive observer: offline entry-only adapter plan

This document proposes the next implementation slice. **No USB wrapper source, new fixture, build, execution, or proof is added by this plan.** It changes no native hook, activation adapter, pending operation, writer, recovery path, or runtime gate. Existing gates remain **CLOSED**.

The purpose is to implement and test two passive register snapshots followed by transparent tail forwarding. It replaces the earlier idea of a correlation scope spanning native action execution with two independent entry records. It adds no native activation call.

## Two entry boundaries, one forwarding design

The targets remain the ordinary entries already reviewed:

| Boundary | Entry | Known native caller LR values | Initial record |
| --- | --- | --- | --- |
| Guarded DeviceLibrary action | 0x1be7e38 | 0x1be8aa0, 0x1be8c00 | Opaque controller bits from x0, kind bits from w1, opaque pointer bits from x2, original LR/SP |
| Named source service | 0x20a1550 | 0x1be81e0 at the final interface call | Opaque receiver bits from x0, exact twelve Source bytes from x1 and low w2, original LR/SP |

The [action ABI review](../activation-abi/ACTION-REVIEW.md) establishes the known caller packing and unresolved return semantics. The [service ABI review](../activation-abi/SERVICE-REVIEW.md) establishes the Source packing and real normal/exception paths. These are static addresses and callsite facts, not new live provenance.

The first adapter must read **no native pointees**. In particular, action x2 remains an opaque address value: copying its bits does not copy the eight-byte carrier it points to. The service value is already present in registers. Reconstruct it byte-for-byte as little-endian bytes 0–7 of x1 followed by bytes 0–3 of x2; preserve the trailing Source bytes without promoting them to identity. The upper 32 bits of x2 are outside that twelve-byte value but remain part of forwarded machine state.

Records should identify the boundary and retain raw values. A record is an entry observation, not accepted navigation, a native return, completion, cancellation, or lifetime ownership. Distinct local record IDs, identical values, same-thread occurrence, ordering, and the known service LR do not prove that two records belong to one action invocation. There is no pending-action TLS, cross-entry depth, retained native pointer, or cleanup requiring a later callback. Any eventual relation must remain explicitly weaker than proven invocation identity.

## Proposed fixed-width entry snapshot

The frozen [task executor adapter](../../mods-shortcut-delivery/task-observer-integration/executor-entry.S) demonstrates a useful source pattern: save state, call a nonthrowing helper, restore original SP/LR, then tail-branch. Its [fixture and evidence](../../mods-shortcut-delivery/task-observer-integration/README.md) cover a different one-input adapter with a 320-byte frame and q0..q7. Those passing checks do not prove a new USB adapter.

For this candidate, use two separately named assembly entry points with one reviewed layout and a fixed **800-byte, 16-byte-aligned frame**:

| Offset | Contents |
| --- | --- |
| 0..247 | Full x0..x30, eight bytes each (x30 starts at 240) |
| 248..255 | Reserved padding, not native data |
| 256..767 | Full q0..q31, sixteen bytes each |
| 768, 776, 784 | NZCV, FPCR, FPSR snapshots |
| 792..799 | Reserved padding, not native data |

Original SP is the frame base plus 800; original LR is the saved x30. Save scratch registers before using them to capture system state. The helper receives only a borrowed pointer to this private frame and a boundary tag. It must not retain that pointer after returning. Match C/assembly field offsets and sizes explicitly; do not publish uninitialized padding as evidence.

After the helper returns, restore the fixed-width registers and controls, then original SP and LR. Load the stable original target through x16 and use BR, with no after callback. The native target then sees the original caller stack and LR. Unknown scalar, pair, SIMD or aggregate-return behavior is left to the native target and caller; the adapter does not assign a C return type or touch a native result.

This is preservation of state **seen at the adapter entry**, with an explicit x16 tail-transfer exception. It does not claim that all caller state survives the installer: the existing island/trampoline also uses x17 as scratch. The [hook-lifetime review](../../mods-shortcut-delivery/task-hook-lifetime/README.md) documents that distinction, the relaxed original-slot store followed by an entry-code store, and the absence of demonstrated unwind registration for anonymous trampoline PCs.

## Helper, CFI and lifetime contract

Each original-target slot must be aligned, nonnull, initialized before its entry can be reached, and stable for the lifetime of all reachable forwarding code and callers. A null target is not an observation refusal that can safely skip the original call. Use a separate capture-ready publication gate; loading an original pointer alone does not prove publication of helper configuration. Closing capture must not reclaim the frame's destination storage, original slot, code, or records still reachable by a producer.

The helper must finish its local work before forwarding. It must be bounded in work, nonthrowing, preserve errno, and avoid native calls, recursion into the observed targets, cancellation points, longjmp, allocation, locks that can wait, formatting, filesystem or network output. A noexcept declaration alone does not satisfy those conditions. If a fixed record sink is included, use preallocated, bounded storage with an explicit drop/refusal result and no overwrite of still-readable records. Disabled, exhausted or contended recording must preserve exactly-once native forwarding. Bounded work is not a real-time latency guarantee.

Correct new CFI is required while the helper runs: the proposed CFA offset is 800 and saved x30 lies at CFA−560. Describe relevant saved registers and restore rules at their actual instruction boundaries. A helper-frame backtrace and a native-target exception are separate checks. After the tail branch, the adapter has no active frame or cleanup obligation; an ordinary exception raised later in the target should unwind through the target's own metadata to the original caller. That design does not prove unwinding from a fault or asynchronous interruption inside an installed island, trampoline, save/restore sequence, or nonthrowing helper.

This slice covers ordinary fixed-width AArch64 GPR/Advanced SIMD and the selected status/control registers. It adds no SVE/SME, PAC/BTI, arbitrary TLS, process-signal, asynchronous cancellation, foreign-exception, stack-exhaustion, or installed-trampoline guarantee. It neither installs signal recovery nor treats a faulting observation as recoverable.

## New discriminating offline fixture

Implement the new adapter and an owned synthetic caller/helper/target fixture separately from frozen experiments. Do not replay the task executor suite or infer USB behavior from its assertion count. The useful new checks are:

1. An assembly caller supplies distinctive GPR, full-vector and control-state values, real stack arguments and canaries. A hostile but AAPCS64-conforming helper changes permitted volatile state, including upper halves of q8..q15 and q16..q31. The target verifies restored entry state, original SP/LR and stack arguments, with the x16 transfer exception explicit.
2. Action x2 names inaccessible fixture storage while the synthetic target deliberately does not dereference it. Capture must retain only its bits. Service capture must reconstruct all twelve Source bytes exactly, including opaque trailing bytes, and forward high x2 bits unchanged. This is a fixture read-boundary check, not permission to pass invalid pointers to firmware.
3. Synthetic targets return high-bit scalar values, register pairs, HFA/vector values and an aggregate through x8. The real caller receives each result after exactly one entry helper and zero return callbacks.
4. Nested target calls and a target-thrown exception exercise real caller/target destructors and catching. Entry records remain independent, with no inferred pairing or TLS cleanup. A later normal invocation still behaves ordinarily.
5. A live backtrace from the helper and static disassembly/CFI checks cover the new helper frame. Keep this evidence separate from target exception propagation, where the adapter frame is already absent.

If bounded capture is part of the candidate, add focused disabled/full/contention cases that preserve native forwarding; do not import an unrelated transport suite. Identical values in nested or concurrent entries must not produce an asserted correlation.

Useful negative controls corrupt only the helper-observed LR or original SP, omit an argument restore, omit full-vector restoration, or omit helper errno restoration. Require named value-oracle failures after clean target return and fixture cleanup. Wrong-CFA/LR-rule controls should be compiled and rejected by metadata inspection. Do not execute intentionally shifted-SP or BR-to-BLR controls whose expected result is a crash, loop, or broken cleanup.

## What the checkpoint would and would not add

A passing future fixture could establish the new assembly/helper forwarding behavior for its synthetic calls and state channels. It would not establish runtime hook ownership, installer publication, actual receiver provenance, a readable action carrier, native event ownership, media availability, source acceptance, or safe navigation. The first records would not prove that the supplied action carrier survives native normalization, because that carrier is deliberately not read.

The immediate next work is this small offline implementation and its own discriminating checks. Writer/recovery readiness, installed-path execution, any richer pointee capture, physical timing and activation remain separate closed gates. No source-level plan or future synthetic pass opens them automatically.

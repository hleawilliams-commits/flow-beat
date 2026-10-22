# Independent static review

The challenger reviewed both verifiers, their sanitized results, `TASKBOX.md`,
`BOUNDARIES.md` and selected exact-ELF function bodies offline. The reviewer did
**not** rerun the unchanged verifiers. The parent independently reproduced both
original results byte-for-byte before the wording correction below, then
independently reproduced the corrected task-box JSON exactly. The earlier
private receipts were retained. No new runtime observation,
ABI wrapper, hook installation or device/network operation was performed.

The material scope correction is resolved in the verifier, result and notes.
The task-box scan excludes direct literal `B`/`BL` targets in three selected
submission bodies. It does **not** establish that an entire call chain never
invokes a predicate or executor through indirect/transitive callees or the
later notification callback. The selected submission slot is `+0x30`, distinct
from the `+0x28` owner predicate. The main verifier's numeric-literal overlap
scan likewise does not resolve computed addresses, other installed modules or
live hook ownership. Passing instruction policy does not establish a claim.

The evidence properly separates task storage, memory-pool references, the raw
dispatcher capture and child lifetimes. A successful queued return is not child
execution or completion. A task can be consumed and freed before its posting
call returns. Cancellation matching and shutdown cleanup can free tasks without
executing them. The static dispatcher sentinel is not a per-event cancellation
epoch or a retained-lifetime guarantee. No additional unsupported completion or
retention conclusion was identified within those stated bounds.

## Requirements for a future passive observer

These are design constraints, **not a present safety proof**:

- To count concrete executor arrival, retain only an entry observation and
  bounded metadata such as original LR/thread identity; saving a raw task
  address is unnecessary and equal address bits would not prove object identity.
- A shared submission observer at `0x237a440` must filter the original route
  caller LR (`0x1b01e04`) and reject null `x1` before any task read. The reviewed
  allocation-failure path can submit null. Such filtering narrows provenance;
  it does not by itself prove object mapping, extent or native lifetime.
- Only after establishing the concrete task's valid entry lifetime may a
  wrapper copy initialized scalar fields: full 64-bit TaskNo at `+0x10`, full
  ID at `+0x20`, and the two 32-bit inputs at `+0x28/+0x2c`. Do not copy all
  56 allocation bytes, assume padding is initialized, or follow the captured
  dispatcher/child graph before native guards.
- A post-submission record must use only those pre-call local copies and the
  full native `x0` return. Never read task or dispatcher memory after the
  original call. TaskNo scope, wrap and reuse need explicit correlation bounds;
  reservation ordinals, log order and equal pointer bits are insufficient.
- A concrete executor entry proves invocation only. It does not show guard
  passage, child selection, handled input or completion. Copying scalar fields
  there still needs a justified live-task contract; it cannot inherit safety
  merely because later successful native branches read those fields.
- A new call/return wrapper needs its own argument/return, reentry, exception
  and unwind checks. Existing entry-only tail-forwarding ABI evidence does not
  validate it. Preserve native results, thread behavior and existing hook
  ownership; no callback may retain native pointers for later processing.

No source or verifier change was made by the challenger. The report-only scope
correction was made by the artifact owner. Raw firmware and disassembly remain
private; this review adds no observation or activation implementation.

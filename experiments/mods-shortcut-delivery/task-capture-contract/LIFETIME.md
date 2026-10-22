# Entry-local task capture: lifetime and provenance

A synchronous, bounded scalar copy at either reviewed entry can borrow the
**task storage for that native invocation**, conditional on the native call's
ordinary validity and lifetime contract. This is supported by construction
before submission and queue consumption before execution/cleanup. It does not
follow merely from some later native branch reading a field, and it does not
retain the captured dispatcher, children or task after forwarding.

This is a focused offline review of the exact ELF
`6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`, against repository
baseline `70fb9d4d3aaffe04ceda5164d5bb2c4a93b759d9`. It builds on the existing
[task/executor audit](../route-task-ownership/BOUNDARIES.md),
[task-box audit](../route-task-ownership/TASKBOX.md), and
[independent review](../route-task-ownership/REVIEW.md). Narrow firmware slices
were inspected to confirm initialization, publication and the two original-LR
values. No unchanged verifier suite was rerun, no firmware was executed, and no
runtime, physical-device or network operation occurred. This note does not
validate a wrapper, install a hook or establish a live capture result.

## The two invocation boundaries

| Boundary | Native input | Reviewed caller and original LR | Storage argument |
|---|---|---|---|
| `TimerTaskBox` submission `0x237a440` | `x0=box`, `x1=task` | Route `0x1b01e00` is `BLR x23`, so its original LR is **`0x1b01e04`** | Allocation-success path constructs this task before calling the service. A separate allocation-failure path can call the same service with null. |
| Concrete executor `0x1b029c0` | `x0=task` | `executeTasks` loads execute slot `+0x10` at `0x237d7f8`, then **`BLR x1` at `0x237d7fc`**, so its original LR is **`0x237d800`** | The drain has popped a task, read its vptr/TaskNo and selected its execute slot. Its ordinary-return cleanup calls `freeTask` later at `0x237d824`. |

These LR values are exact instruction return sites. They narrow callers, but do
not authenticate memory, identify a live object or exclude corrupted state.
The LR must be captured before the observer's own helper calls replace it. The
executor LR was checked from the actual `BLR` instruction and its successor,
not inferred from the previous input observer's unrelated LR `0x22c6e08`.
The caller is a generic task drain, so its LR alone does not identify this
specific task type; the expected task vptr is also required.

## Construction and initialized bytes

The route requests **0x38 bytes aligned to 8** at `0x1b01d64..80`. It saves the
allocator result in `x19` at `0x1b01d84`, and branches on null at `0x1b01d88`.
Only the nonnull path constructs these fields:

| Byte range, end exclusive | Store and meaning |
|---|---|
| `[0x00,0x08)` | `0x1b01db8`: task vptr `0x2d444d8` |
| `[0x08,0x0c)` | `0x1b01dbc`: 32-bit initialization tag returned by `0x7305f8` |
| `[0x10,0x18)` | `0x1b01dc0`: full 64-bit TaskNo from the box counter |
| `[0x18,0x20)` | Same paired store: raw adjusted dispatcher address |
| `[0x20,0x28)` | `0x1b01dc4`: full 64-bit ID copied from route `x2` |
| `[0x28,0x2c)` | `0x1b01dc8`: route `w3` |
| `[0x2c,0x30)` | Same paired store: route `w4` |

Thus the locally proved initialized ranges are **`[0,0x0c)` and
`[0x10,0x30)`**, totaling 44 bytes. Nothing in these stores initializes padding
`[0x0c,0x10)` or tail `[0x30,0x38)`. Copying the entire 56-byte allocation, or a
C++ struct whose padding spans those holes, is unjustified. The selected
[capture contract](capture.h) reads only **24 payload bytes**:
TaskNo at `[0x10,0x18)`, ID at `[0x20,0x28)`, and the two integer inputs at
`[0x28,0x30)`. The eight-byte vptr check is separate, for **32 task bytes read
in total** on a successful copy. It never reads the initialization tag, the
captured dispatcher field `[0x18,0x20)`, padding or the unused allocation tail.
Do not replace those disjoint reads with a contiguous 32-byte payload copy. The
full ID remains an opaque value and must not be dereferenced.

These stores precede the submission call at `0x1b01e00` in the reviewed path.
The subsequent specialization counter increment counts task bookkeeping; it is
not a dispatcher retain or a publication fence relied on for payload capture.
The reviewed local path supplies no earlier queue publication of this task
pointer. It does call allocation/profiling/initialization helpers whose complete
transitive behavior is outside this review. This is a bounded native-call
precondition, not a whole-program assertion that no external alias can exist.

On the allocation-failure path, `x19` remains null while diagnostic helpers run;
`0x1b01f5c` rejoins the submission sequence at `0x1b01df0`. Consequently a matched
submission caller LR can accompany a null task. The observer must reject it
before any vptr or selected payload read; caller provenance is insufficient
alone.

## Publication and execution ordering

The selected TimerTaskBox post route forwards to the concrete AsyncTaskBox
submission reviewed earlier. Queue insertion `0x237f7e8..ec` copies the task
pointer into a queue node's payload. Its successful link update uses release
`STLXR` at `0x237f864`. Queue removal reads the linked node with acquire `LDAR`
at `0x237f920`, then copies the node's task pointer at `0x237f95c..60` before
returning it to the drain. The construction stores precede this selected
publication path; the consumer's task execution follows removal.

Those instructions support the intended handoff of initialized payload from
posting to consuming thread. They are not an independent proof of the full
queue algorithm, reclamation, exception handling, or all possible concurrent
aliases. An observer should not replace the native synchronization, add its own
claims about general memory ordering, or use the integer guard as a lifetime
fence.

At the executor call site, the native drain already reads the popped task's
vptr and TaskNo, dispatches its execute slot, and performs normal cleanup after
that call returns. The inspected path does not free the task before invoking
its executor. Cancellation matching can instead bypass execution and go to
cleanup, and shutdown can free queued tasks without invoking this entry. A
missing executor observation is therefore UNKNOWN; it is not proof of loss,
cancellation or a failed submission.

Task storage and captured-object storage remain distinct. The executor's first
native instruction reads the raw dispatcher field at task `+0x18`; its later
sentinel/address checks do not retain the dispatcher. The selected capture
helper never reads the task's captured dispatcher field,
even as opaque bits. It also must not read dispatcher `+0x19c`, child fields,
native encoder objects or any other pointed-to memory. Task initialization
facts do not establish that the captured dispatcher itself remains valid.

## Conditions for a narrowly borrowed copy

The proposed scope is **entry-only, synchronous and pre-forwarding**:

1. Verify the configured phase and original caller LR, and require nonzero
   observer run/invocation identifiers, before attempting a task read. Those
   identifiers scope observer records; they do not prove native identity.
   Reject null and arithmetic overflow for every intended byte range;
   require the allocation's eight-byte alignment. These are rejection filters,
   not proof of mapping or allocation extent.
2. While still in that invocation, check the exact task vptr `0x2d444d8` and
   copy only TaskNo, ID, delta bits and cumulative bits using the disjoint
   ranges above. Do not copy the task's dispatcher field or tag. The vptr check
   itself requires valid task storage; a plausible vptr cannot make an
   arbitrary address safe. Use fixed-width copies that do not overread these
   selected ranges.
3. Complete native-memory reads before calling the original entry. The current
   helper returns a 64-byte pointer-free observer context containing the
   24-byte payload and observer metadata, not task/box/dispatcher addresses.
   Queue/log only observer-owned value copies. Do not retain a task address for later
   reads, let an asynchronous worker dereference it, or depend on a pointer
   remaining valid across the original call.
4. Forward native arguments and control flow unchanged even when capture is
   rejected, full, reentrant or disabled. The copy helper must not invoke native
   code, change thread ownership, or mutate the task or queue.

Under the reviewed native call's valid-storage precondition, these reads fit
inside the same storage lifetime needed by the native service/execute call.
That is the conditional justification for borrowing; it is stronger than
assuming validity from a later successful branch, but weaker than a runtime
memory-safety guarantee. It does not establish exclusion of a concurrent
native destructor, an arbitrary corrupt or spoofed caller, memory-mapping
validity, safe observer unload, or all transitive reentrancy. If the experiment
requires an unconditional guarantee against such cases, these checks do not
provide it and payload reads remain gated.

In particular, post-return reads remain forbidden. Another consumer can execute
and free a task before its posting call returns. Executor return is immediately
followed by further drain work and eventual freeing. Optional `onTaskPosted`
notification and exceptional paths supply no longer retain. A call/return
observer, if designed separately, may retain only pre-call value copies and
native return-register bits; this note does not validate its ABI/unwind behavior.

## Interpretation of captures

A submission-entry copy means only that the reviewed call was reached with a
matching, nonnull, type-filtered task under the stated assumptions. It does not
show a successful queue return. An executor-entry copy means invocation reached
the concrete task executor, before its dispatcher guard and child handling. It
does not establish guard success, handled input, completion, a cancellation
outcome or safe navigation.

The current helper classifies a matching nonzero returned TaskNo or matching
submission/execution values as value relations only; it has no native
admission, cancellation or completion state. The copied full TaskNo, ID and
inputs may support a bounded correlation design,
but TaskNo scope/wrap, queue reuse, losses, reentry and separate observer lanes
need explicit treatment. Equal pointer bits, reservation numbers and log order
are not event identity. No native pointer retention, dispatcher/child ownership,
focus handback, direct-menu activation or native pause is authorized by this
entry-local task-storage analysis.

No new verifier was added: the existing exact-function fingerprints and anchors
already cover the construction, queue links and executor `BLR`; this pass read
the relevant slices and confirmed their ordering. A new host capture fixture can
discriminate read boundaries and rejection behavior using owned synthetic
storage, but cannot prove those native lifetime assumptions or ARM64 timing.

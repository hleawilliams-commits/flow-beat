# Task-box submission, execution and ownership

The exact firmware supplies a concrete **queued** implementation for the
`+0x30` task-box call made by route entry `0x1b01cf8`. It is distinct from the
separate owner-thread predicate at `+0x28`. The task allocation and queue own the
task storage; the inspected capture does **not** retain the dispatcher or its
children. This narrows the earlier [input-route](../input-route/) uncertainty
without converting the four [bounded entry records](../route-observer-burst/runtime-evidence.md)
into queue-admission or execution observations.

This is an offline ELF audit. No new hook, native call, process-memory read,
emulator/device/network access, package or core change occurred. All addresses
below are exact-firmware code/data locations, not observed runtime object
addresses. Firmware SHA-256 is
`6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
The [verifier](taskbox-verifier.py) and [result](taskbox-result.json) bind the
relationships to exact instruction blocks, RTTI, diagnostic names and unwind
extents. The companion [task/executor review](BOUNDARIES.md) covers captures,
task-specific destructors, the dispatcher guard and hook policy.

## Concrete submission chain

| Stage | Exact relationship |
|---|---|
| Shared task box | `0x1b00e10` returns the singleton at `0x3fe8f88`. Its guarded initialization calls constructor `0x1b00d20`. |
| Concrete type | Constructor installs vptr `0x321b108`; RTTI identifies `meow::TimerTaskBox`. `+0x28` resolves to `0x1e61cd8`, while `+0x30` resolves to `0x237a440`. |
| Route submission | `0x1b01d20..2c` obtains that box and reads `+0x30`. `0x1b01df8..e00` passes `x0=box`, `x1=allocated task` to it. There is no `+0x28` selection in this submission sequence. |
| Timer forwarding | `0x237a440` obtains shared `TimerTaskBox::Impl` through `0x237a140` and tail-calls `0x237eac0` with `x0=TimerTaskBox`, `x1=Impl+0x20`, `x2=task`. |
| Embedded queue | Impl constructor `0x237fa50` installs vptr `0x321afd8` at `Impl+0x20`. RTTI identifies `meow::AsyncTaskBox`; its `+0x30` is `0x2379cc0`. |
| Queue insertion | `0x237eac0` calls that `+0x30`. `0x2379cc0` saves the eight-byte task field `+0x10`, submits the task pointer through queue helper `0x237f770`, and returns the saved value on its successful insertion path. |

The diagnostic strings explicitly name
`AsyncTaskBoxBase::postTaskInternal(AsyncTaskBoxBase&, IAsyncTask*)` and
`AsyncTaskBox::postTaskInternal(IAsyncTask*)`, with return type `meow::TaskNo`.
The TaskNo is the value created by the atomic increment at task-box `+0x20` and
stored in task `+0x10`; it is not a task reference count or the observer's
reservation-slot number. Successful queue insertion returns it in full `x0`.
It supplies no handler return or completion result. This audit does not prove
that zero is impossible after counter wrap, or exercise allocation, diagnostic
or exception paths. Both post helpers contain failure/zero-result cleanup
branches; those branches are not a tested failure-recovery API.

The queue helper copies the task pointer into a queue node at node `+8`; the
link publication uses the observed acquire/release exclusive operations. The
three checked submission bodies (`0x237a440`, `0x237eac0`, `0x2379cc0`) have no
direct literal `B`/`BL` to task executor `0x1b029c0` or either named owner-thread
predicate. That narrow check does not resolve indirect calls, transitive behavior
of other callees, or the route's later notification callback. The selected
`+0x30` sequence establishes a queue path for the constructor-installed vtables;
it does not select `+0x28`, establish their live values at the earlier input
instant, or prove whole-queue concurrency behavior.

Do not equate “queued” with “executes after the poster returns.” A consumer could
run concurrently. In addition, after a nonzero submission return the route can
invoke an optional box `+0x18` object's `+0x10` callback; its diagnostic label is
`AsyncTaskBoxBase::doAsync - onTaskPosted`. The constructor initializes that
pointer to null, but this does not prove it stays null. Callback behavior,
reentrancy and scheduling relative to the poster are not established. The outer
route entry also performs later calls and does not preserve the submission
TaskNo as a general public return contract.

## Thread predicates are separate

`TimerTaskBox` predicate `0x1e61cd8` compares `pthread_self()` with a nullable
owner pointer's field `+0x10`, obtained through getter `0x2503090`. A null owner
leaves comparison operand zero. It returns one for inequality and zero for
equality. The wrapper `0x249b0f0` and its actual
`R_AARCH64_JUMP_SLOT` relocation identify `pthread_self`, rather than Linux TID
or PID.

The embedded `AsyncTaskBox` also has its own `+0x28` predicate, `0x99ca38`, which
compares `pthread_self()` with box field `+0x30`. The selected submission sequence
uses `+0x30`, not the `+0x28` predicate slot. The three checked submission bodies
have no direct literal `B`/`BL` to either named predicate; indirect and transitive
behavior remains unproved. The older generic view wrapper did
use `TimerTaskBox +0x28` to select its path; that earlier finding must not be
transferred to this separate route. The recorded `TID != PID` is still only the
observer's lane decision. It does not establish either live native comparison,
the queue consumer thread, or a permission to run navigation on that thread.

## Execution, cancellation and freeing

`TimerTaskBox::Impl`'s virtual function `0x2380a20` invokes
`AsyncTaskBox::executeTasks()` at `0x237d4f0` on `Impl+0x20`. The latter pops a
pointer through `0x237f900`. After its validity and cancellation checks, it stores
the task's TaskNo as the box's current task number and calls **task virtual slot
`+0x10`**, which is `0x1b029c0` for this route's concrete task. After ordinary
return, it calls `AsyncTaskBoxBase::freeTask()` at `0x237e9f0`.

`freeTask` first calls task virtual slot `+0` (the non-deleting destructor), then
returns its storage through the box's memory-pool `+0x18` deallocator or the
verified default deallocation shortcut. It does not execute the task. The
`AsyncTaskBox` destructor has a distinct queue-draining loop at
`0x237a704..720` that pops pending tasks and calls `freeTask` without calling their
execute slot. Destruction is therefore not evidence that a handler ran. This
bounded audit does not prove exception cleanup, safe destruction concurrent with
producers, or a custom observer's shutdown lifetime.

A real token-based cancellation path also exists:

1. `TimerTaskBox +0x10` is `0x237fdc0`, forwarding the eight-byte input token to
   `AsyncTaskBox::cancelTask(TaskNo::Type)` at `0x2376130`.
2. That method requires auxiliary context at box `+0xc8` and copies the token
   into its auxiliary queue. The default Impl construction initializes this
   field to null. Configuration, rejection handling and whether the live box
   supports cancellation were not observed.
3. `executeTasks` reads the queued task's `+0x10` TaskNo and compares it with
   retained cancellation tokens. The match at `0x237d788` branches through
   `0x237da84..88` directly to cleanup at `0x237d810`, bypassing the execute call.

This is a static mechanism for suppressing execution by task number, not an
owned cancellation API for this feature. No producer of a matching cancellation
token for these four events was observed. A cancellation token is not proof of
view retention, revocation already in progress, eventual drainage, timing,
wraparound safety, or permitted navigation. The executor's separate dispatcher
integer guard remains a different check; see the companion review.

## What the reference counts own

The `TimerTaskBox` constructor places a memory-pool pointer at box `+8` and its
shared control block at box `+0x10`. That control block's exact RTTI is
`std::_Sp_counted_ptr_inplace<meow::AsyncTaskBoxBase::DefaultMemPool, ...>`.
The constructor initializes its two 32-bit count fields to one. TimerTaskBox
destruction decrements the control block's strong count at `+8`, dispatches
control-block disposal on the last strong owner, and then handles its weak count.
Those operations retain the **memory pool**, not the dispatcher captured in the
separately allocated task.

The task's raw dispatcher capture at `+0x18`, its TaskNo at `+0x10`, and its
specialization-global allocation counter are different objects and meanings.
The task-specific destructor audit finds no retained dispatcher release, and the
queue owns a pointer to task storage rather than a shared dispatcher pointer.
Native lifetime rules may exist elsewhere, but this chain establishes no retain
for arbitrary delayed use of the dispatcher or its children. An observed task
pointer must not be saved and dereferenced after native submission, execution or
freeing; consumption can race the return from posting.

## Possible passive boundaries and remaining gate

`0x237a440` accepts box/task in `x0/x1`; the downstream `0x237eac0` accepts
originating box/target box/task in `x0/x1/x2`. They are broader shared services,
so entry counts alone are not route-specific. The companion verifier checks
unchanged pinned instruction/overlap policy for these entries. Passing that
static policy is not a complete ABI wrapper, claim or live-lifetime proof.
A post-return hook must preserve the complete return ABI and use copied values;
it must not read the task again after the native call. An execution entry record
at the concrete task executor would mean execution was attempted, not that its
guard passed, a child handled input, or the task completed.

Any future correlation needs a native-owned lifetime boundary and an explicit
copied identifier contract. The existing scalar input observer intentionally
reads no task or dispatcher objects and cannot provide that correlation alone.
No new observer or activation implementation follows from this audit. Native
returns, active child selection, completion/cancellation, dispatcher retention,
focus handback and navigation remain unresolved runtime/ownership gates.

## Reproduction and audit limits

Run with the existing private ELF and a Python environment containing Capstone
and pyelftools:

```sh
python3 taskbox-verifier.py --elf /private/path/EP147 --output /private/path/result.json
```

The verifier passes **199 instruction anchors across 38 blocks**, 12 static data
words, four RTTI identities, six diagnostic method names, the actual
`pthread_self` relocation and 21 exact function/unwind fingerprints. Eleven
strict controls reject predicate/post slot confusion, a wrong subobject,
truncated TaskNo, execute-versus-enqueue confusion, a reversed queue outcome,
wrong cancel field, cancellation that executes, wrong executor slot, shutdown
execution, pool/dispatcher confusion and a reversed thread predicate. These are
static mutation checks, not executed firmware or stress tests.

The first retained verifier run passed and its result is preserved privately.
Review then narrowed the direct-call exclusion wording: it does not prove
transitive or indirect behavior. This report-only correction changed no anchors
or controls. During exploratory disassembly, one
scratch request used a non-FDE address and one scratch import used a nonexistent
placeholder module; neither executed firmware or modified repository evidence.
A later read-only command was blocked before launch when automatic approval
review ran out of credits, and the default executor also could not launch with
the workspace symlink. The resumed approved execution completed the audit;
these are tooling failures, not candidate or native-runtime failures. Raw
scratch and disassembly remain private.

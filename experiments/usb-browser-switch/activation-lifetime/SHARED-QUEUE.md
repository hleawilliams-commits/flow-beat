# Shared queue lifetime is separate from the originating boxes

Both the timer and source service construct a `TimerTaskBox` subobject with
their own reference-counted **DefaultMemPool** instances. Their pool controls do not own the execution queue.
The posting route uses a separate, process-shared `TimerTaskBox::Impl`, which
registers a `juce::DeletedAtShutdown` base. Its destructor reaches the pending
queue cleanup. This supplies the missing concrete ownership distinction; it does
not make view/service destruction a drain or establish callback quiescence.

This is static inspection of EP147 SHA-256
`6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
The [earlier task-box review](../../mods-shortcut-delivery/route-task-ownership/TASKBOX.md)
already establishes posting, execution, token cancellation and pending-task
cleanup. Those passing observations are not repeated here. This review binds
the shared implementation's construction, native shutdown registration and
concrete destructor targets. No native call, emulator access or installation
occurred.

## The shared implementation lookup

`0x237a140` forms base `0x4043000+0x9f0`, checks the initialization guard at
base `+0xe0` (**`0x4043ad0`**), and returns the pointer at base `+0xe8`
(**`0x4043ad8`**). Its initialization branch allocates **`0xf8` bytes**, calls
constructor **`0x237fa50`**, stores that pointer, and releases the guard.
The normal lookup has no acquisition of a timer/view/service reference.

The incoming low-byte flag controls initial timer startup. The implementation
constructor saves that flag, sets interval `0x21` (33) at Impl `+0xf0`, and, when
the flag is nonzero and its timer interval is not positive, calls the timer-start
helper `0x250a240`. This flag is not a cancellation permit or ownership count. It is used only
when singleton construction runs; later flag-one lookups do not revisit startup.
The timer constructor's flag-one lookup and the service constructor's lookup
are separate from their newly created pool controls.

The implementation has these concrete subobjects:

| Offset | Evidence |
|---|---|
| `+0` | Primary vptr `0x321b0c0`; RTTI `0x321ae10` names `meow::TimerTaskBox::Impl`, with a Timer base at zero. |
| `+0x18` | Secondary vptr `0x321b0e8`; the same RTTI describes `juce::DeletedAtShutdown` at offset `0x18`. Constructor calls `0x25029a0` on this adjusted pointer. |
| `+0x20` | Embedded execution `AsyncTaskBox`, constructed separately from each originating box and its pool. |

The full lookup contains no `atexit` registration or release of its cached
pointer. That local absence is not a whole-program lifetime claim: the
constructor's actual shutdown registration matters, and other field writers or
shutdown invocations were not exhaustively searched.

## Native shutdown registration and destruction

RTTI `0x34f4300` identifies `juce::DeletedAtShutdown`; its name is at
`0x34f42e0` and its address point is `0x34f4cd8`. Constructor
**`0x25029a0`** stores the adjusted base pointer into a shared registry's
array, increasing its length under the registry's local lock. Destructor
**`0x2502280`** finds/removes that pointer and may resize the array under the
same lock. This establishes registration of the Impl base, not a view lease or
an assertion that shutdown has already occurred.

The Impl vtable binds the destructor routes:

| Address point / slot | Target | Local sequence |
|---|---|---|
| `0x321b0c0 +0` | `0x237fdf0` | Destroy AsyncTaskBox at Impl `+0x20`; unregister DeletedAtShutdown at `+0x18`; destroy Timer base. |
| `0x321b0c0 +8` | `0x23805f0` | Same sequence, then sized delete of `0xf8` bytes. |
| `0x321b0e8 +0` | `0x237fe40` | Adjusted secondary-base form of non-deleting destruction. |
| `0x321b0e8 +8` | `0x2380590` | Adjusted secondary-base form followed by sized deletion. |

All four reach **`0x237a610`**, the AsyncTaskBox destructor already reviewed.
Its pending-task loop pops and frees tasks, with no task-execution virtual call
in that loop. The new fact is that **Impl destruction** reaches this queue
cleanup. The concrete DefaultMemPool disposal used by the timer and service
instead resolves to `0x237e790`, which clears pool bookkeeping and returns.
These are different objects and different destruction paths.

The shutdown-registry lock protects registry operations in the inspected
bodies. It is not the previously inspected timer schedule mutex, and this
review does not prove that it stops producers or waits for an already selected
callback. The Impl destructor's local order even reaches AsyncTaskBox cleanup
before destruction of its Timer base; thread and shutdown sequencing therefore
remain necessary context. No unsafe native behavior is inferred solely from
that order.

## Consequence for activation

Finding a long-lived shared queue does not extend the lifetime of a raw timer
or service captured by a queued task. Conversely, finding an originating pool
control does not mean its last release destroys that shared queue. The
[view](VIEW-OWNER.md), [timer](TIMER-OWNER.md), and
[service](SERVICE-OWNER.md) reviews identify actual native owners separately.

The remaining proof must connect a permitted action to live owner custody,
message-thread/reentrant callback ordering, exception paths and teardown of the
actual shared implementation. No custom cancellation, drain, reference-count
write, post-return native read or direct setSource adapter is enabled by this
static graph. Native activation and complete wrapper gates remain **CLOSED**.

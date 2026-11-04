# SourceSelectView timer destruction: entry removal is not callback quiescence

The previously unresolved virtual destructor is the deleting destructor of **`juce::MultiTimerCallback`**. Its inspected body removes an active entry from JUCE's timer schedule under the global timer mutex, clears its interval and deletes the entry. It does not wait for an already selected timer callback to return or cancel the separately queued `StateRefreshCycleTimer` task. This narrows the teardown chain; it does not prove that native navigation is unsafe, or establish the broader ownership contract needed for a new activation callback.

This is a static review of the private EP147 image with SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`, firmware tag `ep147-9e9454e357389ece`. Addresses, RTTI, complete function extents and hashes below refer only to that image. No firmware function, emulator endpoint, hook or previous test suite was executed. Raw disassembly remains private. The [earlier ownership review](../activation-ownership/README.md) remains the context for the raw queued timer capture and activation gate.

## Concrete type and construction

| Exact anchor | Static meaning |
|---|---|
| `0x1b2cbd0..0x1b2cbd4` | SourceSelectView destruction passes its embedded member at `view+0x348` to `0x1ae8958`. |
| RTTI `0x2d412c8` | Names `gui::source_select::StateRefreshCycleTimer`, with `juce::MultiTimer` as its base. |
| `0x1ae89b8..0x1ae89cc` | After its task-box reference handling, the timer destructor frees the listener-array storage at `timer+0x28`, then tail-calls the MultiTimer destructor at `0x25059e0`. This is the observed order, not an independent lifetime guarantee. |
| `0x250a610` | The MultiTimer start path finds an existing entry by id or allocates a `0x28`-byte entry. At `0x250a69c` it stores vptr `0x34f4ec8`; `0x250a6ac` stores the raw MultiTimer parent at entry `+0x18`, and `0x250a6b4` stores the id at `+0x20`. It inserts the entry into the parent's array at `0x250a6c8`, then starts the underlying timer. |
| Vptr `0x34f4ec8` | Its preceding RTTI word points to `0x34f4768`, named `juce::MultiTimerCallback`, whose base is `juce::Timer`. Slots `+0`, `+8`, `+0x10` point respectively to `0x250bb10`, `0x250bca0`, `0x250aed0`. |
| `0x2505a14..0x2505a1c`, `0x2505a8c..0x2505a94` | The MultiTimer destructor's array cleanup dispatches virtual slot `+8`. For the concrete entries constructed above this resolves to `0x250bca0`, rather than an unidentified cancellation service. |

The MultiTimer destructor first acquires its own lock at `parent+8` through `0x24a2700`. Its entry array is at `parent+0x10`, with count at `parent+0x20`. The first deletion loop runs before that lock is released at `0x2505a84`; a second array-cleanup loop follows. The parent lock is distinct from the global timer mutex used by each entry destructor.

## What the entry destructor actually does

`0x250bca0` first installs the base Timer vptr, then calls `0x2499580` with `0x40443b8` at `0x250bcc8`. The wrapper tail-calls the image's `pthread_mutex_lock` PLT entry. It tests the entry interval at `+0x10`.

For an active entry with an existing TimerThread, `0x250bcfc..0x250bd3c` removes the entry from the thread's 16-byte-element schedule vector: it shifts later elements, fixes their stored indices and shortens the end pointer. It clears the entry interval at `0x250bd40`, calls the `pthread_mutex_unlock` wrapper at `0x250bd48`, then tail-calls sized `operator delete` with size `0x28` at `0x250bd5c`. The inactive path also unlocks and deletes, at `0x250bcdc..0x250bcf0`.

There is no condition wait, thread join, task-box drain or queued-task lookup in this complete destructor body. The mutex call can wait for that mutex; it is not a wait for the duration of timer callbacks. These are static call/branch facts, not observed success of locking, deletion or cancellation.

## The callback runs outside that mutex

The concrete `juce::Timer::TimerThread::CallTimersMessage` path at `0x250b0e0` selects an entry while holding the same global mutex. It releases the mutex at **`0x250b1c0`**, then invokes the entry's callback through slot `+0x10` at **`0x250b1f4`**. It reacquires the mutex later at `0x250b21c` on the normal loop path; the elapsed-time branch and exception cleanup also reacquire it separately.

The selected `MultiTimerCallback` forwards its raw parent and timer id at `0x250af18..0x250af2c`. The inspected mutex removal therefore prevents continued scheduling of that registered entry; the mutex alone does not establish that a callback already selected outside the lock has finished. Native message-thread serialization, permitted destruction contexts and other owners may constrain that overlap, but this review has not established those contracts for a new operation inside the listener.

The separate queued task remains a different lifetime question. `StateRefreshCycleTimer` captures id at task `+0x18` and raw timer pointer at `+0x20` (`0x1ae8dec..0x1ae8df0`), then submits the task at `0x1ae8e44`. Execute entry `0x1ae8a40` reloads that pointer and invokes listeners; the id-1 loop can read `timer+0x38` after a listener returns at `0x1ae8b54`. Removing the underlying JUCE schedule entry neither edits this task's capture nor, in the inspected entry destructor, removes or drains that submitted task. The global task-counter decrement described in the preceding review is not a retain/release of the embedded timer or view.

## Bound evidence and remaining gate

The complete bodies were read using their ELF unwind extents, not a fixed-size guess extending into neighboring functions:

| Function entry / extent | SHA-256 of exact function bytes |
|---|---|
| `0x250a610` / `0x164` — entry construction/start | `0737a65ba5e6068d8e4eb2d9538d60b36eb192c2baeafc7743fb33d4b12d7b06` |
| `0x25059e0` / `0x124` — MultiTimer destruction | `97ce5795c28d2a755fe210bcd5ccd2ba95afc66f4e7c996d8ef94514bfd4c79c` |
| `0x250bca0` / `0xc0` — entry deleting destructor | `f14516e13bb957c1e26607ce5ec5f71ec05b71ac6c792b3e739d6921a8f03a28` |
| `0x250b0e0` / `0x204` — timer message callback | `36144d326496b43aba432c07c5612ea8f5c14ef2c9a6ee03566a579c330f30fc` |

The remaining proof must cover actual view/timer retention or serialization across native navigation, the queued task-box owner's destruction/cancellation contract, and permissible reentrancy through both direct and queued listener routes. This review does not establish absence of every transitive cancellation mechanism, a use-after-free in the native application, or a safe custom activation scope. It adds no native adapter, USB button, cancellation, post-return native read or reference-count write. The executable-segment vslot refusal and one-instruction timer-listener hook refusal remain unchanged.

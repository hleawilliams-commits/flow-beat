# The timer's shared field owns a memory pool

`StateRefreshCycleTimer` embeds a `meow::TimerTaskBox`; the shared pointer inside that member owns **`AsyncTaskBoxBase::DefaultMemPool`**, not the enclosing timer or the shared execution queue. The exact last-owner release resolves to a pool destructor that clears one word, followed by control-block deallocation when its weak count also reaches zero. The inspected release chain provides no task-number cancellation, queue drainage or wait for a queued raw-timer callback.

This extends the [concrete timer-entry review](../activation-abi/TIMER-REVIEW.md) and the earlier [task-box ownership review](../../mods-shortcut-delivery/route-task-ownership/TASKBOX.md) by binding the timer's own construction and release paths. It is static inspection of EP147 SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925` (`ep147-9e9454e357389ece`). The source review ran no policy test, emulator endpoint, native function or hook. The aggregate receipt records a separate check of new ownership pins using the unchanged static verifier. Raw disassembly remains private.

## Constructor and destructor identity

The constructor is **`0x1ae9380`**, called from SourceSelectView construction at `0x1b2d3f8`. **`0x1ae8868` is a deleting destructor**, not a constructor: after destroying members and the MultiTimer base, it passes size `0x68` to sized `operator delete`. The non-deleting destructor used for the embedded member is `0x1ae8958`.

The exact StateRefreshCycleTimer vptr is `0x2d412f0`, preceded by RTTI `0x2d412c8`. Its first two slots are `0x1ae8958` and `0x1ae8868`; slot `+0x10` is the existing timer callback `0x1ae8c98`. This separates ordinary embedded-member destruction from deletion of a separately allocated timer.

The constructor establishes these fields:

| Timer offset | Construction and meaning |
|---|---|
| `+0x40` | `0x1ae9420` stores vptr `0x321b108`, whose RTTI at `0x321add8` names `meow::TimerTaskBox`. This is an embedded originating box, not a pointer to the shared execution implementation. |
| `+0x48` | The same store writes a pointer to the pool object at `control_block+0x10`. |
| `+0x50` | `0x1ae9428` stores the separately allocated control block. Its strong/weak 32-bit counts were both initialized to one at `0x1ae93e4`. |
| `+0x58` | Initialized to null alongside the control-block pointer. The existing posting path treats box `+0x18` as an optional post-notification callback object. |
| `+0x60` | Initialized to zero at `0x1ae942c`; the existing posting path increments this originating box's TaskNo counter. This is separate from pool ownership. |

The allocation at `0x1ae93b0..0x1ae93ec` is `0x20` bytes and installs control-block vptr `0x2900d38`. Its preceding RTTI is `0x2900848`, which names `std::_Sp_counted_ptr_inplace<meow::AsyncTaskBoxBase::DefaultMemPool, std::allocator<…>, (__gnu_cxx::_Lock_policy)2>`. The in-place pool begins at block `+0x10`; its vptr `0x321af58`, installed at `0x1ae9418`, has RTTI `0x321aab8` naming `meow::AsyncTaskBoxBase::DefaultMemPool`.

The constructor subsequently calls `0x237a140` with the low-byte flag one at `0x1ae9424..0x1ae9430`. That lookup is separate from the just-stored pool pointer/control block. It does not turn those fields into ownership of the timer or of the execution queue. The existing task-box review covers submission through the shared implementation; this report's release conclusion concerns the timer's concrete member only.

## Exact last-owner release

The non-deleting destructor sets its timer/base-member vptrs, obtains the control block from timer `+0x50`, and decrements the strong count at block `+8`. The image contains both atomic and single-thread branches; both compare the previous count with one. Only the last strong owner invokes control-block slot `+0x10`, at `0x1ae89e4..0x1ae89f0`.

For the constructor-installed block, that slot is **`0x6d0b30`**. Its complete body adjusts the pointer to block `+0x10`, loads that pool object's vptr, and tail-dispatches slot `+0`. The constructed pool's slot resolves to **`0x237e790`**, whose complete two-instruction body clears the 32-bit word at pool `+8` and returns. It does not follow a timer pointer, locate a task token or invoke the queue destructor.

After disposal, the timer destructor decrements the block's weak count at `+0xc`. On the last weak owner it invokes control-block slot `+0x18`, at `0x1ae8a14..0x1ae8a20`. That slot is **`0x6d0db8`**, a one-instruction tail branch to PLT entry `0x426680`; the relocation names unsized `operator delete(void*)`. The separate deleting timer destructor contains the same strong/dispose/weak/destroy sequence before freeing timer storage.

Consequently, non-last strong release does not dispose even the pool; last strong release disposes the pool; last weak release frees its control block. None of these counts is a timer/view retain. The inspected member-release bodies contain no call to the existing token cancellation route or AsyncTaskBox queue-draining destructor. This is not a claim about arbitrary deallocator implementations or every transitive native destruction path.

## What remains unresolved

After releasing the pool reference, `0x1ae8958` frees listener-array storage and calls the MultiTimer base destructor. The earlier review shows that base destruction removes schedule entries but does not establish callback quiescence. Meanwhile, the already identified queued task copies a raw timer pointer at task `+0x20`; its executor can reread the timer after a listener returns. The pool control block has no enclosing-timer field or release in the construction/disposal chain inspected here.

These facts close the specific hypothesis that the timer's `+0x50` shared field provides a hidden queue drain or timer retain on last release. They do not establish when the shared execution implementation is destroyed, whether other native owners cancel relevant TaskNos, or whether message-thread serialization and permitted reentrancy prevent the problematic overlap. Live object identity and a safe activation lifetime remain unproved. No use-after-free in the native program is asserted, and no post-return native read, custom cancellation, reference-count change or USB activation follows from this review.

## Exact function fingerprints

Each extent below is taken from the image's unwind entry and hashed as complete function bytes:

| Entry / extent | SHA-256 |
|---|---|
| Constructor `0x1ae9380` / `0x198` | `d614fe03093f8f90b9bd0537b0ae816864eabab0aa472e8028ec5108f88782fc` |
| Deleting timer destructor `0x1ae8868` / `0xf0` | `afff1ce145598bc30222bd9e66099af738a64d580584863f7f66c0fa547de3f7` |
| Embedded timer destructor `0x1ae8958` / `0xe4` | `4f5f5427ae2ff77a0a3ecb7bf90046e6724abf4fbf7927cb6882453e68ac1e61` |
| Pool control-block dispose `0x6d0b30` / `0x14` | `c9322a0d5f7b7f4961a06797af8f8fc2ee4814fac5a2224382aa1f36f6199a0f` |
| Pool destructor `0x237e790` / `8` | `a2273e47a001f09be28f9789aa4c91d78b56f793e4ba197edd0f11c37cf9f3fb` |
| Control-block destroy `0x6d0db8` / `4` | `1cec6b3c0648dc55ffc8ab621965bb0d78dc659d0eaaeb839e47929790a1b505` |

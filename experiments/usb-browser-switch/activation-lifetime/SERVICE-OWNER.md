# BrowseSourceSelect: native owner found, queued capture still borrowed

The source service has a concrete native owner outside SourceSelectView:
`gui::GuiObjectManager` stores its allocated `BrowseSourceSelect` at offset
`+0x178` and invokes its deleting destructor when that slot is replaced or the
manager is destroyed. This resolves where the service's storage is owned.
It does **not** provide a new retain operation, an independently acquired service
handle, or permission to bypass the guarded DeviceLibrary action.

The queued setSource task still carries a raw service pointer. The shared
control block in the service owns its task allocator, not the service or the
shared task queue. No service-specific cancellation/drain contract was
established here. The native activation and complete wrapper gates remain
**CLOSED**.

This is an offline read of the same ELF64 little-endian AArch64 ET_EXEC image,
SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
It follows [the full service ABI review](../activation-abi/SERVICE-REVIEW.md)
and [the existing task-box ownership review](../../mods-shortcut-delivery/route-task-ownership/TASKBOX.md).
No firmware code, hook, emulator or device was run. No previous verifier or
passing test suite was replayed.

## Native manager ownership

The service's vtable address point `0x317de60` gives:

| Slot | Target | Function extent |
|---|---|---|
| `+0` | Non-deleting destructor `0x20a0ae8` | `0x9cc` bytes |
| `+8` | Deleting destructor `0x20a14c8` | `0x28` bytes |
| `+0x10` | `setSource`, `0x20a1550` | Previously reviewed `0x1024` bytes |

Constructor `0x20a3020` has extent `0x1de0`. It installs this service
vptr at `0x20a309c/30ac`. A direct call-site search found the construction
sequence inside `0x217e968`, whose installed vptr `0x2cd9988` has RTTI
`gui::GuiObjectManager`:

1. `0x217fce0..fcec` requests a `0x90`-byte allocation, keeps the returned
   address in `x20`, and calls the service constructor.
2. `0x217fcf0/fcf4` reads the manager's previous `+0x178` pointer and replaces
   it with that constructed address.
3. If the previous pointer was nonnull, `0x217fcfc..fd04` calls its virtual
   deleting-destructor slot `+8`.
4. The manager destructor `0x1a2cac0` later loads this same field at
   `0x1a2cb54` and calls slot `+8` at `0x1a2cb64`.

That is concrete delete-on-replacement and delete-on-manager-destruction
behavior. It is sufficient to identify native owning storage without assigning
an unproved C++ smart-pointer type. It is not evidence that the slot cannot be
replaced, that the manager is immortal, or that external code may retain its raw
pointer after lookup.

The service constructor also contains the interface object-map registration
path: the diagnostic names
`ObjectMap::regObject<gui::source_select::IBrowseSourceSelect>`, and the
normal insertion path at `0x20a356c/3570` places the raw service address into a
map node's value slot. A registry entry is not itself a reference-counted service
owner or a lease available to a custom caller.

The manager destructor performs earlier virtual calls involving fields
`+0x1a0` and `+0x198`. Their transitive shutdown behavior is outside this
review. Consequently this report does not claim that the complete manager
shutdown lacks serialization or drainage. It only identifies the actual holder
and the local destruction routes.

## The embedded task box owns a pool

The service constructor builds a `TimerTaskBox` subobject at service
`+0x30`. The relevant layout is established by explicit stores:

| Service offset | Construction evidence |
|---|---|
| `+0x30` | Vptr `0x321b108`, stored at `0x20a3130`; RTTI is `meow::TimerTaskBox`. |
| `+0x38` | Pointer to an embedded DefaultMemPool at allocated control block `+0x10`, paired with that vptr store. |
| `+0x40` | The pool's control-block pointer, stored at `0x20a3138`. |
| `+0x48` | Optional post-notification pointer, initialized null by the paired store. |
| `+0x50` | TaskNo counter, initialized zero at `0x20a313c`. |

The constructor allocates the `0x20`-byte control block at
`0x20a30d4/30d8`, initializes both 32-bit count words to one at
`0x20a30f8`, and installs control vptr `0x2900d38`.
Its RTTI identifies
`std::_Sp_counted_ptr_inplace<meow::AsyncTaskBoxBase::DefaultMemPool, ...>`.
The embedded pool's own vptr is `0x321af58`.

The last-strong control callback `0x6d0b30` resolves the object at control
`+0x10` and invokes its destructor slot `+0`. For the constructor-installed
pool vtable, that is `0x237e790`: clear pool `+8`, then RET.
The weak-control destroy callback `0x6d0db8` takes the deallocation tail.
Neither of those concrete targets destroys or retains BrowseSourceSelect.
This distinguishes the control block's actual object from the service whose
task allocation uses it.

The service destructor first invokes dependency slot `+0x18` with its
embedded listener addresses (`0x20a0b3c` and `0x20a0b58`), destroys the two
mapped dependency members, and frees listener-array storage at service
`+0x58`. It then decrements the strong count of the control pointer loaded
from service `+0x40`; last-strong and last-weak paths dispatch the callbacks
above. Finally it clears its two listener bookkeeping words.

The deleting destructor calls this non-deleting body at `0x20a14d8`, then
takes the sized-deallocation tail at `0x20a14ec` with size `0x90`.
The local pool release is not a queue drain. The dependency callbacks and their
transitive effects are not replaced by an absence-based whole-program claim.

## Shared queue and task-specific destruction

The constructor calls `0x237a140` at `0x20a3140`. That getter returns the
shared `TimerTaskBox::Impl` pointer stored at `0x4043ad8`; the pointer is
separate from the service's pool control. The already reviewed posting route
uses this Impl's embedded AsyncTaskBox at `Impl+0x20`.

The service-specific queue submission is now tied to that route:
`0x20a16b8` forms `service+0x30`, then `0x20a16c4` calls
`0x237a440`. TaskNo comes from service `+0x50` and is stored in task
`+0x10`. The returned TaskNo is tested for zero before the optional posting
notification; this local sequence does not publish an owned completion or
cancellation handle to the original source-action caller.

The setSource task uses vptr `0x317e0f0`:

| Task slot | Target | Actual local role |
|---|---|---|
| `+0` | `0x209f848` | Non-deleting task destructor |
| `+8` | `0x209f918` | Same bookkeeping followed by deletion of `0x38`-byte task storage |
| `+0x10` | `0x20a2578` | Execute copied source change using the raw captured service |
| `+0x18` | `0x209f4f8` | Diagnostic-value method; not a retain operation |

Both task destructor bodies decrement the specialization-global counter at
`0x40130b8` and clear task `+8`. They do not load the captured service at
task `+0x18`. That counter therefore does not release an independently retained
service. The capture stores at `0x20a1670/1674` remain a raw service address
plus copied source bytes.

The executor loads task `+0x18` at `0x20a258c` and immediately reads service
`+0x28` at `0x20a259c`. Listener execution can be followed by further service
reads at `0x20a26d4`, `0x20a26f4/26f8` and `0x20a2748`. These are concrete
dependencies on live service storage. They are not a claim that every return
path rereads it or that the native program currently violates its own lifetime
rules.

The existing task-box review establishes real generic mechanisms:
`TimerTaskBox +0x10 -> 0x237fdc0 -> AsyncTaskBox::cancelTask 0x2376130`
for a TaskNo, execution-side matching that can skip a queued task, and the
AsyncTaskBox destructor's pending-task cleanup loop. Cancellation depends on
its configured auxiliary context; removal from a queue is not proof that an
already executing callback has returned. None is automatically performed by
the concrete DefaultMemPool destructor identified above, and no service-specific
use of those mechanisms was established by this review. The unchanged
task-box evidence was read, not rerun.

## Scope and next ownership gate

The positive result is a native manager owner, a separate shared queue, and
precise distinction among service storage, task storage and pool control.
There is still no independently acquired service lifetime valid across arbitrary
posting, navigation, reentrant listener calls or manager teardown. Static
ownership alone also does not make direct setSource calls a valid replacement
for the guarded DeviceLibrary action.

A next bounded review can resolve the manager's earlier shutdown participants
and their relationship to shared queue producers/consumers, or establish an
actual native-owned invocation scope that keeps the manager/service alive
through the permitted action. It must account for already selected callbacks,
concurrent producers, exceptions and the live implementation selected by the
registry. No new cancellation, drain, reference-count write, post-original
native read or custom activation follows from the addresses above.

Complete function body and FDE/CIE fingerprints for the twelve selected owner,
service, task and pool entries are retained privately with 84 semantic anchors.
LSDA addresses are recorded only; **no new LSDA or exception-path execution
proof is claimed**. The earlier full service ABI findings remain unchanged.
Raw firmware, disassembly and analysis captures remain private.

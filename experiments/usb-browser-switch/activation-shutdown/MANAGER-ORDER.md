# GuiObjectManager's two earlier SubDisplay destructors

This static checkpoint resolves the manager members at `+0x1a0` and
`+0x198`, which are destroyed before `BrowseSourceSelect` at `+0x178`.
They are two instances of the same concrete
`gui::sub_display::SubDisplay` type. Their immediate destructors perform
local member cleanup. This review does **not** establish a drain, cancellation
receipt, or serialization boundary for the service's queued raw-pointer tasks.

The input is the same private ARM64 ELF used by the preceding
[SERVICE-OWNER review](../activation-lifetime/SERVICE-OWNER.md), SHA-256
`6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
The manager constructor/destructor bodies are already pinned by that review;
this checkpoint adds relation anchors in those bodies and new SubDisplay/member
body fingerprints. It performs no native call or runtime observation.

## Two concrete objects and the actual virtual slot

The manager constructor `0x217e968` initially clears both slots at
`0x217ea54`. It allocates `0xc0` bytes twice and calls the same
constructor `0x2164f70`:

| Manager member | Constructor call | Selector argument | Store into manager |
|---|---:|---:|---:|
| `+0x198` | `0x217fd84` | `w1 = 0` | `0x217fd8c` |
| `+0x1a0` | `0x217fe04` | `w1 = 1` | `0x217fe0c` |

The constructor retains that selector at object `+0x1c`
(`0x2164f98`, `0x2165020`). Its primary vptr is `0x31a9288`,
installed at `0x2165008`; RTTI `0x31a9170` names
`gui::sub_display::SubDisplay`. The primary address-point slots are:

| Offset | Target | Observed contract |
|---|---:|---|
| `+0` | `0x21649e0` | Returns whether the byte at object `+0x70` is nonzero. |
| `+8` | `0x2164d38` | Nondeleting destructor. |
| `+0x10` | `0x2164ee8` | Calls that destructor, then sized delete with `0xc0`. |

Thus the manager's `+0x10` virtual calls are deleting destructors for this
actual type. They must not be interpreted as a generic stop method from a
different vtable convention. The manager invokes the `+0x1a0` object at
`0x1a2caf8`, then the `+0x198` object at `0x1a2cb0c`.
It subsequently destroys other members before the already-reviewed service
deleting call at `0x1a2cb64`. This establishes call order on that destructor
path, not completion of other threads or pending callbacks.

## Immediate SubDisplay destructor contract

The complete `0x2164d38` body is `416` bytes. After installing its own
vtables, its normal path does the following in order:

1. Calls the object-`+0x28` mapping helper `0x2166318` with `w1 = 1`.
   It calls the mapped object's virtual `+0x30` getter, then the returned
   object's virtual `+0x18` operation with the SubDisplay pointer as argument
   (`0x2164d80`–`0x2164da4`). Construction calls the same getter and
   returned-object virtual `+0x10` with the same pointer
   (`0x21653a8`–`0x21653cc`). This is a paired
   registration/removal-shaped interaction; the two returned-provider targets
   and their synchronization semantics are not resolved here.
2. Calls `0x2499570` on object `+0x90`. That entire four-byte helper is a
   tail branch to the imported `pthread_mutex_destroy`.
   The caller immediately continues with cleanup and does not branch on the
   returned status. Destruction of this local mutex is not a demonstrated
   wait, task cancellation, or successful serialization receipt.
   Its constructor `0x24991c0` performs the corresponding pthread attribute
   setup and mutex initialization.
3. Calls the mapping cleanup helper `0x7288e0` on object `+0x80`,
   then conditionally destroys the members at `+0x78`, `+0x60`,
   `+0x68`, and `+0x58` via their actual virtual slots.
4. Releases the shared-control references associated with the pairs at
   `+0x48/+0x50` and `+0x38/+0x40`. The body decrements the strong count;
   only the last-strong path invokes control slot `+0x10`, followed by
   the weak-count/deallocation path. It does not establish that these are the
   last references.
5. Calls mapping cleanup `0x21644a0` on object `+0x28`, clears the local
   type field at `+0x18`, and returns. The deleting wrapper then frees the
   SubDisplay allocation.

The constructor-installed members make those immediate virtual calls concrete:

| SubDisplay member | Installed concrete type and identity | Immediate destruction boundary |
|---|---|---|
| `+0x78` | `meow::HuiButtonDoubleClick`; constructor `0x2360890`, secondary vptr `0x3216868` | Slot `+8` is secondary deleting thunk `0x21662c0`. It frees a local allocation, destroys its own mutex, and sized-deletes `0x80` bytes. This is not the service task box. |
| `+0x60`, `+0x68` | Two `gui::sub_display::TrackInfoUpdater` objects; constructor `0x21751b0`, primary vptr `0x31acdb8` | Slot `+8` at `0x2175158` calls destructor `0x21750a8`, then sized-deletes `0x90` bytes. The destructor invokes virtual cleanup of five owned members and the `+0x28` mapping helper; those descendants remain a transitive boundary. |
| `+0x58` | `gui::sub_display::PlayerInfoCache`; constructor `0x2163da8`, vptr `0x31a8d48` | Slot `+0x18` at `0x2163c38` releases two shared controls, invokes its mapping cleanup, and sized-deletes `0x40` bytes. |
| Shared pairs `+0x38/+0x40`, `+0x48/+0x50` | In-place shared controls for `gui::sub_display::PlayerInfoData`; control vptr `0x31a9338` | Last-strong slot `+0x10` at `0x2166108` adjusts to the contained object and branches through its first virtual slot. The contained object's full teardown is not followed here. |

The exact imported local deallocation operations are `free` and
`operator delete(void*, unsigned long)`; the mutex imports are
`pthread_mutexattr_init`, `pthread_mutexattr_settype`,
`pthread_mutexattr_setprotocol`, `pthread_mutex_init`,
`pthread_mutexattr_destroy`, and `pthread_mutex_destroy`.
No success interpretation is inferred merely from calling any of them.

## Bounded evidence and remaining gate

The new input set contains 14 complete FDE-bounded function extents, plus
selected relation instructions, vtable words, RTTI names, and import bindings.
The central body fingerprints are:

| Function | Size | SHA-256 |
|---|---:|---|
| SubDisplay constructor `0x2164f70` | 2072 | `54ff5ae1ca34732e91a5606007d3ef70356ce078d46e291120550af95c4abf60` |
| SubDisplay destructor `0x2164d38` | 416 | `8da5aa5f3b7402861767f4e1a098988e05d7ca0df4b0be0f3953ebdcbcba79d6` |
| Deleting wrapper `0x2164ee8` | 40 | `35202130751d67ec17f50122c0f52bd9fb042826e49ba6e3891252024ff01d6c` |
| Local mutex destructor `0x2499570` | 4 | `f791b9d193597b2ad8affb7797956c86b947f24dc54d7405e99ca9be6b1bd042` |

FDE/CIE bytes and decoded CFI row counts are retained as extent metadata.
No LSDA is decoded by this checkpoint, and constructor exception paths are
not promoted into a successful lifetime contract.

The complete immediate SubDisplay destructor contains no direct call to the
reviewed `setSource` service, shared task-queue destructor, or reviewed
task-cancellation functions. That direct-call observation is deliberately
narrow: provider virtual calls, mapping helpers, updater descendants, and
last-strong data destruction can have further effects. This checkpoint neither
asserts that such effects are absent nor establishes them as a service barrier.

The earlier manager order therefore supplies concrete local object teardown,
but does not make a queued task's raw `BrowseSourceSelect*` independently
owned or prove that the task is finished before service destruction. Native
activation, full wrapper ABI/unwind support, live operation-scoped retention, and service-task
quiescence gates remain **CLOSED**.

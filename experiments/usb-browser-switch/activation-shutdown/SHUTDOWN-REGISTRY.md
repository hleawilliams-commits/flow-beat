# Native shutdown registry and ordinary application exit

The selected ordinary application runner returns from its message-dispatch
loop, invokes application shutdown, deletes the application, and only then
invokes the DeletedAtShutdown registry dispatcher when the shared GUI
initialization count reaches zero. For the concrete EP147Application vtable,
application shutdown/destruction releases UiLayerObjects and its GuiObjectManager
before that later registry pass can delete TimerTaskBox::Impl.

This is a static ordering result, not a live owner lease or proof that all
producers, callbacks and alternate teardown paths are quiescent. Native
activation and complete wrapper gates remain **CLOSED**.

The inspected image is the same private AArch64 EP147 ELF, SHA-256
`6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
The preceding [shared-queue review](../activation-lifetime/SHARED-QUEUE.md)
supplies the unchanged Impl registration and destruction routes. Their retained
disassembly was read; no old verifier or test suite was replayed. No firmware,
hook, emulator or device operation was executed.

## The actual registry deletion dispatcher

Function **`0x2502ad0`**, extent **`0x21c`**, operates on the same registry
used by the already reviewed DeletedAtShutdown constructor and destructor.
Its base is `0x4044398`: lock `+0x50`, initialization guard `+0x58`,
array pointer `+0x60`, capacity `+0x68` and current length `+0x70`.

The selected body performs these operations:

1. Acquire the registry lock at `0x2502af0`, read its length, allocate
   a separate pointer array and copy the current entries at `0x2502b24`.
   Release the lock at `0x2502b2c`.
2. Traverse that copied array from its last entry toward its first.
   For each candidate, reacquire the registry lock at `0x2502b64`
   and search the current registry for the same pointer.
3. Skip a candidate that is no longer registered. For a still-registered,
   nonnull candidate, release the lock at `0x2502c08`, load its virtual
   deleting-destructor slot `+8`, and call it at `0x2502c1c`.
4. After traversal, free/reset the registry array and length, then free the
   copied array. The final free/reset section is not enclosed by an additional
   registry-lock acquisition in this body.

This is a concrete deleteAll-style dispatcher, including reverse snapshot
order and a membership recheck. It is not a queue cancellation primitive.
The lock is released before invoking each object's destructor; it does not
itself wait for a selected timer callback or give custom callers a safe
concurrent shutdown protocol. New registrations or arbitrary concurrent
mutations are not assigned a guarantee by this local review.

For the registered Impl secondary base, the preceding vtable evidence maps
slot `+8` at address point `0x321b0e8` to `0x2380590`. That destructor
reaches the embedded AsyncTaskBox cleanup, then unregisters the
DeletedAtShutdown base, destroys the Timer base and deletes Impl storage.
This route is separate from the originating timer/service DefaultMemPool
release.

## A real application runner calls it after loop return

The normal continuation in **`0x6a8980`** has this explicit order:

| Site | Operation |
|---|---|
| `0x6a89bc` | Call the application's initialization predicate at virtual slot `+0x70`. |
| `0x6a89cc` | On its successful branch, call message-dispatch loop `0x2508c60`. |
| `0x6a89d4` | After that call returns, call shutdown wrapper `0x2509760`. |
| `0x6a89e8` | Call the application's virtual deleting destructor at slot `+8`. |
| `0x6a89ec..0x6a89f8` | Decrement the GUI initialization count and test for zero. |
| `0x6a8a18` | On zero, call registry deletion dispatcher `0x2502ad0`. |

The initialization-false branch skips the dispatch loop and still enters the
subsequent shutdown/deletion sequence. A nonzero count skips this particular
registry invocation; the ordering above does not assert that every exit must
perform it.

The inspected loop **`0x2508c60`**, extent `0x4c4`, checks an atomic
stop word at its receiver `+0xc`. Its concrete message callback call is
`0x2508ea4`; the normal return at `0x2508ef4` follows the loop's stop
checks. Thus this runner cannot execute its post-loop shutdown while a
synchronously invoked callback on this same outer loop call is still on the
stack. Merely nesting another loop does not make the outer runner advance
past that callback.

That positive call-stack ordering is narrower than complete shutdown safety.
A callback could invoke a separate destructive/replacement path before
returning. Other producers, other dispatch contexts, exceptions, abnormal
termination and alternative shutdown entry points remain separate questions.
The static loop stop word is not evidence that they have all stopped.

## Concrete application-to-manager ownership

The vtable address point **`0x2cda800`** has RTTI **`0x2ccad28`**,
named `EP147Application`. Its slot `+0x30` is `0x22f9f00`;
slot `+8` is deleting destructor **`0x1a38450`**, which calls
non-deleting destructor **`0x1a38250`** and then deletes `0x120` bytes.

The shutdown wrapper `0x2509760` calls the actual application's
`+0x30` slot at `0x25097a8`. The concrete EP147 shutdown body contains
listener calls and a later virtual `+0xa0` call at `0x22f9fec`.
EP147's static `+0xa0` slot points to **`0x19d3a20`**. That body
performs several earlier participant calls, then invokes virtual `+0x110`
at **`0x19d3abc`**. EP147's corresponding slot at `0x2cda910` is
**`0x1a380a0`**, the owned-member reset below.

This supplies a concrete **ordinary shutdown** caller for the reset. The
reset is not, by its existence alone, evidence of reentrant destruction.
Its manager cleanup occurs during the selected shutdown path, before the
subsequent application deletion and registry pass. The earlier participant
calls are not assigned a stop/drain contract. The deleting-destructor route
also releases any remaining owned member after the shutdown wrapper returns.

EP147Application destructor `0x1a38250` loads its **`+0x110`** field.
For the expected UiLayerObjects deleting-destructor target, it calls
`0x1a37f20` at `0x1a382b4` and deletes `0x28` bytes; the alternative
actual virtual target remains an explicit branch. The separate native member
reset body `0x1a380a0` clears the owning slot before an equivalent release.
The reset is reached by the ordinary shutdown virtual sequence just identified;
it does not supply a new externally callable lifetime operation.

The owned type is **`ui::UiLayerObjects`**, identified by RTTI
`0x2ccabd0` and vptr `0x2cd99a8`. Its constructor `0x22588a0`
allocates `0x1a8` bytes, invokes GuiObjectManager constructor
`0x217e968`, and stores the returned object at UiLayerObjects **`+0x10`**
at `0x2258ba0`. Native build body `0x1a384a8` separately constructs
UiLayerObjects and stores it in the application's `+0x110` slot. EP147's
static virtual slot `+0xd0` points to this build/replace body; its other
callers are not classified here, and no reentrant invocation is asserted.

UiLayerObjects destructor **`0x1a37f20`** loads `+0x10` and, on
the known-target branch, calls GuiObjectManager destructor **`0x1a2cac0`**
at `0x1a37f90`, followed by sized deletion. Its deleting form
`0x1a388e0` has the corresponding route. The
[service-owner evidence](../activation-lifetime/SERVICE-OWNER.md) already
establishes that GuiObjectManager destroys BrowseSourceSelect from
manager `+0x178`.

Consequently the selected normal runner, using these concrete constructor-assigned/static
vtable targets and ordinary returns, destroys the application's service owner
before the later registry deletion pass. If the shared Impl remains registered
at that pass, its concrete registry destructor is reached afterward. This
does not prove which factory or receiver is selected in a live invocation,
that Impl survives all earlier shutdown participants, or that every execution
uses the known-target branches.

## Remaining boundary

The new evidence establishes a real registry dispatcher, its concrete caller,
normal outer-loop return ordering, the ordinary shutdown member-reset caller
and the application-owned manager chain. It does not establish an
operation-scoped live retain for a custom source activation. Whether other
callers can invoke destructive/replacement paths during a proposed operation
remains unresolved. Alternate shutdown entry points, in-flight work outside
this loop, registration changes and the earlier manager shutdown participants
also require their own evidence.

Thirteen newly inspected complete function extents and selected instruction,
RTTI and import relations are retained privately for the aggregate check.
FDE/CIE records and LSDA addresses are fingerprinted; **no new LSDA tables
or exception-path execution proof are claimed**. Raw firmware and
disassembly remain private. No callback cancellation, direct setSource call,
reference-count mutation, hook, post-return native read or USB button was
introduced by this review.

# SourceSelectView owners, navigation and listener removal

The inspected native construction path gives SourceSelectView **two concrete stored reference owners**: `gui::ViewObjectManager` at member `+0x90`, and a registered-view array in its dispatcher at `+0x138`. Both paths increment the view's native count at `+0xd8`, and their teardown paths decrement it and call the deleting destructor at zero. Ordinary `goToView` selection uses a separate current-view pointer at dispatcher `+0x150`; its reviewed body switches selection and sends false/true notifications without directly removing those stored reference owners.

That is stronger than merely finding matching pointers. It still does not establish a callback-duration retain, a lifetime-safe lookup of either owner, or exclusion of reentrant owner destruction. A new activation operation inside the timer callback remains gated.

This review uses only the exact private EP147 image, SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`. The [timer teardown review](../activation-abi/TIMER-REVIEW.md), [activation ownership review](../activation-ownership/README.md) and [task-box ownership review](../../mods-shortcut-delivery/route-task-ownership/TASKBOX.md) remain applicable. No native function, hook, emulator/device endpoint or earlier test suite was executed. Raw disassembly and exploratory analysis remain private.

## The manager owns a counted view

RTTI at `0x2d43290` names `gui::ViewObjectManager`; its vptr is `0x2d436e0`. The manager constructor `0x1af2770` initializes its view slots, including `+0x90` at `0x1af27e8`, then calls the view-building routine `0x1af1898`.

For SourceSelectView, that routine allocates `0x3b0` bytes and calls constructor `0x1b2d248` at `0x1af1ec8`. The replacement sequence:

- increments the new view's 32-bit count at `+0xd8` using the exclusive atomic loop `0x1af1ed8..0x1af1ee8`;
- stores the new view at manager `+0x90`, at `0x1af1ef0`;
- decrements any replaced view's count at `0x1af1ef8..0x1af1f08`, invoking its deleting virtual slot `+8` through `0x1af24d4..0x1af24dc` when the count reaches zero.

The manager destructor `0x1af12d0` performs the corresponding release of `+0x90` at `0x1af13c4..0x1af13e0`, with the zero-count deleting call at `0x1af1680`. The SourceSelectView deleting slot is `0x1b2d0a0`: it calls the complete destructor `0x1b2c9e0`, then sized delete with `0x3b0`.

These are stored ownership operations, not a usable retain API for this feature. The reviewed manager query at `0x1af1000..0x1af1004` loads the cached view and calls `0x1b29648`, which returns a child visibility-bit predicate. That path does not acquire or return an owning view reference. No safe custom manager lookup or callback-scope owner acquisition is established.

## Registration adds a second stored owner

After building the views, the manager constructor loads its dispatcher at `+0x18` and SourceSelectView at `+0x90`, then calls registration wrapper `0x1afb6e0` at `0x1af2a18`. That wrapper first calls `0x1b00530`.

For the constructor's selected insertion path, `0x1b00530` puts the view pointer into the dispatcher's array at `+0x138`, then increments view `+0xd8` at `0x1b00628..0x1b00638`. It updates the array count and invokes a dispatcher notification. The alternate append path also increments the same view count. The routine additionally stores dispatcher `+0x128` as a raw backlink in view `+0xe0`; that backlink store is a different relationship and is not the retain.

The paired dispatcher-base destructor `0x1affb90` drains the registered array. At `0x1affc74..0x1affc94`, it removes an entry from the count and atomically decrements the view's count. Zero reaches virtual deleting slot `+8` at `0x1affca0`. The array storage is freed afterward. This confirms reference ownership of registered entries, rather than inferring ownership from the array's pointer type.

The registration function has replacement/reordering, indirect notification and exception paths. This review does not claim that registration cannot fail, that all views are always registered, or that the constructed manager/dispatcher remains live throughout arbitrary reentrant calls.

## Ordinary navigation selects without directly deleting the stored owners

Diagnostic text identifies `0x1b021d0` as `meow::ViewDispatcher<meow::HuiAdaptedView<>>::goToView`. The reviewed successful path:

1. Reads the old selected view from dispatcher `+0x150`, removes its component relationship, and calls its virtual slot `+0x178` with `w1=0` at `0x1b02344`.
2. Attaches the requested view and stores it at dispatcher `+0x150`, at `0x1b02364` (or `0x1b02478` when there was no previous view).
3. Calls the selected view's same virtual slot with `w1=1`, at `0x1b023d8`, then performs further native notifications.

This complete body has no direct intrusive reference-count adjustment or registered-array removal. Its current-view field is therefore distinct from the counted registered entries and manager cache. Within this inspected operation, deselection is not the same event as deleting SourceSelectView. Indirect callbacks can have broader effects; the absence of direct count operations does not prove a transitive destruction exclusion.

For the constructor-installed SourceSelectView vtable, slot `+0x178` is `0x1b2bda8`. This resolves the boolean's role as deselection/selection notification without inventing a C++ method name:

- Nonzero attaches the view's component at `+0x2c0` and starts embedded timer **IDs 0 and 3**, each with interval argument `100`, through `0x250a610`.
- Zero stops **IDs 0 and 3**, through calls at `0x1b2be48` and `0x1b2be54`, before detaching that component and updating child visibility/state.
- Separate view methods `0x1b2bd38` and `0x1b29cc0` likewise start or stop IDs 0 and 3.

The complete notification and separate stop bodies do **not** stop IDs 1 or 2. This is a bounded body fact, not proof those IDs are active or unused throughout the application. In particular, the earlier ID-1 no-op listener candidate must not be assigned an observed schedule or hidden-view behavior from these ID-0/3 operations. Stopping a registered timer also does not prove that an already selected callback or separately queued task has finished.

## Destruction removes the listener before the embedded timer

The constructor builds the embedded StateRefreshCycleTimer at view `+0x348` and appends the view's `+0x1b8` listener to its listener array at view `+0x370` (timer `+0x28`). The complete view destructor has explicit local removal/order behavior:

- `0x1b2caf4..0x1b2cafc` clears callback/backlink fields `+0x90`, `+0xa0` and `+0x70` on the children loaded from view `+0x328`, `+0x318` and `+0x310`.
- `0x1b2cb28..0x1b2cb70` searches the timer listener array for the exact listener pointer `view+0x1b8`. On a match, `0x1b2cefc` decrements its count, and the following path shifts remaining entries and may shrink storage.
- Only afterward, `0x1b2cbd0..0x1b2cbd4` invokes the embedded timer destructor. Other owned children and the base view are destroyed subsequently.

Pointer equality identifies which listener to remove; it is not a retain of that listener. The local removal contains no direct wait for an invocation already in progress. As the preceding timer review shows, both the direct and queued listener loops may read the embedded timer again after a listener returns. Removal before destruction narrows the native teardown order but supplies neither an in-flight invocation lease nor a token canceling the raw timer captured in a queued task.

The enclosing dispatcher destructor also constructs a concrete `juce::MessageManagerLock` at `0x1b038fc`, with null thread argument `x1=0` set at `0x1b038f4`. The independently reviewed constructor's selected null-thread path (`0x2504630..0x2504650`) retries its acquisition operation until low-byte success and stores the gained status. The destructor's false-status logging branch exists, but it is not a demonstrated normal-return acquisition failure for this constructor path. Its mere presence therefore does not establish ordinary fail-open teardown.

The acquisition path recognizes the message thread/current holder, or uses a posted `BlockingMessage` handshake whose callback waits for release. This is concrete synchronization beyond the timer schedule mutex; it is not a view retain or queued-task drain. The dispatcher calls the guard destructor `0x2504450` at `0x1b03960` **before** its later `0x1affb90` registered-view release. Direct execution/reentrancy on the message thread and the lifetime of queued raw captures remain separate proof obligations. A larger guard scope must not be inferred from the constructor or diagnostic name.

## Exact extents and remaining proof

The new central function bodies are bounded by the exact ELF unwind entries:

| Function | Extent | SHA-256 |
|---|---|---|
| Manager view construction `0x1af1898` | `0xed8` | `f1377e7ecc4e16dbdd8d2b3a0b58090e0899a084a3a09fc6c7a6233ff116d9a0` |
| Manager construction `0x1af2770` | `0x95c` | `1d4d6567fb12ea698f6dc245845209177b066604fbb478bd4bfd8b0d483df9e6` |
| Manager destruction `0x1af12d0` | `0x554` | `8e5585a1057eb048c4668bf8289dcfd6f3168051ae969ee7907e95372e050761` |
| Registered-view insertion `0x1b00530` | `0x478` | `e887edd8f9b134361cf2a3a114758ebfc95ef3c24d4cbfd4242ca601b00b72cf` |
| Registered-view release `0x1affb90` | `0x1b4` | `6494de843890dadaf9084a35746bbcda356d458034d98357ad3a675bfb752fab` |
| Native `goToView` `0x1b021d0` | `0x400` | `6dc4b33a4cad4414d9caf6c971323d1dc4ffb9bf96c83f71d3d5947b4030f9a7` |
| View selection notification `0x1b2bda8` | `0x150` | `558b2b5185064f6836a009735dd917c878a01f9409175e97167a86466134c32d` |
| Dispatcher destruction `0x1b038a0` | `0x1bc` | `b94831044a09648d271b63091ab373414e9acdd2cd3640b9c33e4ee9b5e42423` |

The reviewed manager-field writes are its initialization, view-building replacement and destructor release paths. This is not an exhaustive all-image writer or alias inventory. No count was modified and no native reference was acquired.

The actionable remaining gate is narrower: establish which live manager/registered owner spans the proposed callback and whether native task execution, navigation and destruction are serialized for that exact scope, including reentrant notifications and queued raw captures. A counted owner surviving ordinary selection is useful evidence; it does not prove that a custom operation may destroy or replace that owner during the same callback. No native lifetime bug, safe activation, completion, cancellation, hook eligibility or new USB feature is claimed here.

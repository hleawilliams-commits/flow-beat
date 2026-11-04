# USB native row display contract and next observer plan

The native DeviceLibrary row feeds separate image and text consumers. Its
kind 0/1 image lookup selects assets named for local-library USB/SD media,
while the corresponding text getter returns an empty string. This closes a
static resource relation, not the current visible label-to-action correlation.
The USB-media asset does not identify USB1 versus USB2.

[ROW-LABEL.md](ROW-LABEL.md) records the exact row/component, virtual-slot,
resource-key and text relations. No asset image was unpacked or rendered, and
an exact-basename search found no existing extracted copy of those two assets.
Actual pixels, live row/provider identity and current guarded-action arguments
remain unproved.

## New static verification

One first-time run of the unchanged
[exact-image verifier](../activation-abi/verify-static.py) passed:

| Checked subject | Count |
|---|---:|
| New complete function/FDE extents | 6 |
| Selected instruction relations | 394 |
| Pointer relations | 6 |
| Short string relations, including RTTI and resource names | 11 |
| Imported symbol relations | 4 |
| Newly decoded LSDA tables | 0 |

[Pins](review-pins.json), [result](result.json), and
[verification receipt](verification.json) preserve the subject, hashes and
independent source/retained-byte review. The assembler excludes all 95 earlier
full extents in the ABI, lifetime, shutdown and source-value reviews. Some
selected relations connect frozen constructors/helpers; no preceding complete
pin set or runtime suite was replayed.

The full 9,980-byte cache initializer is one FDE-bounded fingerprint. Semantic
claims here cover its key-0/key-1 image branches and their direct consumer,
not all resources or exception paths. Complete-body metadata does not certify
all semantics. The verifier's inherited generic scope label is still
"static exact-ELF ABI and concrete timer teardown only"; this run's actual
subject is the new row-display pin set. FDE/CIE and LSDA-pointer metadata
supply no new wrapper ABI, unwind or cancellation proof.

The exact private image remains SHA-256
6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925.
Raw captures, scripts and diagnostic logs remain private. No firmware was
executed and no emulator or device was contacted.

## Concrete next implementation

[NEXT-OBSERVER.md](NEXT-OBSERVER.md) specifies a new offline passive adapter
for guarded-action and final-service entry snapshots. It is a plan only:
no new wrapper source, build, test or native adapter was added in this checkpoint.
The old task-executor tests do not validate this proposed wrapper.

The plan calls for saving full fixed-width register/control state in an
800-byte private frame, capturing only entry register values, then restoring
state except for x16 tail-transfer scratch. The existing installer's x17
scratch is also outside a pre-installer state guarantee. The guarded action's x2 remains opaque pointer bits; it does
not read its eight-byte pointee. The service Source is copied from register
values. The plan carries no pending return state, cross-entry event pairing,
post-return native reads or fabricated return-type assumption.

Implement this as a separate offline fixture with discriminating register,
stack, result, nesting/exception and helper-backtrace checks plus safe negative
controls. Review the exact contract and exclusions before implementation.
No hook installation follows from a passing fixture: installer scratch/island
unwind, actual owner scope, writer exclusion and stopped recovery remain gates.

## Evidence and gates retained

Historical V2–V4 named USB choices and their Source values remain valid within
their retained export fixture. Resource names do not replace the missing live
row/action-kind/provider correlation, fresh media identity or accepted intent.
A nonnull provider can still be the native fallback object. Native source
normalization remains structural, and row-update return means a flag changed.

Normal outer-loop shutdown ordering remains positive but bounded. Alternate
replacement/destruction, reentrant and queued lifetime, callback quiescence,
final completion, asynchronous cancellation, missing media, alternate databases
and scroll/sort remain open. No native activation, USB button, reference-count
mutation, core composition, installation, catalog/package change or release was
added. All existing runtime and product gates remain **CLOSED**. Do not replay
this passing pin set unchanged.

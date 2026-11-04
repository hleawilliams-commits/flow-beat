# DeviceLibrary carrier provenance

The native action receives an eight-byte carrier resolved from a
**Source-keyed provider and a DeviceLibrary row kind**. The row does not simply
store the MediaID from which it was constructed. This checkpoint resolves
the intervening provider lookup, its real fallback object, and the provider's
carrier getters/enumerator. It does not establish a current live provider,
a USB-port enum API, or which DeviceLibrary kind is shown as “Local Library”
in a particular live UI.

The input remains the exact private AArch64 image with SHA-256
`6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
No native function, hook, emulator or device was exercised. The preceding
[guarded-action ABI review](../activation-abi/ACTION-REVIEW.md) and
[ownership review](../activation-ownership/README.md) remain unchanged.

## Distinct values in the row-to-action path

These values occupy different namespaces:

| Value | Native storage/use |
|---|---|
| Parent source descriptor | A 12-byte `Source`: discriminator at offset 0 and eight-byte MediaID carrier at offset 4. The controller keeps it at `+0x78`; a DeviceLibrary row keeps a copy at `+8`. |
| DeviceLibrary row kind | A separate 32-bit value at row `+0x14`. It selects a provider getter. It is not the Source discriminator or a numeric USB-port identifier. |
| Candidate MediaID | An eight-byte entry returned by the provider's enumeration. Its type bits feed the native kind mapper during row construction. |
| Action carrier | A newly resolved eight-byte result from `0x1be5798`, stored in the native caller's stack before the guarded action. |
| Display label | UI text such as “USB1”, “USB2”, or “Local Library”. This checkpoint does not infer a kind or identity merely from that text. |

The frozen selected-row caller `0x1be8a20` already establishes stride
`0x20`, the kind load at `0x1be8a80`, resolver call at `0x1be8a88`,
and full returned-`x0` store at `sp+0x38`. The action receives
`x2=&carrier` at `0x1be8a9c`. That address borrows the still-live
caller frame. It is not a row pointer, retained provider, completion token,
or safe pointer for later use.

## Rows retain the parent Source plus a kind

The complete row-rebuild body is `0x1be8c50`, extent `0xccc`.
Before its rebuilding work, it checks the supplied descriptor against
controller `+0x78`: Source discriminator, carrier word 0, full byte 4,
and byte 5 (`0x1be8d3c`–`0x1be8d88`). A mismatch returns.
This is a local input comparison, not an existence or uniqueness proof.

On its provider-enumeration branch:

1. Obtain the RootSourceDataSet pair through the previously reviewed
   `0x1c12518`; call its virtual slot `+0x50` with the supplied
   Source address at `0x1be8f00`.
2. Call the returned provider's virtual slot `+8` at `0x1be9040`,
   with a stack destination supplied in `x8`. The result is a vector of
   eight-byte candidate carriers.
3. For each admitted candidate, construct a `MediaInfoProvider` row using
   vptr `0x2dc8288`. Copy the controller's parent Source into row
   `+8` through `0x1c10690` (`0x1be9078`–`0x1be9084`).
   The helper's first operations copy twelve bytes; this is not a copy of
   the candidate's eight bytes into the row.
4. Pass the candidate address to kind mapper `0x1be31a8` at
   `0x1be908c`; store the returned kind at row `+0x14`.
   The iteration advances candidates by 8 and rows by `0x20`.

The native body has additional filtering, sorting, flags and selection work.
Those operations are not replaced or assigned completion semantics here.
The kind mapper and its default/diagnostic behavior are reviewed separately
in [SOURCE-VALIDATION.md](SOURCE-VALIDATION.md).

Thus a later row action resolves through the row's stored parent Source
and kind again. An earlier enumeration is not a permanent snapshot of the
later getter's returned carrier; this work supplies no epoch, freshness or
immutable-provider guarantee.

## Concrete provider lookup and fallback

The already-pinned factory helper `0x1c12518` can construct
`gui::source_select::RootSourceDataSet` through `0x1c211b8`.
Selected constructor anchors install primary vptr `0x2e09098`;
RTTI identifies the same concrete type. Its slot `+0x50`, pointer word
`0x2e090e8`, targets the new complete lookup body **`0x1c192f8`**.

That lookup searches the tree rooted at receiver `+0xf0`, using the
sentinel at `+0xe8`. It compares the supplied Source with node keys
at node `+0x20` through **`0x1c10908`**. On its matching branch it
returns the address **node `+0x30`**.

The comparator's key equivalence is narrower than full MediaID equality:
it orders the Source discriminator, carrier word 0, and **full byte 4**,
but not byte 5 or bytes 6/7. The separate value review pins those semantics.
In particular, full byte values `0x02` and `0x22` stay distinct;
masking both down to five type bits would change this contract.

A miss does not return a null/error result. It returns the real static
fallback object at **`0x3ff0330`**. Its one-time initialization constructs
`Source(0, 0)`, then calls **`0x1c12f40`** on that object
(`0x1c193b8`–`0x1c193d8`). The guard has a normal release and an
exception-abort/unwind path; no execution of either is asserted here.

The constructor installs the `RootSourceData` vptr
**`0x2e00d08`**, clears only the six meaningful bytes of each carrier
at `+0x18` and `+0x20`, and initializes its indexed-tree root `+0x48`
to null. Those stores do not clear either carrier's trailing bytes 6/7. A separate
selected node-construction relation calls the same constructor with
`x0=node+0x30` at `0x1c26e8c`. These anchors bind the expected
concrete provider layout; they do not exclude a different live virtual
implementation or later mutations.

The cleared meaningful bytes describe **initial fallback state**, not a promise that
every future miss must return a zero carrier. This review does not prove
the static fallback remains unmodified. Likewise, returning a nonnull
provider address does not prove that requested media exists.

The row resolver saves the returned provider pointer, then releases its
temporary RootSourceDataSet shared-control pair before invoking provider
getters. The native route's broader ownership assumptions are not replaced
by a custom retain. Neither that temporary pair nor the raw returned pointer
establishes an operation-scoped lifetime lease for a new adapter.

## Actual carrier methods and candidate enumeration

For the concrete RootSourceData vtable:

| DeviceLibrary kind | Native operation | Concrete result |
|---|---|---|
| 0 or 1 | Slot `+0x10` → `0x1c13230` | Load the full eight bytes at provider `+0x18`. |
| 2 | Slot `+0x18` → `0x1c13238` | Load the full eight bytes at provider `+0x20`. |
| 3, 4, 5, 6 | Slot `+0x40` → `0x1c0c720`, index 0, 1, 2, 3 respectively | Search the provider's integer-keyed tree; return the stored eight bytes at matched node `+0x28`, or zero on a missing index. |
| Outside 0–6 | Resolver default | Return zero after the provider lookup; not a native action acceptance/refusal result. |

These are provider fields and integer keys, not assigned port names or
unproved library labels.

Concrete enumeration method **`0x1c0cbd8`** gathers six candidates:
the two scalar getters followed by indexed values 0–3. It recognizes the
expected getter targets for inlined loads/tree searches and also retains
virtual-call branches for different targets. It allocates a vector containing
the six eight-byte candidates, then removes entries whose low byte is zero
or whose byte-4 low five bits are zero.

That filter is structural validity, not storage availability. During vector
compaction the implementation copies meaningful word 0 and bytes 4/5;
it does not make bytes 6/7 into identity. A missing indexed value in this
enumeration's inline path clears the meaningful fields before filtering;
the review does not infer canonical padding from those operations.
The independent indexed getter itself has an explicit `x0=0` miss return.

## Existing named-port observation remains historical evidence

The preserved
[V4 native-path run](../missing-boundary-investigation/runtime-evidence/README.md)
used native SOURCE selection and Local Library confirmation for
USB1 → USB2 → USB1. It associated those manual choices with requests
`1:0102`, `1:0122`, and `1:0102`, including supplied packed values
`0001010200000001` and `0001012200000001`.
[V2's parent observations](../observer-v2/runtime-evidence/README.md)
are a separate source-descriptor boundary. The historical fixture used the
same existing export database on both media; alternate databases were not
validated by those observations.

Those are real named choices from the bounded earlier emulator runs, not
absent evidence. They do not observe this checkpoint's guarded-action kind
and scalar, prove a fresh mounted-media identity, assign a universal USB
numeric enum, or prove that the new provider lookup is the live target.
The parent observer's row kind/discriminator and DeviceLibrary `+0x14`
must not be conflated. No old observation was repeated.

## Extents and remaining gates

Eight new complete FDE-bounded extents are captured:

| Entry | Bytes | Purpose |
|---|---:|---|
| `0x1be5798` | 516 | Row carrier resolver |
| `0x1be8c50` | 3276 | Row rebuild/provider enumeration caller |
| `0x1c192f8` | 284 | Source-keyed provider lookup |
| `0x1c12f40` | 388 | RootSourceData construction |
| `0x1c0cbd8` | 1204 | Candidate-carrier enumeration |
| `0x1c13230` | 8 | Provider field-`0x18` getter |
| `0x1c13238` | 8 | Provider field-`0x20` getter |
| `0x1c0c720` | 88 | Integer-indexed carrier getter |

The resolver's complete code SHA-256 is
`6480a66a14864cdc2aa9fc4cde9c2de9df23d7022db7851c90a20b8fb04db592`;
the provider lookup's complete code hash is retained in the accompanying
aggregate pins. FDE/CIE fingerprints and decoded CFI row counts are metadata.
No new LSDA table or exception-safe wrapper is proved.

Still unresolved are the current UI label-to-DeviceLibrary-kind correlation,
fresh named USB-to-provider identity, media availability and ambiguity handling,
permitted native action scope, indirect target provenance, complete wrapper ABI
and unwind behavior, operation-scoped retention, cancellation and asynchronous
completion. The guarded native action remains required; this research does not
authorize calling the final service directly. Activation and wrapper gates
remain **CLOSED**.

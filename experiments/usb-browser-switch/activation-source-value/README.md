# USB source values and provider provenance

This review narrows the value contract between a native DeviceLibrary row and
the final source service. It adds no activation implementation. A source that
passes the native constructor's structural predicate is not thereby mounted,
recognized, uniquely selected, or accepted by the guarded native action.

Three findings matter for a future adapter:

- Source normalization can change the discriminator while preserving an
  invalid carrier. The kind mapper's ordinary fallback is zero, colliding
  with its supported category-2 result.
- Rows store their parent Source and a separate DeviceLibrary kind. The later
  action resolves a carrier through a provider again; it does not retain the
  earlier enumerated carrier as an immutable snapshot.
- Provider lookup returns a real fallback object on a miss. Nonnull provider
  address is not availability. Its Source-key equivalence ignores carrier
  byte 5, while row matching includes it; both preserve the full byte 4.

These distinctions are grounded in new complete-body evidence:
[SOURCE-VALIDATION.md](SOURCE-VALIDATION.md) and
[CARRIER-PROVENANCE.md](CARRIER-PROVENANCE.md).

## What was checked

One first-time run of the unchanged
[static exact-image verifier](../activation-abi/verify-static.py) passed:

| Checked subject | Count |
|---|---:|
| New complete function/FDE extents | 12 |
| Selected instruction relations | 238 |
| Pointer relations | 13 |
| RTTI names | 4 |
| Imported symbol relations | 5 |
| Newly decoded LSDA tables | 0 |

[Review pins](review-pins.json), [result](result.json), and
[verification receipt](verification.json) preserve the exact subject,
fingerprints, independent source/byte review and private diagnostic hashes.
The assembler rejects overlap with all 83 complete extents in the preceding
ABI, lifetime and shutdown pin sets. Some selected relations connect to those
frozen callers/constructors; the old full-function checks and runtime suites
were not replayed.

The verifier retains its original generic scope label,
"static exact-ELF ABI and concrete timer teardown only". This run uses the
new source-value pins, and supplies neither full wrapper compatibility nor
a new retention, cancellation, or runtime proof. FDE/CIE and LSDA-pointer
metadata are distinguished from actual LSDA decoding.

The exact private ELF remains SHA-256
6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925.
Raw disassembly, capture scripts, runtime material and local paths remain
private. No firmware was executed and no emulator or device was contacted.

## Preserve the historical evidence and remaining gates

The existing V2–V4 runs already associate manual USB1/USB2 choices with
descriptors 0102/0122 and native requests 1:0102/1:0122. Their full byte-4
values differ even though both have category bits 2. The new comparisons
preserve that distinction. These are bounded historical observations using
the same export database, not a universal port enum or a fresh target snapshot.

The source-descriptor discriminator, DeviceLibrary row kind, carrier category
and V4 packed-request category are distinct fields. Neither the old runtime
labels nor a structurally valid value establish the current guarded-action
arguments. Row-match/update return means that some row flag changed; it is
not a found, unique or accepted result.

Next connect an actual permitted native Local Library invocation to its live
row/provider and owner scope, retaining exact argument provenance. The current
label-to-DeviceLibrary-kind relation, fresh media availability/ambiguity,
wrapper ABI/unwind, alternate/reentrant paths and queued lifetime still need
evidence. Calling the final service directly would bypass the guarded action.

The previous ordinary-shutdown ordering result remains intact. Final browser
completion, asynchronous cancellation, missing/ejected media, alternate
databases, scroll/sort, observer ownership and stems activation remain open.
No USB button, native adapter, post-return native read, reference-count mutation,
installation, package/catalog change or release was added. All existing runtime
gates remain **CLOSED**. Do not replay this passing pin set unchanged.

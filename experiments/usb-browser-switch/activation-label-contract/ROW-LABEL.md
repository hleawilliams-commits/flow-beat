# DeviceLibrary row display contract

There is a concrete static relation between DeviceLibrary row kind and a
named **image asset**: kind 0 selects
`img_deviceLibrary_localLibraryUsbMedia.png`, and kind 1 selects
`img_deviceLibrary_localLibrarySdMedia.png`. The actual row component
receives that image separately from its text. For those two kinds, the
reviewed text getter supplies an empty string.

This narrows the display question without proving that a current row
visibly says “Local Library”, that either image contains those words, or
that a particular native activation used kind 0 or 1. The asset names are
evidence of a native resource association, not decoded pixels, a USB-port
numbering API, or a live label-to-action observation.

The input is the same private AArch64 image with SHA-256
`6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
No firmware function, hook, emulator or device was exercised. No prior
test or verifier was replayed. This review adds six complete FDE-bounded
code fingerprints; the preceding 95 full extents remain unchanged.

## The displayed provider is the actual row

The preceding [carrier review](../activation-source-value/CARRIER-PROVENANCE.md)
established that native rebuild constructs `MediaInfoProvider` rows with
vptr `0x2dc8288`, parent Source at `+8`, and separate kind at
`+0x14`. Candidate MediaID type bits determine the kind; the row retains
the parent Source rather than that candidate's full carrier.

New refresh body **`0x1be3d98`** connects those rows to their display:

- It rejects negative indices and checks the native row count. On the
  expected count-method path, the count is derived from controller
  `+0x88/+0x90` using the existing `0x20` stride
  (`0x1be3eb4`–`0x1be3ee0`).
- It forms the exact row address as
  `controller.rows_begin + index * 0x20`
  (`0x1be3ee4`–`0x1be3ef0`).
- A supplied component is dynamically cast to the RTTI-named
  `gui::source_select::DeviceLibraryComponent`. If no component was
  supplied, the branch allocates one and invokes its constructor
  `0x1bfdd70`. The constructor's full behavior is outside this review.
- It passes `x2=row`, `w1=index`, and `x0=component` to
  **`0x1bfd818`** at `0x1be3f28`.

This proves a specific native consumer relation. It does not authorize a
fabricated index, identify a currently mounted provider, or establish a
row/component lifetime lease.

## Image and text follow separate virtual slots

The complete component updater **`0x1bfd818`** retains its supplied
row/provider pointer in `x19`. Its relevant paths are:

| Provider slot | Actual consumer |
|---|---|
| `+0x10` | Called at `0x1bfd890`, with a stack result in `x8`; that result is supplied to `0x25f6d40` for the component child at `+0x100`, then released through `0x2511a90`. |
| `+0x20` | Called at `0x1bfda80`, with a separate stack result; that result goes to text helper `0x1bcd2d0` for child `+0x110`. |
| `+0x40` | Called at `0x1bfdb30`; its string's first byte controls additional display visibility. This is a separate supplementary field, not the kind-to-image map. |

The exact `MediaInfoProvider` vtable binds `+0x10` to
**`0x1be9c08`** and `+0x20` to **`0x1be4e18`**.
Selected helper anchors show that `0x25f6d40` compares the returned
value pointer with child `+0xd8`; when they differ, it passes that field
and the returned value to `0x251e8e0`. That callee's full body is outside
this review. These anchors do not prove rendering, successful asset loading,
or what a user sees.

## Kind-to-image lookup

Image method **`0x1be9c08`** obtains the native cache singleton and passes
the address of row `+0x14` to **`0x1bffc80`**
(`0x1be9c30`–`0x1be9c3c`). It forwards the caller's hidden result
destination. Its slow path allocates the cache and calls constructor
`0x1c029b8`; a selected constructor anchor at `0x1c02a80` invokes
initializer **`0x1c002b8`**.

Lookup `0x1bffc80` hashes the full 32-bit kind using the cache's bucket
count, walks the bucket chain, and compares the stored full key. A match
copies the value from node `+8` through `0x251e8b0`; a miss constructs
an empty/null image value through `0x251e750`. These helper anchors show
refcounted-value copying and null initialization, not a text conversion.

The initializer's relevant branches establish:

| Exact key | Resource group | Resource filename |
|---|---|---|
| 0 | `SourceSelect` | `img_deviceLibrary_localLibraryUsbMedia.png` |
| 1 | `SourceSelect` | `img_deviceLibrary_localLibrarySdMedia.png` |

For key 0, the filename and group are built at
`0x1c002bc`–`0x1c002f0`; the resource/path/image operations feed a
temporary image, which is assigned to the zero-key node at
`0x1c004cc`–`0x1c004d4`. Key 1 follows the analogous branch starting
at `0x1c004e8`, explicitly compares/stores key 1, and assigns its image
at `0x1c006dc`–`0x1c006e4`.

The complete initializer extent is relatively large because it also
initializes other resources, containers and cleanup paths. Its fingerprint
binds that extent; this semantic review is bounded to these two image-map
entries and their direct consumer. It does not certify every resource or
exception path in the initializer. No LSDA is decoded here.

## Separate text behavior

The complete getter **`0x1be4e18`** reads the same kind at row
`+0x14`, with these distinct outcomes:

| Kind | Text path |
|---|---|
| 0 or 1 | Construct from the empty NUL string at `0x28fb368`. |
| 2 | Query the Source-keyed native provider; return a provider string only on the checked status/descriptor branch. A returned empty provider name becomes the literal `No Name`; failed predicates use the empty path. |
| 3 | Literal `Apple Music`. |
| 4 | Literal `Beatport`. |
| 5 | Literal `Spotify`. |
| 6 | Literal `Tidal`. |
| Other | Invoke the diagnostic/default path, then use the empty path if that call returns. |

For kind 2, the provider lookup is made through the already reviewed
factory and slot `+0x50` using row `Source` at `+8`.
The getter releases its temporary factory control pair, then calls the
provider's status slot `+0x28`. Only result 4 proceeds to a descriptor
query through `+0x70`; only the resulting word at the queried
descriptor's offset 4 equal to 2 proceeds to string slot `+0x20`.
These are exact local predicates, not assigned availability semantics or
a new provider-retention guarantee.

Thus the specific text slot does not supply the English words “Local
Library” for kinds 0/1. That is not proof that the native UI lacks the
label: an image, another component, or a resource variant can contribute
visible wording. No image was unpacked or rendered in this checkpoint.
An exact-basename search of existing private work files found no already
extracted copy of either named image, so the investigation stops at the
resource boundary.

## What this closes and what remains open

This establishes an exact static relation from native DeviceLibrary rows
to a component, separate image/text consumption, and kind 0/1 image
resource names carrying local-library USB/SD terminology. It does not
establish a universal English text-to-kind equivalence. In particular,
the filename's `UsbMedia` does not distinguish USB1 from USB2.

The historical
[V4 native path](../missing-boundary-investigation/runtime-evidence/README.md)
still associates manual USB1/USB2 Local Library choices with the earlier
observed Source values. It did not capture the guarded DeviceLibrary
action's kind and eight-byte scalar. Neither this static image relation
nor the historical name association replaces that missing correlation.
The Source discriminator, row kind, resource key, carrier identity and
physical-port label remain distinct values.

| New complete entry | Bytes | Reviewed role |
|---|---:|---|
| `0x1be3d98` | 596 | Row/component refresh relation |
| `0x1be4e18` | 632 | Separate text getter |
| `0x1be9c08` | 264 | Kind-based image getter |
| `0x1bfd818` | 1368 | Component image/text consumer |
| `0x1bffc80` | 120 | Integer-keyed image lookup |
| `0x1c002b8` | 9980 | Cache initializer, bounded key-0/key-1 review |

The component updater's complete code SHA-256 is
`2b1ae0b4a1cbb30e142c18ca4c9d3f9b573d7bdf686726c450dcf4876a6cfbfd`.
The image lookup's is
`92cb6b0116b19dd51b2be45b39b33c0500369b3b34ea168aeef1eac045be0636`.
FDE/CIE fingerprints and CFI row counts are metadata, not wrapper or
unwind permission.

Live receiver provenance, current visible label/action correlation,
fresh media identity, native guarded-action scope, operation-scoped
retention, asynchronous cancellation and final completion remain open.
The complete-wrapper and activation gates remain **CLOSED**.

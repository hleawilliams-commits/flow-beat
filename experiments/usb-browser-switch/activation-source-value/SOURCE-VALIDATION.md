# Source normalization and identity predicates

This is a static review of four previously unpinned complete functions in the exact EP147 ELF with SHA-256 `6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`. It narrows the final source-value contract described in [activation ownership](../activation-ownership/README.md). No native function was called, no observer was installed, and no runtime or earlier test suite was repeated. The new aggregate verifier has not been run as part of this review.

The constructor applies a structural predicate and normalization. It does not establish mounted media, a recognized category, provider availability, accepted activation, or Local Library readiness. Different downstream helpers compare different subsets of the value; there is no single MediaID equality contract established here.

## Constructor layout and normalization

`Source::Source` at `0x1b00160` takes destination `x0`, requested discriminator `w1`, and an eight-byte carrier in `x2`. It first stores the discriminator at destination offset `+0` and the full carrier at `+4`. The resulting value occupies twelve bytes. This body contains no calls and does not retain an input pointer.

Let `D` be the requested discriminator and `M.byteN` the little-endian carrier byte at index N. The constructor tests the structural predicate:

```text
P = (M.byte0 != 0) && ((M.byte4 & 0x1f) != 0)
```

The first condition checks only the low byte, not whether the whole first word is nonzero. The complete body produces these outcomes:

| Requested discriminator | P | Stored discriminator | Stored carrier bytes 0–5 | Stored carrier bytes 6–7 |
| --- | --- | --- | --- | --- |
| 1 | true | 1 | unchanged | unchanged |
| 1 | false | 0 | unchanged | unchanged |
| other value | true | requested value | zero | unchanged |
| other value | false | requested value | unchanged | unchanged |

The requested-1 failure branch writes only the discriminator, then passes through the non-1 checks. Because P is already false, those checks return without clearing the carrier. Conversely, the non-1/P-true branch clears the first word and bytes 4 and 5, leaving the upper two carrier bytes copied. These are opaque upper bytes in this review, not a claim that every native consumer treats them as padding.

The helper does not clamp arbitrary non-1 discriminators. Nor does P restrict the category to the mapper's recognized cases below. A supplied value can pass P while using an unrecognized nonzero category. Constructing a discriminator-1 value is therefore only a structural step in the final native request, not proof that the request is accepted.

## Three separate numeric domains

The following values have different roles:

| Domain | Location or computation | Meaning established here |
| --- | --- | --- |
| Source discriminator | Source `+0`, 32 bits | Constructor branch discriminator; requested 1 is subject to P |
| Carrier category | `M.byte4 & 0x1f` | Input to the row-kind mapper |
| DeviceLibrary row kind | Row `+0x14` | Mapper output used by the row construction path |

A historical “source row kind 1” descriptor must not be substituted for DeviceLibrary row kind 1 or carrier category 1. The row/provider construction relation is separate from the constructor's normalization.

The mapper at `0x1be31a8` reads only the low five bits of carrier byte 4:

| Carrier category | Ordinary row-kind return |
| --- | --- |
| 2 | 0 |
| 3 | 1 |
| 4 | 2 |
| 8 | 3 |
| 6 | 4 |
| 11 | 5 |
| 7 | 6 |

For other categories, it calls diagnostic helper `0x236db60` and returns 0 if that call returns normally. This review does not establish the diagnostic helper's exceptional or nonreturning behavior. The ordinary fallback collides with the supported category-2 result, so output 0 cannot certify recognized media or independently identify Local Library.

## Identity is specific to each helper

The selected row-matching predicate inside `0x1be59a0` calls the row resolver at `0x1be5798`, then compares its returned carrier against the supplied MediaID using the full first 32-bit word, full byte 4, and full byte 5. Its matching predicate ignores carrier bytes 6 and 7. The function scans all rows, can match multiple entries, invokes an existing row's virtual `+8` path, and updates row state. Its return copies the `w23` accumulator into `w0` and reports whether any row's `+0x19` flag changed; it does not report found, unique, or accepted. It is not a general equality API, and these comparisons do not describe all its effects.

The Source ordering comparator at `0x1c10908` is different. It orders:

1. The 32-bit Source discriminator using a signed comparison.
2. The first 32-bit carrier word using an unsigned comparison.
3. The full carrier byte 4 using an unsigned comparison.

It ignores carrier byte 5 as well as bytes 6 and 7. Thus two Sources with equal discriminator, first word, and byte 4 but different byte 5 are equivalent under this ordering, while the selected native row-matching predicate distinguishes those carrier values. The [provider lookup](CARRIER-PROVENANCE.md) uses this comparator, establishing that tree's particular map-key equivalence; it does not establish universal MediaID equality.

Both comparisons retain all bits of byte 4. The category mapper masks its upper three bits, so matching only `byte4 & 0x1f` loses identity information that these native comparisons retain.

## Relationship to historical USB observations

The [observer-v2 evidence](../observer-v2/runtime-evidence/README.md) and [V4 investigation](../missing-boundary-investigation/runtime-evidence/README.md) already record bounded native-path observations named USB1 and USB2, with source descriptors `0102` and `0122`, respectively. V4 recorded requests `1:0102` and `1:0122` and packed request words `0001010200000001` and `0001012200000001` in that run.

Those historical descriptors have the same category bits, 2, while their full carrier byte 4 differs (`0x02` versus `0x22`). This is consistent with preserving the full byte for identity while masking it for classification. Both the row-matching predicate and the Source comparator distinguish this particular pair; ignoring byte 5 does not alias those historical port descriptors. The old labels remain evidence for that observed run and export database, not a universal USB-port enum, a current connection state, or freshness of a newly supplied value.

The V4 packed request is a separate request container. Its unchecked category byte must not be silently equated with the eight-byte carrier accepted by this constructor. The static review adds no new named-port observation, completion result, or readiness result.

## Evidence boundary

| Complete function | Start | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Source constructor | `0x1b00160` | 84 | `be21fdaed90def942782c8c5a7328e720a678add57bc0eded21c6356efde4a11` |
| Carrier category mapper | `0x1be31a8` | 136 | `5634cfd22b5859faa1c5afe3d89ed5fa80801f2d2a40d0f97110bc1160d3f54b` |
| Native row-match/update body | `0x1be59a0` | 220 | `00c0d4da185b56a37a6a5bce3395b25e090d9a1351c88d216f6a2738e8ad24ed` |
| Source ordering comparator | `0x1c10908` | 68 | `157210b52f86b21edebf4a1f62f4f88570ab8e326a8d19d1c04c2de07c18c97d` |

The private capture retains four complete bodies, 72 instruction anchors, and their FDE/CIE extent metadata. No LSDA decoding or transitive exception analysis is claimed. The extraction completed without a preparation failure. Raw firmware and disassembly remain private.

This narrows how an already-supplied final value is copied, normalized, classified, and compared. It does not supply a fresh owned receiver, an authoritative media/provider selection, view/timer retention, final completion or asynchronous cancellation, or a safe native activation boundary. Existing runtime gates remain closed.

# Phase-1 host validation

The final fixture passes **146 assertions per plain, ASan/UBSan and TSan mode**. Seven separately compiled strict source mutations each fail exactly one named assertion in every mode. The runner rejects unexpected counts, exit codes, failure labels and sanitizer diagnostics, including recovering UBSan diagnostics. Seventeen source integration checks bind handler registration, absence of a compat handler, default-off/read-only opt-in, the CRC parameter callback, its single preserved array-type metadata declaration and ABI assertions. These source checks do not execute Linux registration.

Result: [host-result.json](host-result.json), SHA256 `7fb98ccc51d3e4f4f102e0dd3bab7eb9e0c4ff2d0f200f759eb3bfac110181a5`.

- Fixture [test-guard.c](test-guard.c): `f616e5670a5c368e3289ac3298a33152295d46e76bea856070a88a04c136e2f5`.
- Runner [run-tests.py](run-tests.py): `5f74e5f9fefe1d8a8c0b195700834980ef37d2290f78942ccdba8575012f7ae6`.
- Preparation helper: `1a3ef13fdeb9c99a742428b927d13c476529f8fc85030661825c27d64daa12b5`.

Run from the repository using a local pinned emulator checkout:

```sh
python3 experiments/emulator-overlay-ownership/run-tests.py \
  --emu ../xdj-az-emu --result /tmp/overlay-ownership-result.json
```

An independent run repeated the final full matrix and obtained byte-identical result JSON (`d84abda80147eda0ff8afdd68de9ded7b6d9f7f7069865f700c501a467078f8e`). The independent host compiler was Apple clang 16.0.0 (`clang-1600.0.26.6`), targeting `arm64-apple-darwin25.2.0`, with `CC` unset. This remains a host test, not a Linux kernel build.

Each invocation applies the verified patch to a fresh private source copy through `prepare.py`. It retains source/patch/fixture/runner snapshots, all 13 extracted function bodies, state declarations, full frame and UAPI headers, host shim headers, compiler output and execution logs. The public result fingerprints those inputs and extraction products. No original emulator source is edited, and no kernel module, device or transport is used.

## Metadata-only correction

The first callback patch omitted the baseline `spi_crc_len` array-type metadata. The corrected patch adds exactly `__MODULE_PARM_TYPE(spi_crc_len, "array of int");` after the callback registration. The runner now requires exactly one such declaration and the full final matrix was rerun against the corrected, pinned input. The earlier `d84abda80147eda0ff8afdd68de9ded7b6d9f7f7069865f700c501a467078f8e` result, source snapshots and note remain retained as a checkpoint, rather than being silently relabeled.

[Exact saved-input comparisons](host-metadata-comparison.json) establish that the complete C source differs only by this one line. All13 extracted function bodies and the tested declarations are byte-identical; so are every strict-mutant extraction and host output. The fixture itself is unchanged. The current result and source fingerprints above identify the final metadata-preserving candidate; this comparison adds no kernel execution or runtime proof. Parent then independently repeated the final full matrix using the same Apple clang16.0.0 host compiler and obtained byte-identical result JSON (`7fb98ccc51d3e4f4f102e0dd3bab7eb9e0c4ff2d0f200f759eb3bfac110181a5`).

## What executes

The host compiler builds exact candidate bodies for CRC calculation/set/get, usercopy wrappers, SPI open/release/read, control open/release/write/ioctl, and the owned-next-sequence predicate. Candidate state declarations and ABI assertions are also extracted. The frame header stays in private extraction snapshots; public tests use synthetic patterns.

`miso_lock` maps to a pthread mutex. Usercopy operates on owned buffers, can make a partial copy then fail, and can pause a chosen caller after copying. Lock wrappers can pause before acquisition or after unlock. Allocation, capability, file-mode, integer parsing, formatted parameter output and scheduling are explicit host shims. In particular, the `strtol` conversion shim does not prove Linux `kstrtoint` lexical behavior. STATE is checked as a raw overlay/idle snapshot; it is not confused with the CRC-restamped SPI read.

The fixture checks legacy behavior and independent buses, malformed/unauthorized/faulted operations, full-state preservation on refusal, explicit release and close retaining the fence, same-file versus separate-file identity, stale epochs and exact sequences, final valid 64-bit counters and exhaustion, short CRC lists, invalid parse/allocation failures, extreme signed CRC prefixes and output faults. Authorization loss and bad headers are tested separately for FRAME and RELEASE as well as CLAIM.

Forced schedules pause old writes/clears or CRC requests before the final mutation lock, acquire ownership, then resume them. Other schedules pause an old tagged write across release/reacquisition, race two claims or equal-sequence writes, and change state after a reader/getter has copied its locked snapshot. The publication stress uses 1,000 writes. At its midpoint the writer waits until the reader observes the midpoint and completes another actual `spi_read`; the final assertion requires this overlap. A producer join is not used to authorize those reads.

## Discriminating controls

| Source mutation | Required named failure |
|---|---|
| Remove only the final legacy mixer payload fence | `queued_legacy_write_refused_at_commit` |
| Remove the clear-all fence | `clear_all_atomically_refused` |
| Remove matching-file ownership check | `file_identity_required` |
| Remove exact next-sequence check | `duplicate_sequence_refused` |
| Clear the fence on owner close | `close_keeps_permanent_fence` |
| Write each parsed CRC value into live state before parsing succeeds | `parse_failure_no_partial_commit` |
| Read CRC length after unlocking the copied frame | `read_frame_and_crc_same_snapshot` |

Each mutant runs only its corresponding bounded oracle: three assertions for the two paused-thread oracles, one for the others. The candidate runs the complete 146-assertion fixture. A compile failure, crash or sanitizer report cannot satisfy a strict control.

## Limits and retained development evidence

These checks establish behavior of the extracted methods under the stated host shims. They do not prove Linux spinlock/IRQ behavior, real usercopy, compat ioctl support, file-reference lifetime, kernel capability enforcement, actual callback registration or module loading. A same `struct file` pointer models aliases; no `dup`/`fork`/last-reference VFS operations run. Final close is exercised after calls finish; explicit RELEASE supplies the concurrent ownership transition. The custom parser's commit logic executes, but its kernel conversion/allocation services are mocked.

The initial host compile failed before any test because of a host `static_assert` macro conflict and a short-circuit thread-creation expression that could leave a thread handle uninitialized. Both were fixture issues; the failed snapshot is retained. Subsequent plain 127- and 132-assertion runs passed but are superseded by the expanded 146-assertion matrix. They remain in private immutable run directories. Independent source review also required witnessed reader/writer overlap and exact assertion counts; both are present in the final fixture. No candidate kernel source was changed to make the tests pass.

Phase 1 still lacks a producer handback API. Passing these checks does not establish queued-generation recovery, native input receipt, safe overlay restoration, a built/loaded kernel module, installation readiness or physical timing. No runtime contact occurred in this validation.

# Private stems staging with ordinary producer ordering

This is a **host-tested, unpublished core composition**, with a complete ARM64 compilation. It adapts the separate ordinary producer-ordering patch to the frozen private-staging draft. Recovered buffers remain private; no activation API or usable recovery workflow is added. There is no community package, installer, or catalog entry.

**Do not install this composition. The staging runtime gate remains closed.** Passing these tests does not explain the historical ordinary-restart assertion failure, resolve the recovery activation contract, or validate the physical deck. Neither this composition nor its binary has been installed in the emulator or on hardware.

## Exact patch stack

Start from upstream `3e0c5cb010454228f4ee63226b60d060494eeaae` and apply:

| Order | Patch | SHA256 |
| --- | --- | --- |
| 1 | [Memory retry](../stems-memory-retry/memory-retry.patch) | `5905b0f514cf33e2dafbbdecccf18125e5352108515eded6b424f4dbc6138f1b` |
| 2 | [Activity follow-up](../stems-retry-activity/activity-followup.patch) | `69ed45bf1d8b645a574379be0c8b1389eaaec2b919e8de1529d6e75f6751c30c` |
| 3 | [Frozen private staging](../stems-recovery-staging/staging-only.patch) | `c8d8122ec0af65ebd478ebea1fedbb5b030c77ac186d2d9a5c7818ae01d6a03b` |
| 4 | [This adapted incremental patch](composed-publication.patch) | `5eda65de513a365ce6ccbab0718eb40b2bdc4cd17991aac7b1bc9bff8656ea81` |

The standalone [ordinary publication patch](../stems-ordinary-publication/ordinary-publication.patch), SHA256 `b4ff1224d1f0faaa4e248d564d9a053877b644e277eb2d5d0ebe011623501a34`, is the reference for step 4. **Do not additionally apply it to this stack.** All four earlier patch files, including that standalone reference, remain unchanged. The runner verifies their hashes before assembling a temporary tree. It only reads the supplied upstream checkout through pinned Git objects.

The final [extraction manifest](manifest-006.json) records the complete patched source hashes, actual extracted functions, fixture inputs, and original restart-test hash. Its exact production extraction SHA256 is `773607411f6b4763ee1f2edf3c3434201b797c5321d4458df9632a2dd05b20ac`. The [independent manifest](root-manifest-010.json) is identical.

## What is combined

The patch touches `stem_playback.c`, `stem_envelopes.c`, and `ofsload.c`:

- Decoder progress publishes immutable initialized PCM prefixes with release stores. Ordinary progressive/envelope/loader-offset consumers acquire the presence mask and decoded counters, following the [standalone producer contract](../stems-ordinary-publication/README.md).
- Failed-slot removal uses release ordering. In the pinned decoder, every freeing failure exit precedes the first positive decoded count; a positive prefix is not subsequently freed through that decoder's failure path. A scan observing an old presence bit must stop at its zero count, before reading PCM. A scan observing the removed bit acquires its preceding pointer-clear/free writes.
- Ordinary envelope progress reads its mapping length under the existing header-seed mutex and retains that mapping through the pass. Samples remain bounded by the acquired common prefix.
- Ordinary loads reconcile their final acquired minimum after decoder joins. The helper performs no extra progressive-envelope work. Recovery finalization does not invoke this public helper.

The 400 MiB reserve, memory admission, update/signature protections, ordinary offset-before-join order, and recovery offset-after-join order are retained. This does not create additional memory capacity.

## Private publication and lifetime contract

The two new publication defenses are deliberately separate:

1. `load_stems_ogg` captures the job's fixed recovery routing and calls `publish_stem_prefix` at finalization only when `!recovery`. Private completion continues to retain its minimum in `g_recovery_dk` after all writers have joined.
2. `publish_stem_prefix` acquires `g_adv_lock` before checking `!recovery_hidden(deck)`, the offset hold, and stale-load state. A direct caller cannot bypass private routing merely by skipping `advance_stem_frames`, including if a private job starts before the helper acquires its lock.

The inherited early private return in `advance_stem_frames` remains. Private envelope progress, cache loading, header seeding and final envelope builds remain suppressed. The patch changes neither public envelope export functions nor the audio callback to mask accidental publication. Public frames, pending availability, PCM pointers, public offset, envelope bytes/generation/mapping/directory, and existing waveform exports are checked unchanged while recovery is prepared. A pre-existing cached waveform may remain exported even while public stem frames are zero; it is preserved rather than forcibly hidden.

Actual call ownership matters. Private mode is set before child creation and stays fixed through every decoder join. Drop while loading invalidates the job rather than clearing its routing or freeing active writers. Completion/drop and new-job decisions share the advancement lock. An ordinary launch must follow private drop/join; it cannot inherit private routing. Private completion never writes the flusher's loaded-track identity.

The helper's in-lock test is **a routing veto, not job-token authentication**. Its accepted callers still must supply the current ordinary job's acquired or joined prefix. An arbitrary future caller retaining private counts across a completed drop could violate that contract after mode becomes ordinary. The actual private call graph avoids creating such a call at all; this patch does not introduce a general tokenized publication API. Direct bypass tests below are deliberate defensive probes, not evidence that their artificial callers exist in production.

## Host tests and controls

From the repository root:

```sh
python3 experiments/stems-staging-publication-composition/run-tests.py --upstream /path/to/xdj-az-mods --manifest /tmp/composition-manifest.json
python3 experiments/stems-staging-publication-composition/run-tests.py --upstream /path/to/xdj-az-mods --plain-only --suite staging --control helper-guard-removed
python3 experiments/stems-staging-publication-composition/run-tests.py --upstream /path/to/xdj-az-mods --plain-only --suite staging --control helper-check-before-lock
python3 experiments/stems-staging-publication-composition/run-tests.py --upstream /path/to/xdj-az-mods --plain-only --suite staging --control callsite-guard-removed
```

Both fixtures compile the actual composed load, decoder, envelope, lifecycle, flusher, acquired-prefix scan and bounded offset-sample functions. The private fixture also executes the actual `pbread_hook`. Only the completion tail of `ofs_load_measure` is included; its earlier codec/correlation work is represented by fixed fixture locals. The test codec supplies synthetic samples, and `mlock` is a no-op. These are actual-source contract tests, not native media-decoder or offset-calibration tests.

Ordinary decoder threads run concurrently in **plain, ASan/UBSan and TSan**. There is no serial-ordinary switch or sanitizer suppression in this runner. The ordinary fixture uses `3 * PROG_CHUNK + 137` frames, crosses several production progressive callbacks, and checks opening/allocation/resampling failures, a short prefix, bounded envelope/offset readers while another tail is unfinished, and explicit missed-publication/hold/stale finalization faults. That missed-publication test deliberately clears public frames after real joins; it does not force stale atomic reads or reproduce the historical failure. Uniform header lengths do not explicitly force a late envelope-length extension; the locking change also relies on source inspection.

The private fixture retains the frozen 4,096-frame lifecycle cases and uses actual loader/decoder pthreads on all four decks. The runner requires the entire original `completion_boundary` test function to remain byte-for-byte identical, including **“consumed restart still launches ordinary load.”** It exercises replacement, drop, eject/empty/new-track behavior, mode-release order, worker/decision contention at the actual lock attempt, private offset after all joins, independent token/generation vetoes, and native PCM/Position preservation in the fixture's `pbread_hook` path.

Added defensive cases call the actual public-prefix helper during private decoding and after preparation, with hold/stale independently removed. Another caller is paused at its actual lock attempt while mode is ordinary; an actual private job starts, then the caller continues. The hold is deliberately opened in that fixture so the in-lock private test is the only rejection. These explicit fixture faults are retained in the source and are not production state transitions.

The staging extraction wraps only the publication helper entry with a relaxed atomic private-call counter, then executes its unchanged extracted body. This detects a missing finalization-call guard even when the helper's independent guard prevents visible damage. It does not add a mutex or acquire/release ordering to the decoder's store/scan interval. Existing condition-variable schedules deliberately order fixture state and selected production accesses; passing TSan is not a proof for every possible interleaving.

| Evidence | Result |
| --- | --- |
| [Final author run](run-006-final-sanitizers.txt) | Ordinary 336/0 and staging 1020/0, each in plain, ASan/UBSan and TSan. |
| [Independent parent-task run](root-run-010-sanitizers.txt) | The same six results; its extraction manifest matches exactly. |
| [Helper guard removed](run-007-helper-guard-removed.txt) | Exactly 12 intended private-prefix rejection failures; the candidate guard is absent only in this temporary negative control. |
| [Helper check moved before lock](run-008-helper-check-before-lock.txt) | Exactly four intended failures, one per deck, at the mode-transition schedule. |
| [Final callsite guard removed](run-009-callsite-guard-removed.txt) | Exactly 12 intended private-helper-call failures. The helper still prevents public frames, demonstrating the two checks independently. |

The control runner requires exit status 1, exactly the expected failure labels/counts, and matching summary totals; unrelated failures do not satisfy a control. The original historical ASan failure remains [separately recorded and unexplained](../stems-recovery-staging/observed-asan-failure.txt). No attribution to this composition, release/acquire ordering, or final reconciliation follows from these passes.

Earlier scoped results remain unchanged: run 001 is the initial 884-check plain fixture before independently opening the hold in the lock-transition case; run 002 has that strengthened case in all sanitizer modes; runs 003–005 are its 12/4/8-failure controls. The final fixture adds existing exported-waveform preservation and stricter control acceptance. The production patch did not change. Initial temporary-tree assembly omitted files needed by a preceding patch and stopped at `git apply` before creating a candidate or running tests; assembling the complete patch inputs corrected that setup omission.

## Complete ARM64 build

The exact stack was assembled separately from both repositories and compiled using the existing immutable ARM64 build image with network access disabled and image pulling disabled. All seven production-source hashes matched the host manifest before and after compilation. [Build metadata](build-info.json) and the [sanitized compiler log](build-log.txt) record the image, source hashes and command.

The full `azstem.so` build succeeds with the pinned Makefile's `-Wall -Wextra -Werror` core flags. The output is ELF64 little-endian AArch64:

- Stripped binary SHA256: `6350613fbd3ecea62fc1f415a71e875a2e87a583d4a9fe4d523a1d53b1476045`.
- Unstripped binary SHA256: `f264d72733b7dd73bfe88e13e65ad4b2cdb604413dcf26d77e10be4dd500b106`.

Binaries remain outside this repository. No installation, reload, deployment, package publication, or physical-deck action was performed.

## Remaining boundaries

- This is preparation only. Recovered stems cannot be activated, and retained buffers consume RAM until drop/replacement. It does not make Henry's missing pads usable.
- Public PCM pointers and `stem_frames` remain plain fields. Audio-reader publication and reader quiescence/reclamation are not established by this worker contract. The existing `free_stems` fences do not prove a previously captured pointer is no longer in use.
- The flusher verifier still passes plain `stem_frames` to `ofs_correlate` directly, bypassing the acquired-counter loader scan. Full offset-reader synchronization and lifetime remain outside scope.
- The ordinary worker's identity-check/write gap, job-allocation failure behavior, and in-place UI envelope-reader lifetime remain inherited limitations.
- The full post-render hook, real audio caches/prefetch, native decoder, storage throughput, audio quality/deadlines, and physical four-deck behavior are not exercised. The raw diagnostic fread mode can already return zeros without public stems; no native-PCM claim is made for that mode.
- Neither inferred stillness nor a metadata update establishes accepted native LOAD, transport pause, activation authorization, or audio ownership. The [activation contract](../../docs/STEMS-RECOVERY-ACTIVATION.md) remains unresolved.

Keep the separate official core and existing rollback artifacts intact. This experiment supplies a reviewable composition and stronger host evidence; it does not reopen the staging runtime gate.

License: GPL-3.0-only, matching upstream.

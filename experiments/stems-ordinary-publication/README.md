# Ordinary stem decoder publication: separate core candidate

This experiment gives the ordinary decoder's worker consumers an explicit initialized-prefix contract. It addresses a reproduced ThreadSanitizer finding between a decoder's PCM writes and progressive-envelope reads. It also reconciles the final decoded minimum after decoder joins, under the existing hold/cancellation gate.

It is **not an explanation or fix for the unexplained staging ASan assertion**, a memory-recovery activation design, a community package, or a physical-device update. The frozen staging draft remains unchanged and its runtime gate remains closed. This directory contains no installer or distributable binary.

## Source and patch order

The standalone base is upstream commit `3e0c5cb010454228f4ee63226b60d060494eeaae`. Apply only `ordinary-publication.patch` to that commit. Its SHA256 is:

```text
b4ff1224d1f0faaa4e248d564d9a053877b644e277eb2d5d0ebe011623501a34
```

The candidate changes `src/azstem/stem_playback.c`, `src/azstem/stem_envelopes.c`, and `src/azstem/ofsload.c`. It does not depend on the memory-retry, activity, or private-staging experiments. The [extraction manifest](extraction-manifest.json) records the pinned source hashes and extracted production functions. The supplied upstream checkout is read through Git objects; tests apply the patch in a temporary directory.

[Composition checks](composition-check.json) show that the patch also applies textually after memory-retry plus activity, but this is **not a runtime validation of that stack**. Those experiments can initiate later recovery and have separate activation concerns. The ordinary patch does not apply after frozen staging `c8d8122e…`; no combined candidate was made or tested.

## The worker contract

The following reasoning is limited to one ordinary load job, with its per-slot counters reset before decoder creation and no replacement job reusing the same deck state before the old decoder children join.

1. Each decoder owns one PCM slot. A positive `g_stem_dec[deck][slot]` publishes the slot pointer, allocation metadata, and immutable PCM prefix `[0, count)`. Future decoding writes only beyond that prefix. Both the progressive and whole-file resampling branches now store the positive count with release ordering.
2. The minimum scans in `advance_stem_frames`, `env_progress`, and `ofs_stems_decoded` acquire each counted slot's progress before consuming its pointer or samples. An acquired positive count therefore orders that slot's prior initialization and sample writes before the consumer. A scan may conservatively observe an older prefix; it must not treat an unacquired count as permission to read PCM.
3. Failed-slot removal is also publication. The failure path removes its presence bit with release ordering, and the three worker scans acquire the presence mask. This matters because envelope code loads each pointer before checking its presence bit, and `ofs_stem_mono_lim` reads every non-null pointer.
4. Envelope mapping metadata is protected by `g_envp_lock`. `env_progress` now reads `g_env_total` and computes its bucket mapping while holding the same mutex used by decoder header seeds. It holds that mapping throughout the pass. It reads samples only with indices below the acquired common prefix; the unfinished tail stays at the existing placeholder values.
5. After all actual decoder joins, `load_stems_ogg` reconciles the final acquired minimum through `publish_stem_prefix`. This helper uses `g_adv_lock` and rejects both a held gate and a stale load. It performs no progressive-envelope pass; the existing final envelope build follows on success. It adds no work to an audio callback.

Release/acquire ordering establishes visibility when a consumer observes a published count. It does not guarantee that every concurrent progressive scan observes the newest count. The post-join reconciliation removes dependence on the final progressive callbacks having observed the final common minimum.

### Why failed slots do not free a published prefix

In the pinned `decode_stem_progressive`, every `goto fail` is before the first positive decoded-count store: opening/validating the file, failing whole-file resampling, allocating the PCM buffer, or allocating the temporary chunk buffer. The temporary-buffer failure explicitly clears its structural PCM pointer before freeing it and removing the presence bit. The successful resampling branch stores its positive count and returns.

After progressive decoding has published a positive count, a later zero/short codec read exits the loop; it does not enter `fail` or free the PCM buffer. The shorter prefix remains allocated until the loader finishes. Thus, within this one-job contract:

- A scan seeing the old presence bit for a failed slot sees that slot's zero count and returns without reading slot pointers or PCM.
- A scan seeing the removal acquires the preceding pointer-clear/free operations. Multiple failed-slot removals are atomic read-modify-write operations on the same mask, so a later acquired mask also carries the prior removals' release sequence.
- A scan observing a positive acquired count reads a slot that will not be freed by a subsequent decoder failure within that job.

This is distinct from making a transient structural pointer safe to dereference. A pointer assigned before its first positive count is not a readable PCM prefix.

## Important boundaries

This is a worker-producer contract, not complete audio-buffer publication or reclamation:

- `deck_stems.stem_frames` and the public PCM pointers remain plain fields. `pbread_hook`, the post-render hook, and other audio consumers do not gain a complete C11 acquire/lifetime protocol from this change.
- The flusher verifier in `key_shift.c` still calls `ofs_correlate(dk, dk->stem_frames, ...)` directly. It does not use the acquired-counter loader scan. The existing epoch postcheck does not establish PCM lifetime or synchronization by itself. This patch does not fix every offset-reader path.
- `free_stems` closes the frame gate and nulls pointers before freeing, but its fences do not prove that an already-running reader has released a previously captured pointer. No reader quiescence or reclamation mechanism is added here.
- The ordinary worker's stale-check/loaded-identity write remains separate from the flusher's identity transition. The existing in-place envelope exports can also be read by UI code without `g_envp_lock`; this experiment does not prove the UI's entire envelope lifetime.
- The memory reserve, admission calculation, file discovery, initial availability behavior, update protections, and ordinary offset-before-join order are unchanged. Successful ordinary loading can still introduce stems according to existing playback semantics.
- No real Vorbis decoding, full offset calibration, audio deadline, USB throughput, emulator runtime, or physical-device behavior is established by the host fixture.

The original staging assertion lacked its failing deck, operand, and state. Neither this independent race finding, this candidate's passing tests, nor final-prefix defense testing attributes that historical failure to a particular cause.

## Checks and discriminating controls

Run from the repository root:

```sh
python3 experiments/stems-ordinary-publication/run-tests.py --upstream /path/to/xdj-az-mods --manifest experiments/stems-ordinary-publication/extraction-manifest.json
python3 experiments/stems-ordinary-publication/run-tests.py --upstream /path/to/xdj-az-mods --baseline
```

The runner compiles extracted production load, decoder, envelope, lifecycle, minimum-scan, and bounded offset-sample functions. It uses actual loader/decoder pthreads and synthetic codec output, temporary stem filenames, fake available memory, and no-op `mlock`. The fixture's resampling helper supplies completed synthetic buffers; it tests publication of that branch, not the actual resampler. Only the actual completion tail of `ofs_load_measure` is extracted; real source decoding and correlation are omitted.

The synthetic track has `3 * PROG_CHUNK + 137` frames, so it crosses several actual progressive callbacks. On all four decks the fixture checks ordinary completion, opening failure, temporary-allocation failure after a structural pointer has been assigned, whole-file resampling failure/success, short-read prefix bounds, and an envelope/offset reader while a slow decoder is paused before its second chunk and other decoders can continue.

The prefix-bound check waits through the production acquired-minimum scan until every slot has published its first chunk. It checks the envelope stops there and preserves its unfinished tail; the actual `ofs_stem_mono_lim` accepts the last initialized sample and rejects the next sample. Header lengths are uniform in this fixture, so it does not explicitly force a late extension of `g_env_total`; the mutex change is additionally supported by source inspection.

Finalization defense uses an **explicit test fault**: after the real final decoder join, the fixture sets public frames to zero. The unpatched loader stays at zero; the candidate reconciles its completed prefix. This is not a forced-stale atomic read or a reproduction of the unexplained staging failure. Held and stale load variants remain unavailable. Separate calls to the actual publication helper test hold-only and stale-only guards immediately, before loader cleanup could conceal a prohibited transient publication.

There is no serialized-ordinary mode or sanitizer suppression in this runner. The baseline control is accepted only when all functional assertions pass and TSan reports the expected decoder/progressive-reader race; any unrelated race report alone is insufficient.

| Artifact | Result / scope |
| --- | --- |
| [Current candidate checks](test-results.txt) | 336/0 plain, 336/0 ASan/UBSan, 336/0 TSan in the recorded run. |
| [Independent parent-task run](root-test-results.txt) | 336/0 in all three modes; its [extraction manifest](root-extraction-manifest.json) matches the author's manifest exactly. |
| [Initial completed candidate checks](prior-test-results.txt) | 308/0 in all three modes, before adding direct stale-only/hold-only helper checks. |
| [Initial baseline control](prior-baseline-results.txt) | 308/0 in each mode; TSan reported decoder-to-envelope PCM/structural-pointer races and a bounded offset-reader race. |
| [Current baseline control](baseline-results.txt) | Pinned upstream, without the candidate; includes stricter expected-race validation. |
| [ARM64 build](build-info.json) | Standalone ARM64 core compilation recorded by the parent task; temporarily exercised in the emulator, with [separate runtime evidence](runtime-evidence/README.md). |

The [initial prefix-barrier failure](initial-prefix-barrier-failure.txt) is retained, with the two local repository-path occurrences replaced by `<REPO>`; assertion and traceback details remain intact. Its fixture incorrectly assumed the slow slot reaching its second-chunk pause meant every other slot had already published a first chunk. The fixture now waits for that condition through the real minimum scan. The production patch did not change. That fixture error is separate from the unexplained older staging assertion.

## Bounded ordinary-load emulator check

The exact standalone ARM64 binary was temporarily loaded in the localhost emulator. One native all-four load showed progressive public frames on each deck, followed by 8,599,552 final frames, all four stem slots present and finalized envelopes on every deck. Actual source identities matched the intended private fixture. All four playheads advanced during brief playback and stopped across paused snapshots, with the same process throughout. [Runtime evidence](runtime-evidence/README.md) records the sampling, corrected test expectations and complete restoration.

These are non-atomic observational snapshots from one ordinary load. They do not measure audio quality, prove a production race impossible, reproduce memory refusal, exercise private staging, or validate physical timing. The host race control and scoped publication proof remain separate evidence.

## Required future staging integration

No staging integration patch is included. A combined version would require explicit review and actual-source integration tests. In particular, the new direct final-prefix helper must be guarded against private recovery, both at the finalization call site and in the publication helper. Frozen staging's early return in `advance_stem_frames` alone does not guard a different direct publication call. Otherwise private decoded counters could incorrectly publish public frames while public pointers remain absent.

An eventual combined patch must have its own exact stack, hashes, private-state invariance tests, ordinary-path regression checks, and unchanged recovery activation gate. Passing this standalone contract fixture does not reopen the frozen staging runtime gate.

License: GPL-3.0-only, matching upstream.

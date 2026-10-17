# Stems recovery: private staging draft

This separate core experiment decodes a memory-retry job into private buffers and **never activates those buffers**. It is a prerequisite for a future activation design, not a usable stems fix, community package, or physical-device update. No activation API, binary, installer, or catalog entry is included.

**Validation remains open. Do not install this draft.** One ASan/UBSan run failed the ordinary-restart assertion. The current run and 30 later diagnostic repeats passed, but the earlier failure is unexplained. The emulator installation gate remains closed; no staging runtime installation or physical validation was performed. See [the retained observed failure excerpt](observed-asan-failure.txt).

## Exact source and patch order

Apply to upstream commit `3e0c5cb010454228f4ee63226b60d060494eeaae`, in this order:

1. `../stems-memory-retry/memory-retry.patch`
2. `../stems-retry-activity/activity-followup.patch`
3. `staging-only.patch`

The frozen staging patch SHA256 is `c8d8122ec0af65ebd478ebea1fedbb5b030c77ac186d2d9a5c7818ae01d6a03b`. The extracted candidate production source SHA256 is `c14d7c3504a92a08135219dcb8e5df81bf506e715307c924aefcda044888bef9`. [The manifest](extraction-manifest.json) records source and dependency hashes and the complete extracted-function list.

The patch touches only `stem_playback.c`, `ofsload.c`, and `stem_envelopes.c`. It preserves the 400 MiB reserve and admission calculation. It does not change official update, signature, downgrade, or package protections. The supplied upstream checkout is read through pinned Git objects and remains untouched.

## Draft behavior

A retry may start only after the prior load has ended with public frames, pending availability, and all public PCM pointers absent. Each recovery job captures its track, source, generation, and a nonreused token. Recovery routing remains fixed until every decoder has joined.

Recovery decoders write a separate `deck_stems` object. They do not replace the public PCM pointers, publish progressive frames or availability, seed or build public envelopes, read a cached envelope into public state, or update the public offset. Existing public envelope generation and export behavior remain unchanged; exports are not masked to manufacture an empty result.

For recovery only, offset measurement runs after decoder joins. Its private sample readers therefore have no concurrent decoder writers. Ordinary loads retain their existing offset-before-join order. The retained frame count is the minimum decoded prefix after joins; it is **not proof that every file decoded completely**. A short prefix may also be too short for offset measurement, which can wait for its existing timeout and retain an unmeasured zero offset.

A successful recovery retains its buffers privately and blocks another recovery attempt. There is no way to activate them in this draft. Retained buffers consume RAM until track replacement, eject, or drop. Staging does not make four arbitrary long tracks fit into memory.

Track replacement or eject invalidates the token. An in-flight decoder retains ownership until joins; only then does the worker discard the private buffers. Recovery completion never overwrites the flusher's loaded-track identity. Launch, completion, and drop decisions use the existing advancement mutex, which is released before thread creation or inline decode. An accepted replacement job consumes the obsolete restart request, including one retained through empty eject ticks.

The ordinary loader and its progressive publication remain largely unchanged. This experiment does not resolve their existing lifecycle or concurrency issues. In particular, job-allocation failure can still clear retry eligibility before allocation fails, and the ordinary worker's stale-check/loaded-identity write is not made atomic by this patch.

## Host checks and scope

From the repository root, using a checkout containing the pinned commit:

```sh
python3 experiments/stems-recovery-staging/run-tests.py --upstream /path/to/xdj-az-mods --manifest experiments/stems-recovery-staging/extraction-manifest.json
python3 experiments/stems-recovery-staging/run-tests.py --upstream /path/to/xdj-az-mods --baseline-ordinary
```

The diagnostic repetition command used was:

```sh
python3 experiments/stems-recovery-staging/run-tests.py --upstream /path/to/xdj-az-mods --asan-repeat 30
```

The runner applies the patches in a temporary tree and compiles extracted production functions with `-std=c11 -Wall -Wextra -Werror -O1 -g -pthread`. It extracts the actual load, decoder, lifecycle, flusher, envelope, retry, and `pbread_hook` functions rather than mirroring their behavior. Production types and constants are extracted from the pinned patch stack. The prior publication fixture contributes declarations and native stubs, not its tests.

The fixture uses real loader and decoder pthreads, synthetic 4,096-frame stereo codec output, temporary stem filenames, a synthetic memory value, and native-read stubs. It extracts only the actual completion tail of `ofs_load_measure`; earlier codec/correlation work is represented by fixed fixture locals. It does not test real offset calibration, Vorbis decoding, disk throughput, or audio deadlines.

On all four decks, the fixture checks:

- A real ordinary load refuses admission one byte below the reserve; private recovery is admitted at the boundary.
- Resume during private decoding and after completion preserves public deck and envelope snapshots, public exports, availability, PCM output, and the full returned Position in the actual `pbread_hook` path with the fixture's gain, mute, and sum controls.
- Completed staging does not activate, does not repeatedly retry, and is freed on drop.
- Replacement during decode retains buffers until joins, discards stale staging, preserves the replacement identity, and permits an ordinary load.
- A worker is paused at the actual `loading=0`, private-mode-still-set completion window. A second thread rendezvous at its actual advancement-lock attempt tests both drop and a consumed restart request. The atomic-store observer also checks that private mode is not cleared while loading remains set.
- The actual flusher handles eject debounce, retained restart state across empty ticks, a confirmed replacement, and subsequent ticks without a duplicate load.
- Recovery offset completion observes all decoder joins, while ordinary offset completion still precedes decoder joins in the parallel ordinary fixture.

The snapshots retain nonzero envelope bytes, generation, final state, counts, directory, and offset sentinels. Availability and public PCM are initially absent, as required for recovery admission. The complete post-render hook, raw diagnostic fread path, and real audio caches are not executed by this harness. Private-pointer isolation leaves their public inputs unchanged by source inspection, but that is not equivalent to a complete runtime test. In particular, the existing raw diagnostic fread mode can already emit zeros with absent public stems; this patch preserves its baseline behavior and does not claim native PCM in that mode.

## Recorded results and open gate

| Evidence | Result | Meaning |
| --- | --- | --- |
| Earlier candidate run | Plain 528 passed; ASan/UBSan 527 passed, 1 failed | Unexplained ordinary-restart assertion; full failure state was not retained. |
| [ARM64 build](build-info.json) | Compiled with GCC 7.5.0 and `-Wall -Wextra -Werror` | Local binary only; no emulator installation or physical test. |
| [Current host run](test-results.txt) | Plain 528/0; ASan/UBSan 528/0 | These particular runs passed; they do not resolve the earlier failure. |
| [Diagnostic repeats](asan-diagnostic.txt) | 30 ASan/UBSan runs, each 528/0 | Failure did not recur; no causal fix or explanation established. |
| [Baseline ordinary TSan](baseline-ordinary-tsan.txt) | 28 assertions passed; TSan reports a data race | Reproduced with only the two preceding patches and no staging patch. |
| Current private-recovery TSan | 524/0; no TSan report in this run | Ordinary decoder fixture is deliberately serial; recovery remains threaded. |

The baseline TSan report concerns a normal progressive path reading PCM through `advance_stem_frames` while a decoder writes it. The private-recovery TSan configuration serializes only ordinary decoding to isolate that known finding; it uses no sanitizer suppression. Recovery decoding, the worker, and forced completion/drop/restart schedules remain threaded. The condition-variable schedules deliberately order fixture state and selected production accesses, so this is not a general race-free or reclamation proof.

The unexplained ASan assertion was `consumed restart still launches ordinary load`. Later diagnostic logging would retain frames, mode, loading, stale, hold, restart, and decoded counters if it recurred. Those values are unavailable for the failed run. Fixture join bookkeeping was subsequently corrected to avoid matching recycled pthread IDs to already-joined entries, and the ordinary-offset assertion was strengthened. Neither change has been shown to explain the failed frame assertion.

The [focused restart diagnostic](diagnostics/README.md) now traces actual per-thread counter observations and independently verifies worker returns and joins. Its 1,000 bounded restart cases passed; a named codec-EOF control correctly retained a failing original assertion with all workers joined and zero decoded counters. This supplies a narrower discriminator for a future failure, not a causal explanation or a reason to open the installation gate. Original evidence remains unchanged.

A possible weak-memory interaction in the unchanged ordinary progressive minimum scan was considered during review, but was not reproduced or established as the cause. No forced-stale atomic model is presented as a production reproduction. Repeated passes, an independent baseline TSan finding, and a successful ARM64 compilation do not close this failure. No staging emulator installation should proceed until the failure is understood or the validation contract is explicitly revised with evidence.

The next implementation decision belongs to the [activation design options](../../docs/STEMS-RECOVERY-ACTIVATION.md). Staging alone cannot make the stems pads usable and must not be described as fixing Henry's physical missing-stems incident.

License: GPL-3.0-only, matching upstream.

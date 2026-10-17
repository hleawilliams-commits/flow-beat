# Stems recovery: resume during decode/publication

This is a **reproduction-only core experiment**. It adds no production patch,
community package, emulator install, or physical-device change. A passing test
means the existing activation gap was reproduced, not fixed.

The preceding [memory retry](../stems-memory-retry/) and
[activity follow-up](../stems-retry-activity/) experiments can schedule a refused
load after fresh observations show stillness. They do not keep that recovery
inactive when the DJ resumes while the worker decodes and measures its offset.

## Concrete result

On all four fixture decks, the actual scheduling predicate accepts stillness.
A real worker thread fills the decoded-frame counters while the offset gate is
held. The main thread publishes two resumed playhead observations. The worker
then executes the unchanged offset-completion block: it opens the gate and the
unchanged `advance_stem_frames()` publishes 4,096 frames despite that observed
motion. The track generation remains the same.

The complete, unchanged `pbread_hook()` is then called against synthetic native
PCM and synthetic stem buffers. Its resulting PCM differs from the native
fixture as follows, identically on Decks 1–4:

| Stem state | Previous original/sum state | First 2,048-frame read changed | Next read changed |
|---|---|---:|---:|
| All four enabled, gains at unity | Original | 0 | 0 |
| All four enabled, gains at unity | Sum / initial zero state | 1,024 | 0 |
| Vocal muted | Either | 2,048 | 2,048 |
| One stem gain at 0.5 | Either | 2,048 | 2,048 |
| Drum buffer missing | Either | 2,048 | 2,048 |
| Forced-sum mode | Either | 2,048 | 2,048 |

The all-on/unity distinction comes from the existing 1,024-frame sum/original
crossfade and its `g_orig_prev` history. `stems_play_original()` returning true
does not imply that the first read is untouched: an initial/previous sum state
still blends the stem sum into that first block. A previous original state
does leave native PCM untouched. Neither result measures audible severity.

The fixture also compares **all 32 bytes** of the returned native `Position`,
including nonzero sentinel bytes outside the sample field; the hook preserves
the native result in these cases.

## Why one final worker-side activity check is insufficient

A second deterministic schedule executes the actual `hook_pbi()` prefix through
its playhead observation. The prefix first calls `g_orig()` to render native
audio. That native fixture synchronously calls the complete `pbread_hook()`.
Only after rendering returns does the prefix publish `stem_play_observe()`.

With the recovered frames already published and a vocal muted, that first
resumed render replaces all 2,048 native frames while the activity counter still
has the stopped value. The actual prefix then records the resumed position.
The runner verifies the pinned function has exactly one native render call and
one observation call, in that order; the test executes the ordering rather than
relying solely on the text check.

The fixture's separate equality comparison illustrates a **hypothetical** final
worker snapshot. It is not a proposed production guard. Such a snapshot can pass
before the first resumed read because movement has not yet been published. The
test does not establish that every real resumed render takes this synchronous
route, or that every native position refresh is timely. It establishes one
permitted ordering that defeats the claimed guarantee.

## Identity/stale positive controls

The threaded fixture directly calls the real `deck_track_changed()` and
`drop_stems()` while a simulated decode is in flight. The real offset-release
block respects the stale flag, the gate stays held, and the real publication
function leaves zero usable frames. The subsequent real `pbread_hook()` retains
native PCM. These controls confirm the tested recognized-stale path still
works; they do **not** run the full flusher debounce or loader join/free path.

Separately, the runner reuses the existing 46-assertion track-generation suite
with extracted `stems_tick()`, `deck_track_changed()` and `memory_retry_ready()`.
Its fixture covers the actual flusher identity/retry decisions, including eject
debounce, but its loader/free dependencies are stubs. Neither suite proves
loader ownership or allocator lifetime during all concurrent track changes.

## Reproduce

From this repository root:

```sh
python3 experiments/stems-retry-publication/run-tests.py \
  --upstream /path/to/xdj-az-mods \
  --manifest experiments/stems-retry-publication/extraction-manifest.json
```

The runner reads Git objects at upstream commit
`3e0c5cb010454228f4ee63226b60d060494eeaae`, applies the memory-retry patch and then
the activity-followup patch **only in a temporary directory**, and extracts:

- Complete `memory_retry_ready`, `advance_stem_frames`, `deck_track_changed`,
  `drop_stems`, `stem_play_observe`, `stem_retry_play_activity`,
  `playhead_obs_ok`, `stem_gains`, `stems_play_original` and `pbread_hook`.
- The unchanged `ofs_load_measure` gate-release tail in a fixture wrapper.
- The unchanged `hook_pbi` prefix through observation, with only its enclosing
  function renamed. The later mix/mute/meter portion is not executed.
- The production `deck_stems`, `Float2` and `Pos32` types, plus constants including
  `XF_ORIG`, `POS_SAMPLE_OFF`, `AZ_EP147_RATE`, `PLAYHEAD_MAX`, the memory floor,
  deck/stem counts, and the offset-state enum.

The native object offsets in the fixture use `offsetof` on safe host objects.
They are explicitly **not firmware offsets**. Block/buffer lengths and synthetic
PCM are fixture choices. Unrelated routing/diagnostic functions are stubs, TLS
names the tested deck, and content matching is disabled. The native renderer,
codec, offset measurement, envelope updates and real allocator are not under
test. [The extraction manifest](extraction-manifest.json) records patch hashes,
patched source hashes, complete function hashes, compiled extraction hashes,
and which constants are production-derived versus fixture-defined.

## Validation and limits

The recorded macOS host run passes **560 publication/ordering assertions and 46
existing identity/retry assertions in each of plain, ASan/UBSan and TSan builds**.
See [test-results.txt](test-results.txt). The publication test creates 44 actual
worker threads, using condition-variable rendezvous to force the intended
schedules. The four delayed-observation cases execute synchronously through the
extracted native-render prefix.

The rendezvous deliberately serializes plain fields such as `stem_frames`;
TSan checks these specific concurrent schedules. It does not establish that the
production engine is generally race-free or that unscheduled interleavings are
safe. The reused identity suite is single-threaded. This work performs no ARM64
build, emulator audio measurement, timing measurement, or physical test.

The 400 MiB reserve and all official update protections are unchanged. Recovery
still cannot make four arbitrarily long stem sets fit in RAM. These tests do not
claim a cause for Henry's earlier audible symptoms or change the measured
physical capacity findings.

## Design consequences, not an implementation

A worker-completion check alone is too late: progressive chunks can publish as
soon as the offset gate opens, before decoder threads join. A last-second motion
snapshot alone also has the render-before-observation hole reproduced above.

A future recovery design needs a recovery-specific staging/activation lifecycle,
an immutable track/job identity, and a deliberate activation decision that can
be invalidated by a track change. The UI must distinguish staged recovery from
usable stems: current pending availability is published before decoding.
Explicit user activation is one option to investigate. An audio-side activation
protocol would additionally need a trustworthy transport/identity contract and
coverage of the asynchronous native readers; stale position equality is not an
authoritative pause signal. None of these alternatives is implemented here.

License: GPL-3.0-only, matching the upstream source.

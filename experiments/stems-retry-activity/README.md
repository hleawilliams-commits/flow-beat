# Stems retry: observe motion between recovery checks

This is a **separate core experiment**, following [the original memory-retry patch](../stems-memory-retry/README.md). It is not a community package or physical-device update. It does not increase available memory or make four arbitrary long tracks fit. No core binary is included.

## Reproduced eligibility errors

The original retry compares one playhead position every roughly 200 ms. A moving loop can return to the same position between checks. A cached position can also remain unchanged after valid audio observations stop. In either case, the original predicate can mistakenly schedule recovery as though it had observed a pause.

The focused test extracts the actual predicate from the archived patch applied to pinned upstream. On all four decks it wrongly permits recovery for both cases: eight expected control failures. This is a deterministic code-level reproduction, not a claim that either case caused Henry's physical missing-stems reports.

## Follow-up

`activity-followup.patch` applies **after** `../stems-memory-retry/memory-retry.patch` on upstream commit `3e0c5cb010454228f4ee63226b60d060494eeaae`.

The audio hook publishes a packed atomic counter containing valid observations and observed position changes. The flusher requires fresh observations with no observed motion across its stillness window before scheduling a retry. Motion that occurs and returns between flusher samples is retained by the counter; a stopped observation stream resets the window.

The existing public playhead export remains unchanged. The counter and reader are internal to the engine. The audio path performs no allocation, logging, clock call or blocking operation. A compile-time assertion requires lock-free 64-bit atomics. Two atomic read-modify-write operations replace the prior relaxed playhead store; their real-time cost and cross-core contention still require measurement on hardware.

The 400 MiB reserve, memory admission, same-track identity, other-loader guard, and bounded retry checks remain intact. Track-generation tests continue to cover ejection and replacement.

## Validation

From this repository root:

```sh
python3 experiments/stems-retry-activity/run-tests.py --upstream /path/to/xdj-az-mods
```

The runner reads the pinned files from Git and applies both patches only in a temporary directory. It never changes the supplied checkout, emulator or physical device.

- Original retry: eight expected failures for moving-loop and stale-observation cases.
- Follow-up: 52 activity assertions and 46 existing track-generation/retry assertions pass in both plain and ASan/UBSan builds.
- ARM64 release build passes `-Wall -Wextra -Werror`; no external atomic helper dependency, maximum required GLIBC version 2.17.
- The exact ARM64 binary was temporarily loaded in the localhost emulator. All four test tracks decoded successfully. Every deck's observation count advanced while paused, its motion count advanced during playback, and observations continued without motion after pausing again.
- The previous emulator stems engine was restored byte-for-byte and both core hashes verified. This restart unloaded the emulator's test tracks. No physical-deck access occurred in this experiment.

See `test-results.txt`, `emulator-activity.jsonl` and `build-info.json`. The host fixtures are single-threaded; they do not establish audio deadlines or all producer/consumer interleavings. The emulator check validates the new observation mechanism, not a repeat of the earlier full memory-pressure recovery suite.

## Required before hardware promotion

This patch gates **when recovery is scheduled**. It does not guarantee that playback remains paused while a worker decodes, measures offset and publishes stems. The DJ can resume playback during that interval. A resume-during-recovery test and an appropriate activation/publication guard are still needed before claiming that recovery cannot switch stems on during playback.

Publication alone does not prove an audible transition. Pinned upstream's `stems_play_original()` (`src/azstem/stem_envelopes.c`) returns true when all four stem buffers are present and enabled, their gains are unity, and `g_allsum` is off; the `pbread_hook` injection path uses that result to retain native PCM. The remaining regression must therefore cover both that ordinary case and an already-muted stem, non-unity gain, missing stem and forced-sum mode. Capture the actual injection path as well as frame availability; a counter becoming positive is not an audio-quality measurement. Do not treat a passing all-on/unity case as proof for the other cases.

Fresh equal position observations establish observed stillness, not an authoritative transport-pause signal. A native position that remains stale while callbacks continue is a separate unresolved limitation. Do not present this as a complete safety guarantee or deploy it to a practicing board.

The earlier experiment's successful moving-deck tests remain historical evidence for those particular runs. This follow-up narrows the claim and records additional cases that must be covered. The physical memory evidence remains in [the incident note](../../docs/evidence/physical-stems-2026-10-17/README.md).

License: GPL-3.0-only, matching the upstream source.

# Stems recovery: activation contract still required

Source review against upstream `3e0c5cb010454228f4ee63226b60d060494eeaae`, with the separate [memory retry](../experiments/stems-memory-retry/) and [activity guard](../experiments/stems-retry-activity/) experiments. This is a design record, not an implementation or permission to install a core. It does not explain every physical missing-stems incident or increase memory capacity.

The [production-path regression](../experiments/stems-retry-publication/) now demonstrates a gap after recovery has been scheduled. The paused-observation test is a scheduling condition; it cannot establish that the deck remains paused through decoding and publication. Do not promote the current retry patches as a finished stems fix.

## Existing source behavior

| Path | What is established |
|---|---|
| `key_shift.c: hook_pbi` | Calls the native render function before publishing the playhead observation. That native call can invoke `pbread_hook`. The first resumed read can therefore precede the motion signal used by recovery. |
| `ofsload.c: ofs_load_measure` | Releases the offset hold and calls `advance_stem_frames` unless the load has been marked stale. Decoder threads need not have all joined. |
| `stem_playback.c: advance_stem_frames` | Progressive chunks can publish usable frames once the hold is open. Checking only `loader_thread` completion is too late. |
| `stem_envelopes.c: stems_play_original`; `key_shift.c: pbread_hook` | All-on/unity can retain native PCM, but the existing cold/previous-sum crossfade changes the first block. Muting, non-unity gain, a missing stem or forced-sum mode takes the injection path. Availability alone is not an audio measurement. |
| `sdk_deck.c` / `sdk_logic.h` | SDK playing is inferred from motion with a hold period. It is not an independently verified native transport state. |
| `stem_playback.c: stems_tick` | Track replacement and ejection are debounced. The public generation can lag the underlying change. `deck_track_map.c` already copies native loaded-track identity, but that is not transport state. |

A final worker-side activity comparison would catch some observed motion, but leaves a check/use window and cannot see a first resumed render that occurs before the observer updates. Moving that comparison closer to publication does not close the contract.

## Requirements for an automatic recovery prototype

1. Give every recovery job an immutable identity and generation. Keep ordinary new-track loading distinct.
2. Hold publication through offset measurement and full decode. Recovery must not expose pending availability, pads or envelope exports before acceptance; `env_cache_load` and progressive envelopes also publish visible state.
3. Latch cancellation on observed motion, invalid identity or track replacement. Moving and stopping again must not revive the same job.
4. Keep loaded-track identity under one owner. Existing worker completion checks stale state and later writes `loaded_track`; a new design must not retain that check/write ownership gap.
5. Join decoder writers before discarding buffers. Buffers must remain unpublished on cancellation. The pre-existing plain fields/fences are not an independently established reader-reclamation protocol.
6. Identify a verified activation boundary that preserves native audio until acceptance. No authoritative pause signal or suitable atomic native transport transaction has been established by this review.

The existing 400 MiB reserve, memory admission, signed update protections and separate core packaging remain unchanged. Passing host checks is not evidence of audio deadlines or hardware safety.

## Alternative requiring deliberate intent

A separate prototype could prepare recovered stems and wait for a per-deck **Enable recovered stems** action carrying the exact prepared token. This would avoid automatic activation inferred from stillness. It is only an option: no new button, workflow or product decision has been implemented, and it would be a core UI/engine change.

The current mask files cannot authenticate that action. `write_stems` also writes masks for track resets, availability changes, settings and screen transitions. `read_stems` polls the latest mask every 50 ms without event identity; a changed mask is not necessarily a fresh user command.

Using physical pads instead requires more work. In `bus_interpose.c`, CRC-checked `bus_erp_frame` calls `erp_side`, which resolves the logical deck and detects rising edges; `erp_pads_stems` queues coalesced bitsets. Those requests carry no prepared token. The path is active only when stems are available and engaged, so an unavailable deck's pads retain their native Beat Loop behavior. Do not silently reinterpret a native pad press as activation.

Any intentional pad-based design would need a distinct visible prepared state, verified mode, a valid all-pads-up frame after readiness or deck selection, then a fresh rising edge tagged with logical deck, prepared token, operation and event sequence. Consumption must reject stale, duplicate and expired tokens. Invalid CRC, held pads, old queued taps, mode changes, same-name replacement and eject must never activate recovery.

`advance_stem_frames` takes an off-audio mutex and performs envelope work; do not invoke it from the audio or pad-input callback. Native cached/prefetched reads and key-shift caches also need explicit treatment. There is no proven single audio owner here that makes casual pointer publication or cache eviction safe.

## Next evidence

First retain the exact injection reproduction and its limits. An implementation must then demonstrate native PCM preservation until its justified activation point, track-token ownership and writer/reader lifetime under forced schedules. Measure audio costs separately. Until that contract exists, keep these patches as source-only development work and preserve the official physical core.

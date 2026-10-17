# Bounded ordinary-load emulator check — 2026-10-17

The standalone ordinary-publication candidate completed one native four-deck load, progressive and final metadata checks, and brief native play/pause checks in the ARM64 emulator. The same existing stem-bearing fixture was loaded on all four decks. The exact emulator baseline was restored afterward. No physical deck was accessed and no package or release was created.

The source stack was pinned upstream `3e0c5cb010454228f4ee63226b60d060494eeaae` plus only `ordinary-publication.patch` (`b4ff1224d1f0faaa4e248d564d9a053877b644e277eb2d5d0ebe011623501a34`). The installed stems binary was `f1dd479b11b7551903de899e8513b962ab8d943b5b5adcf9f68c27d2d055c0fe`. The existing UI core was unchanged. Memory-retry, activity-follow-up and private-staging patches were absent.

## Observed result

Six loading samples spanned 2.647 seconds from first to last sample. All four decks were observed first with zero public frames, then at intermediate public frame counts, and finally with 8,599,552 frames each. This interval is a sampling span, not a precise load-duration benchmark.

| Check | Result |
| --- | --- |
| Native all-four LOAD sequences | Exactly one |
| Track and actual source identity | All four matched the intended private fixture |
| Progressive public frames | Observed on decks 1, 2, 3 and 4 |
| Final decoded/public frames | All 16 decoded slot counts and all four public counts were 8,599,552 |
| Final presence and pointers | Mask 15 and four non-null pointers on each deck |
| Final loading / pending / hold / stale | Zero on all four decks |
| Final envelopes | Ready and final on all four; envelope frames matched public frames |
| Brief native playback | All four observed positions increased between samples |
| Native pause | All four observed positions were unchanged between the two paused samples |
| Process stability | Same process throughout the load and playback checks |

The [sanitized samples](samples.json) preserve the measured fields and sample timing, with music titles, source paths and process identifiers removed. Array order is deck 1–2–3–4. The [result](result.json) records scope, corrections, binary provenance and restoration checks. [Private artifact hashes](private-provenance.json) identify the exact local scripts, raw telemetry and backup; sensitive raw contents remain outside Git.

## Test corrections retained

The first startup helper could accept a prior `loaded` status file before new initialization completed. A subsequent check detected the new startup guard before any LOAD was issued. Testing proceeded only after verifying the new process, its candidate mapping, a newly written loaded-status timestamp and natural guard absence. The restore helper was corrected to require a fresh loaded timestamp first; it then observed the guard and waited for it to expire naturally. No code deleted or reset `.starting`. The inspected initialization source writes the guard before loading mods and publishing the loaded-status file; its healthy tick clears the guard after 90 seconds.

The original final-state assertion compared the visible song title with the native loader's file basename. The basename includes a numeric prefix. The original assertion stopped before playback even though all four actual source paths matched. Its script and an explicitly labeled observed failure excerpt were retained privately. The parent approved completing only the remaining checks on the existing load. A JSON list-versus-tuple comparison in the first continuation also stopped before playback controls; only the container-type comparison was normalized. The corrected continuation used the same six load samples and process, issued no LOAD, and passed the remaining checks. No production source changed for either test correction.

## Restoration and limits

A fresh backup covered 124 regular files, the scoped root inventories, tar-derived modes, two absent paths, loaded mods, both core hashes and USB envelope caches. After the check, every saved file hash/type/mode and inventory matched, the absent paths remained absent, the cache set and contents matched, and both original cores were restored. A fresh startup produced four empty decks and an identical loaded-mod set. Its startup guard was observed and cleared naturally. Emulator ownership was then released.

This is one fixture repeated across four decks, not four arbitrary tracks or a memory-pressure test. Sequential `/proc` reads are not atomic snapshots. Playhead motion and pause do not measure audio quality, audible stem isolation or real-time deadlines. This run does not test missing-stem recovery, private staging or activation, ejection/replacement races, complete audio-reader lifetime, or physical-device behavior. It neither explains the earlier staging ASan assertion nor reopens that experiment's runtime gate.

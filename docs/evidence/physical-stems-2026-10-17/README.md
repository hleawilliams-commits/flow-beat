# Physical missing-stems incident — October 17, 2026

Read-only observations from Henry's XDJ-AZ during practice. Raw captures remain private outside this repository. Track A, Track B and Track C are anonymous labels; no music, source paths, access configuration or raw logs are included here.

## Running setup

The core identified EP147 2.00 as `ep147-9e9454e357389ece`, with stems enabled. Community mods were not suspended. Grouped Deck Panels 0.1.0 was loaded alongside FLOW Beat 0.2.1, Deck Overview 1.0.0, Harmonic Picks 0.1.0 and Phase Strip 0.1.1. Key next to BPM, Phrase Coach and Set Clock were turned off.

The staged official libraries matched the original USB baseline:

| Staged library | SHA-256 |
|---|---|
| `azmod.so` | `5f444898d258cb92bd78499b7d4a0631d9f29279cc47ae5d46df7b7d0c8efc84` |
| `azstem.so` | `3e0205320a0dda69a72e1de5070be82c89fb9e73ec21ce3f561c1dbb23d60ea6` |

These hashes verify staged files, not the process's mapped memory. The experimental stems memory-retry core was not deployed.

## Initial capture

The user initially attributed yellow Beat Loop pads to **Track A on physical Deck 3**. Its decoded stems and availability were already present. The user later clarified, **“it may have just been deck 4 not load stems.”** Deck 4 has a confirmed unavailable state in several read-only captures. Treat the earlier Deck 3 attribution as uncertain; the snapshots do not establish a separate pad-routing bug:

| Physical deck | Anonymous track | Decoded frames | Availability | Control mask |
|---|---|---:|---:|---|
| 3 (`d2`) | Track A | 19,187,376 | 1 | `0x86`: engaged; other and bass enabled, vocals and drums disabled |
| 4 (`d3`) | Track B | 0 | 0 | `0x00`: disengaged |

All four decks were classified loaded. Per-deck source and current-track records agreed with their respective filenames; availability and control files agreed with telemetry. The master stems enable was on. No hold or stale-load flag was set; the mask-poll worker was running, and no stall was recorded.

Track B's exact matching folder contained four nonempty Ogg files: vocals, other, bass and drums. Its `source.json` path and size matched the original audio file. The audio fingerprint was not recomputed, and the Ogg files were not decoded or otherwise validated. File presence alone does not establish that the engine successfully loaded them.

Available memory was **466,608 kB**, about **455.7 MiB**. The official engine reserves 400 MiB before accepting a new stem allocation. This is relevant to Deck 4’s unavailable state. It would not explain yellow pads on Deck 3 while its decoded buffers were present, if that earlier attribution was correct. The snapshot does not establish the load-time failure: `azstem.log` did not exist, so no historical refusal message was available.

## Follow-up during normal practice

The user changed tracks between captures; this was not a controlled retry test. The source records changed for both physical Deck 3 (`d2`) and Deck 4 (`d3`). Deck 3 then had 12,160,575 decoded frames and control mask `0x8e`; Deck 4 still had zero frames and mask `0x00`. All four decks remained loaded, and EP147 kept the same PID.

Available memory rose to **845,660 kB**, about **825.8 MiB**. That later value does not reveal memory available at either load, the new track's allocation requirement, or whether decoding failed for another reason. The captures contain no historical refusal message. These snapshots do not establish that more memory alone would resolve the issue, or that the unavailable deck was stuck on the same track.

## Bounded duration check and current capacity

At **18:21:25 UTC**, physical Deck 4 still had zero decoded frames and availability 0. Its source was Track C, unchanged since the earlier follow-up. Decks 1–3 had 16,030,350, 17,332,319 and 13,312,475 decoded frames. Available memory was **638,128 kB (623.17 MiB)** and locked memory was 1,462,520 kB. No hold or stale-load flag was set.

The assistant read only the first 512 and last 65,536 bytes of each of the four Ogg files for Track B and Track C. Every identification header reported stereo at 44,100 Hz; every final page was a complete end-of-stream page with a valid Ogg CRC. The final granule positions agreed across all four stems. This establishes their declared decoded lengths, not full decode integrity. Track C's `source.json` path and size also matched its source audio file.

| Track | Declared duration | Four stereo float buffers | Available memory required including the 400 MiB floor |
|---|---:|---:|---:|
| Track B | 345.259 s | 487,229,568 bytes / 464.66 MiB | 864.66 MiB |
| Track C | 247.500 s | 349,272,000 bytes / 333.09 MiB | 733.09 MiB |

The pinned engine allocates 8 bytes per output frame per stem. At the initial snapshot, Track B had only 55.67 MiB available beyond the safety floor, far below its 464.66 MiB requirement. At the latest snapshot, Track C had only 223.17 MiB beyond the floor, below its 333.09 MiB requirement. Under this admission rule, a fresh load at those respective snapshot states would be refused. The reserve is retained to protect the rest of the deck; lowering it is not the proposed fix.

At the earlier follow-up, 825.84 MiB was available, sufficient for Track C's 333.09 MiB buffers plus the reserve, yet its frames were zero. The current source record confirms it was the same Track C. The pinned upstream code records the track identity even after a failed load, and an unchanged identity does not retry an ordinary failure. This is the same persistent failure mechanism reproduced in the emulator. Together these observations strongly support temporary capacity pressure followed by no retry as the leading explanation, but they do not provide the missing historical refusal log or rule out a different initial load failure.

Saved stems are compressed files on the USB. This engine still decodes each loaded track's four stems into locked working RAM. Pre-generating stems avoids stem separation on the deck; it does not remove that RAM requirement.

## Limits and next controlled test

The current confirmed issue is **physical Deck 4 remaining without decoded stems**. The observations do not establish the historical failure reason or implicate Grouped Deck Panels. If the earlier Deck 3 yellow-pad attribution was correct, its `0x86` mask records existing stem selections but does not prove that its pads were routed to stems at capture. The assistant collected files, bounded header/tail bytes and metadata only: no audio/control actions, configuration changes, process restarts or core writes were performed during diagnosis.

The controlled capacity test proposed for Deck 4 was **not executed**: wait until the user is ready, unload an unused deck to release its stem buffers, then load a distinct track on Deck 4 before returning Deck 4 to the target track. Capture memory, availability and decoded-frame state around each step. That test concerns Deck 4's unavailable stems and does not promise that four arbitrary tracks' stems will fit or work together. Revisit native pad mode only if a yellow-pad symptom is confirmed while that same deck has decoded stems.

The separate [emulator memory-refusal investigation](../../../experiments/stems-memory-retry/README.md) demonstrates recovery after a refused load. It remains an emulator-only core experiment, not a deployed physical fix.

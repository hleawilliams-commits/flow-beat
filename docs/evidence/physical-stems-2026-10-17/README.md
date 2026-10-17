# Physical missing-stems incident — October 17, 2026

Read-only observations from Henry's XDJ-AZ during practice. Raw captures remain private outside this repository. Track A and Track B are anonymous labels; no music, source paths, access configuration or raw logs are included here.

## Running setup

The core identified EP147 2.00 as `ep147-9e9454e357389ece`, with stems enabled. Community mods were not suspended. Grouped Deck Panels 0.1.0 was loaded alongside FLOW Beat 0.2.1, Deck Overview 1.0.0, Harmonic Picks 0.1.0 and Phase Strip 0.1.1. Key next to BPM, Phrase Coach and Set Clock were turned off.

The staged official libraries matched the original USB baseline:

| Staged library | SHA-256 |
|---|---|
| `azmod.so` | `5f444898d258cb92bd78499b7d4a0631d9f29279cc47ae5d46df7b7d0c8efc84` |
| `azstem.so` | `3e0205320a0dda69a72e1de5070be82c89fb9e73ec21ce3f561c1dbb23d60ea6` |

These hashes verify staged files, not the process's mapped memory. The experimental stems memory-retry core was not deployed.

## Initial capture

The user subsequently confirmed that the reported problem was **Track A on physical Deck 3**: the pads stayed in the stock yellow Beat Loop mode and did not control stems. Its decoded stems and availability were already present. The separate unavailable state on Deck 4 was also observed, but does not explain the reported Deck 3 pad-routing/display symptom:

| Physical deck | Anonymous track | Decoded frames | Availability | Control mask |
|---|---|---:|---:|---|
| 3 (`d2`) | Track A | 19,187,376 | 1 | `0x86`: engaged; other and bass enabled, vocals and drums disabled |
| 4 (`d3`) | Track B | 0 | 0 | `0x00`: disengaged |

All four decks were classified loaded. Per-deck source and current-track records agreed with their respective filenames; availability and control files agreed with telemetry. The master stems enable was on. No hold or stale-load flag was set; the mask-poll worker was running, and no stall was recorded.

Track B's exact matching folder contained four nonempty Ogg files: vocals, other, bass and drums. Its `source.json` path and size matched the original audio file. The audio fingerprint was not recomputed, and the Ogg files were not decoded or otherwise validated. File presence alone does not establish that the engine successfully loaded them.

Available memory was **466,608 kB**, about **455.7 MiB**. The official engine reserves 400 MiB before accepting a new stem allocation. This is relevant to the separate unavailable state on Deck 4, but does not explain the confirmed Deck 3 symptom, whose decoded buffers were present. Even for Deck 4 it does not establish the load-time failure: `azstem.log` did not exist, so no historical refusal message was available.

## Follow-up during normal practice

The user changed tracks between captures; this was not a controlled retry test. The source records changed for both physical Deck 3 (`d2`) and Deck 4 (`d3`). Deck 3 then had 12,160,575 decoded frames and control mask `0x8e`; Deck 4 still had zero frames and mask `0x00`. All four decks remained loaded, and EP147 kept the same PID.

Available memory rose to **845,660 kB**, about **825.8 MiB**. That later value does not reveal memory available at either load, the new track's allocation requirement, or whether decoding failed for another reason. The captures contain no historical refusal message. These snapshots do not establish that more memory alone would resolve the issue, or that the unavailable deck was stuck on the same track.

## Limits and next controlled test

The primary open issue is **native pad mode, routing or display on physical Deck 3 despite loaded stems**. The observations do not establish its cause or implicate Grouped Deck Panels. Track A's `0x86` mask records existing stem selections; it does not prove that its yellow Beat Loop pads were routed to stems at capture. The assistant collected files and metadata only: no audio/control actions, configuration changes, process restarts or core writes were performed during diagnosis.

For the reported Deck 3 issue, inspect the current native pad mode and stems/Beat Loop configuration before changing playback. The separate capacity test proposed for Deck 4 was **not executed**: wait until the user is ready, unload an unused deck to release its stem buffers, then load a distinct track on that deck before retrying the target. Capture memory, availability and decoded-frame state around each step. That test concerns Deck 4's unavailable stems, not Deck 3's confirmed pad-routing/display symptom, and does not promise that four arbitrary tracks' stems will fit or work together.

The separate [emulator memory-refusal investigation](../../../experiments/stems-memory-retry/README.md) concerns failed stem loading. It is not a diagnosis or physical fix for the reported Deck 3 pad-routing/display issue.

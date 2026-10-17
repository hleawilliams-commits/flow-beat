# Late beat-grid recovery — isolated core experiment

A focused test confirms a missed-retry bug in the pinned upstream beat-grid service. This is a possible cause of the reported Deck 2 phrase counter staying unknown; **it is not a confirmed diagnosis of that physical incident**. No physical grid telemetry was captured when that counter failed.

The source patch is for Kyle Hosman's GPL-3.0 AZ-MODS core, not a community-mod package. An isolated ARM64 build was temporarily tested in the local emulator and then replaced with its exact original core and settings. This repository contains source and sanitized evidence, with no distributable core binary. The physical board and official update path remain unchanged.

## Reproduction

Pinned upstream: [`3e0c5cb010454228f4ee63226b60d060494eeaae`](https://github.com/Kyle-Hosman/xdj-az-mods/tree/3e0c5cb010454228f4ee63226b60d060494eeaae).

In `src/azstem/drumroll.c`, `drum_grid_service()` records the current track generation before calling `drum_grid_native()`. The native grid provider can be empty while the track is loading. If that first read fails, the service publishes an invalid grid and consumes the generation; subsequent calls see an unchanged generation and skip the deck indefinitely, even if its native grid has become available.

The test extracts the actual production service, grid state/publisher and `azstem_deck_beat_phase()` into a host fixture. Its native-grid reader is deliberately stubbed so the provider's late arrival is deterministic; settings-file reads return no file. After the first unavailable read, the fixture makes Deck 2's native grid available and invokes the service 20 more times. The unmodified service never reads it again. The actual SDK phase export remains unavailable, and the actual Grouped Panels `gp_follow_step()` cannot anchor. This reproduces the code path without claiming to reproduce the deck's real loading timeline or native-object lifetime.

Grouped Panels already rechecks `deck_grid()` and can anchor automatically once a valid phase is available while the track plays. Resetting its phrase anchor cannot repair a core that never publishes a grid. Other explanations remain possible for the physical symptom, including a genuinely absent/invalid grid or lost phrase confidence after a seek.

## Patch behavior

`grid-retry.patch` changes only `src/azstem/drumroll.c`:

- A new track still immediately invalidates the previous grid and tries the current native grid.
- An unavailable grid is retried every five flusher visits. The flusher sleeps at least 200 ms per visit, so native reads occur no faster than roughly once per second for each unavailable deck; other flusher work can delay them further.
- A successful grid remains cached for its track generation. Already valid decks are not reread when another deck retries.
- A new generation bypasses the previous retry delay. No grid from another deck and no guessed BPM/phrase are substituted.
- No native-grid reads occur while both Beat Sync and community grid demand are off. The SDK's existing demand flag remains latched after a request.
- The generic unavailable message is emitted once per generation, not on each retry. The existing native parser's specific tempo/spacing-disagreement warning is unchanged and may recur at the bounded retry cadence.

Retries run on the existing flusher, not the audio or UI thread. They add no disk access, allocation or thread; the service's pre-existing sync-offset settings read remains unchanged. Empty decks and permanently invalid grids remain unavailable and receive bounded retries while there is demand.

This shared grid also drives **Drum Roll Beat Sync**. The emulator checks below cover both SDK phrase consumers and Drum Roll on all four logical decks. The patch does not address stem-buffer memory refusal, enlarge RAM capacity, reset a DJ's phrase downbeat or establish that four stem tracks fit together.

## Validation and reproduction command

`provenance.json` records the pinned source and patched hashes. The runner reads source directly from that Git commit, applies the patch **only in a temporary directory**, verifies the hashes, and extracts the functions for both baseline and patched tests. It never modifies the supplied checkout.

From this repository root, with the original sibling SDK and upstream checkout available:

```sh
python3 experiments/phrase-grid-retry/tests/run.py
```

For another clone:

```sh
python3 experiments/phrase-grid-retry/tests/run.py \
  --upstream /path/to/xdj-az-mods \
  --sdk /path/to/az-mods-sdk
```

The baseline reproduces the failure in plain and ASan/UBSan builds. The patch passes 112 assertions in each mode: delayed grids recover independently on all four decks, SDK phases become available, Grouped Panels anchors, successful decks remain cached, changed tracks invalidate old grids, saved retry delays cannot delay a new generation, and retry cadence, generic logging, demand and generation wrap behave as expected. See `RESULTS.txt`.

## ARM64 and emulator validation

An isolated archive of the pinned upstream commit was built with **only** `grid-retry.patch`, using `linux/arm64`, the local `azmod-build:18.04` image and `make -C /g CC=gcc azstem`. Compilation passed with the project's `-Wall -Wextra -Werror` flags. All 133 upstream source files were compared with the Git archive; only `src/azstem/drumroll.c` differed, and its hash matches `provenance.json`. The core SHA-256 is recorded in [emulator-evidence.json](emulator-evidence.json); the binary stays outside this repository.

In the local emulator, one ordinary native track was loaded on all four decks. All four native grids became valid, and the actual installed Grouped Panels 0.1.0 consumer acquired and advanced its phrase count while each deck played. Across the two measured snapshots, every playhead and phrase count advanced, while grid publication sequences remained unchanged. The main screen was inspected privately; screenshots and track/library details are excluded from the repository.

Drum Roll was then exercised on each logical deck using native deck selection, SLIP LOOP pads and X-PAD input. A temporary generated bank contained four 0.1-second stereo PCM16 tones at 44.1 kHz. With Beat Sync on, each target alone was armed and touched, had a moving playhead and valid grid, cleared its first-anchor wait, and used an active 4,410-frame sample voice with retriggers. Releasing the X-PAD cleared every touch; all four gates were off at the end of the measured sequence. Numeric observations are in [emulator-evidence.json](emulator-evidence.json).

These are bounded reads of live process state, not an atomic trace or a sample-accurate audio recording. They confirm the actual consumers ran with the native grids and synthetic bank; they do not establish timing accuracy or a real-time deadline bound. The observed native loads acquired valid grids on their first new generation, so **late-grid recovery was not reproduced on native firmware**. That timing case remains covered by the deterministic production-function host test. No native pointers were modified to force it.

The emulator was stopped and its original stems core and both configuration files restored byte for byte. The unchanged UI core, Grouped Panels binary and existing log were also checked against their original hashes. Previously absent logging/sync-offset files remained absent; the generated drum bank was removed; EP147 restarted with all decks empty. No physical AZ was accessed for this validation.

Native pointer/reference handling is unchanged. Normal native acquisition was exercised in the emulator, but this validation did not rerun the upstream native-lifetime stress tests or a full-core concurrency sanitizer. The new retry state is owned by the single flusher; the focused service tests are deterministic and single-threaded.

## Review and next test

Review the patch and upstream native-grid lifetime tests before any hardware-test decision. The remaining native-firmware gate is a naturally delayed grid after the service has already consumed the track generation, or a controlled fixture outside production code that exercises that timeline. Obtain a live grid/SDK snapshot if the physical phrase counter fails again, since the earlier Deck 2 incident has not been tied to this code path. Preserve the official physical core and keep any hardware testing separate from routine community-mod updates.

License: GPL-3.0-only, matching the upstream files being patched.

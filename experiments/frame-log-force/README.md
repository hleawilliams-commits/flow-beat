# Preserve forced diagnostics in the frame log queue

Separate, unpublished core experiment against `3e0c5cb010454228f4ee63226b60d060494eeaae`. **Host regression validated; no core build, emulator run, physical installation or deployment.** This is a diagnostic fix, not a MODS Shortcut delivery fix. It does not re-enable an overrun filter or change any SDK protection.

The pinned core's `sdk_strike` calls `cm_logf(c, 1, ...)` when a filter reaches eight overruns of its 200 µs budget. Both editing and copy-filter dispatch call it while `g_cm_in_frame` is set. `cm_vlog` accepts the forced message with ordinary logs disabled and queues its text. However, `community_tick` drains every queued message with `force=0`; the logger then silently rejects the diagnostic when logs are off. The filter remains disabled even though its reason is absent from the log.

[frame-log-force.patch](frame-log-force.patch) changes only `src/azmod/community.c`:

- Preserve priority in the existing atomic `ready` value: **0 empty, 1 ordinary, 2 forced**. Any nonzero incoming `force` becomes 2.
- Read that value once with the existing acquire operation and pass `ready == 2` to `cm_logf` when draining.
- Keep the existing release publication/reset, eight 160-byte slots, structure layout, write index, drop counter, file output and log cap.

The ready value publishes the text and its priority together; no separate flag array, lock, allocation or extra atomic operation is added. Ordinary messages still require logging to be enabled at enqueue and drain. The patch keeps the existing queue algorithm and does not claim to audit or redesign its concurrency behavior.

Forced logging remains best effort: a full slot still drops the message, and the per-file cap still applies. The existing overflow summary remains ordinary and therefore stays silent with logs off. This patch changes only loss of the force flag for a message that successfully entered the queue.

## Reproduce locally

The runner reads the exact pinned sources through `git show`; it does not use or modify uncommitted core changes. The upstream checkout only needs to contain the pinned commit.

```sh
python3 experiments/frame-log-force/run-tests.py --upstream /path/to/xdj-az-mods
```

The harness extracts, without reimplementation:

- the full production `cm_mod` typedef, including the queue;
- `cm_vlog`, `cm_logf`, and the exact queue-drain loop from `community_tick`;
- `sdk_strike` and its production budget/strike constants;
- the production `azlogcap.h` dependency.

Unrelated SDK pointer types are opaque stubs. The driver supplies mod records and calls the extracted drain block directly; it does not simulate the surrounding tick scheduler, firmware dispatch or real-time timing. Production logging writes ordinary host files within temporary test directories. The runner verifies that the patch leaves the typedef unchanged except for its comment, applies the patch only in a temporary source tree, and removes all generated files and executables on exit.

On 2026-10-17, Apple Clang 16.0.0 on ARM64 macOS produced these results:

| Variant | Result |
|---|---|
| Pinned original control | **48 checks, 9 failed**, reproducing force loss, including the real overrun diagnostic. These are multiple assertions of the same missing flag, not nine different defects. |
| Patched, plain | **48 checks, 0 failed** |
| Patched, ASan + UBSan | **48 checks, 0 failed** |

Checks cover forced versus ordinary messages with logs off/on, changing the setting before drain, exact-once drain, eight-slot overflow without overwrite, unchanged overflow summaries, slot reuse in both directions, nonzero force normalization, truncation, file caps, direct non-frame logging, separate mod queues and the existing forced service-refusal summary. The actual `sdk_strike` regression verifies the unchanged threshold, filter-only disable state and once-only diagnostic. No production scheduling or concurrent stress claim follows from these host tests.

Source fingerprints and validation limits are in [validation.json](validation.json). This directory contains source, tests and the patch only; it is separate from community MODS Shortcut 0.1.2 work and is not a community package or catalog update.

# Copied-overlay V2 integration checkpoint

The separately versioned [pure policy](README.md) now has three private adapter
copies: `action_preflight_v2.py`, `action_coordinator_v2.py` and
`preinstall_frame_v2.py`. They preserve the complete validated observed frame
through preparation and action construction. These copies are not selected by
the runtime runner. No new enabling manifest, runtime run identity, connection,
installation, input or restart was made in this follow-up.

The earlier 28-offset policy and all original helpers remain unchanged. The
closed compatibility refusal remains a valid refusal by that policy. Its saved
settings, backup and observations are historical evidence, not a fresh baseline
or permission to retry. No historical emulator snapshot was restored.

## Exact change

The V2 preflight retains the second validated 152-byte observation, including
its observed CRC, after the existing fresh status/frame/frame/status checks.
The template constructor is used only to build a comparison target. The
coordinator checks the retained observation again against that target and the
permit's counter; it does not replace it with reconstructed idle/settings bytes.

The action constructor copies that entire observation into the first and
release frames. Only the shared latch, intended BROWSE button or one-step browse
counter changes. The coordinator adds the bus-2 prefix and retains both exact
153-byte payloads before any mocked send. FX TIME, packed analog low bits and
all other bytes are copied intact. The V2 preflight also refuses optimized
Python before doing work; the coordinator and pre-install helper retain their
existing assertion guards.

The host-counter helper, status helper and driver-identity helper are unchanged.
First-call ownership/freshness checks, target-before-first-call ordering,
at-most-once retained release after uncertain delivery and permanent failure
closure remain in the coordinator. A release still depends on current process,
core, mixer and retained-file ownership checks, but never on new readiness,
progress, permit or counter-age evidence. Failed ownership may prevent release;
Python `finally` cannot guarantee recovery after process termination.

## Controller-source limits

The pinned kernel `ctrl_write` replaces a complete mixer overlay under its
internal lock. With the required SPI3 CRC-prefix length of 96, subsequent reads
copy that sticky overlay and restamp CRC bytes 96–97 over bytes 0–95. The
identity check validates that writable module parameter sequentially; it does
not make the parameter immutable through a later send. Copied outgoing CRC
bytes are intentionally
retained as payload bytes; after the intended action changes they are not
claimed to be valid newly observed CRCs. No read consumes or clears the overlay.
The kernel source hash is
`9631ca05810408eb9cd40cb38fa3f90d8dd2ac1d4d09a2e73d084ea0dbe03654`.

The [source audit](source-audit/README.md) establishes 51 analog bytes plus the
FX selector and the two independent FX TIME bytes. Persistence can merge older
keys and the driver accepts arbitrary numeric offsets, so the stricter parser
remains necessary. Matching two observations does not establish exclusive
writer ownership. In particular, queued or replayed full frames from the local
preview are a potential competing writer outside the cooperative coordinator
lock. Preservation here means exact bytes relative to the validated baseline,
not proof of unchanged native behavior or state at a later send.

## Evidence and next gate

The pure-policy suite passes 2,914 checks, including 2,603 rejections and four
strict mutations, with byte-identical independent results. The separate source
audit passes 147 checks and 29 rejection controls, also independently identical.
The private actual-adapter matrix passes **1,864 checks**, including 60 expected
refusals and four strict source mutations. It executes the new collect/consume,
pre-install check and coordinator preparation/action functions. It discriminates
reconstructing the neutral frame, dropping FX TIME, rewinding the release
counter and skipping release after uncertain first delivery. Actual source
hashes are checked, while status, identity observations, clock, transport,
host-counter service and coordinator scope/lock checks are explicit test seams.

A separate **1,317-check** companion replaces the host-counter stub with the
unchanged actual helper and real temporary files. It executes alignment →
BROWSE → CW → CCW → exact original host restoration, plus alignment → uncertain
first CW → one retained release → permanent stop → host restoration. Original
counter whitespace and mode, actual action receipts and all intended full-frame
byte changes are checked. The fake controller models a sticky overlay and CRC
restamping, including one possible outcome where the uncertain first write
arrived. It does not prove that outcome for a real timeout.

Both adapter results were independently reproduced byte-for-byte. Their first
executions passed without test repair or overwritten failure. The exact result,
source and harness hashes are in [integration-result.json](integration-result.json).
Neither suite repeats the old lock/startup matrix or adds native timing,
transport delivery or exclusive-writer proof.

Raw frames, saved settings, runtime scripts, full mock transcripts and immutable
input snapshots remain private. The public record contains source hashes and
sanitized conclusions. The frozen original runner still selects the original
helpers and its closed run folder; there is no automatic fallback to V2.

Before another runtime attempt, resolve or explicitly exclude competing overlay
writers, review exact V2 runner wiring and recovery behavior, select a new run
identity, and take a fresh selective backup. A new bounded compatibility check
must succeed before installation. Preserve the natural startup guard, exact
hook ownership and original cores. Do not replay the closed attempt, strip
settings, rearm capture or reset watchdog state. Native task admission,
execution/returns, cancellation, ownership and physical timing remain unproven.
MODS 0.1.3 remains unpublished; stems staging and activation gates stay closed.

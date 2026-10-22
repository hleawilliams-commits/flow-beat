# Sampler and emulator input-baseline preparation

The passive task observer remains **uninstalled**. This checkpoint adds an
exact-binary scalar planner, a fresh runtime plan and one successful read-only
emulator-driver diagnostic. It supplies no native task entry, return,
admission, execution, completion or cancellation observations. No input frame,
service change, module installation, core change or physical access occurred.

## Offline checks

The [owned-scalar planner](status-layout/README.md) passes **81 checks including
54 rejection controls**. Independent execution produces identical result JSON,
SHA-256 `bd5377f93cb8ab59eb49a6cf14278b44add2081f67fa51f42ed946fe6cf63b00`.
Eight local atomics total 56 bytes. The public API binds the exact candidate
ELF, source and frozen map planner, checks actual scalar extents, and accepts
only complete private mappings with supported auxv page sizes. Tests use
fabricated mappings and buffers. No live observer memory was sampled.

A private review of the actual startup/recovery/sampler functions passes
**253 checks across 48 scenarios**. Fake services exercise strict guard error
handling, fresh loaded timestamps, PID/start identity, initial versus cleanup
startup waits, failed stop/action/reload/start/receipt paths, preserved primary
errors, short reads, binary/map/page/process drift and Ready/progress checks.
Closed-state diagnostics remain sampleable. This is not a full restoration,
concurrency or installed-path proof. Its immutable result SHA-256 is
`409dbc301499763cbe1b3503e5f45939df1ae0471656a42b0b3fa1e67ab48f6f`.

The [driver audit and pure policy](INPUT-BASELINE.md) pass **99 checks including
51 rejection controls**, independently reproduced with identical result
`1be70df7ada1c2780a17394799427f6e1551d257faf01a09218d2a528274c113`.
They bind the compiled SPI read handler, full GNU note, dynamic device mapping
and CRC-16/X25 checks. They are separate from live receipt acquisition.

## Retained preflight failure and corrected collector

The first private collector passes **988 mock checks across 51 scenarios**:
34 preflight failures issue zero frame-acquisition commands; 15 later failures
issue one command without retry; two cases succeed. Its live preflight then
failed because the guest's `stat` command was unavailable (exit 127). The
operation/failure receipts and raw output remain private. No acquisition-start
receipt, frame or completion record was produced. The full driver-identity
policy had not yet been evaluated; partial metadata is not a validated sample.
The successful mocks did not establish tool availability in the guest.

The revised collector obtains character-node metadata with the guest's existing
tar utility. It accepts only one physical header for the exact node, character
type, zero size, no link, no extension headers, a valid checksum, valid numeric
fields, two zero end blocks and zero trailing padding. Accepted archives are
1,536–65,536 bytes and block-aligned. It never extracts a file or follows a
symlink. The parser limit does not cap the SSH helper's buffered output, which
remains timeout-bounded. Tar metadata is a path-time observation, not identity
bound to the later acquisition's open file descriptor.

The exact pinned local initramfs contains `usr/bin/tar` as a link to BusyBox;
BusyBox SHA-256 is
`46284743c57e15a5b1a01107083865be72ea850de5d8b8062994a34b1f7c9f8c`.
That is local image provenance, not an independent audit of the running tar
executable. The resulting live metadata must still pass the strict parser and
unchanged driver policy.

The final revised collector passes **1,440 checks across 82 mock scenarios**:
62 preflight failures issue zero frame-acquisition commands, 16 later failures
issue one command without retry, and four cases succeed. Archive controls cover
malformed numbers/checksums, wrong names/types, links, extensions, duplicate or
concatenated headers, end-block truncation and oversized zero padding. An earlier
test-fixture error is retained separately: Python's tar writer normalized the
intentionally invalid permission fields. Direct malformed headers corrected
that test; no collector assertion was weakened. The final immutable result is
`87520e08c9d49aef9022c839d58fb44dfa86f69a76065d499b94a5f36c5ef934`.

## One read-only emulator diagnostic

The changed collector used a new private output directory. Its frozen source
SHA-256 is `b3cca610f6ff1b715cba9c22dbcbc138dc98ffea40a949ff0792fedb0c5ea7c0`;
its pure policy is hash-pinned. It completed one 128-byte acquisition command
after successful identity checks, validated the frame CRC, then repeated and
matched the complete driver identity. The current empty-process/start-identity
and original-core guards passed before and after the diagnostic. No acquisition
retry occurred. The earlier failed preflight remains untouched.

The sampled driver file hash is
`c97d19459a3e99116de70c0f3f781527baf710846a158a9ba051c36537fc338d`, with the exact
loaded GNU note, live state, character-node/sysfs/registered-major binding,
mixer minor 2 and CRC prefix parameter 96. Actual frame values, process details,
raw metadata and commands remain private. The completion receipt SHA-256 is
`e397c1579ad168ec1121a4f2655e6d207f0982ea88fc3788c218f00615ad4b42`.

An independent audit of saved receipts passes **36 checks**, reconstructing
both identities from the raw metadata and checking CRC through a separate
table-driven implementation. Its private proof hash is
`7c6163bccf42eb89c7589b9ee001544a4eea6301c8aaee37d44fb46cfddde312`.
Process identities match the start/completion receipts; core/empty postcheck
responses were not separately retained. Those remain successful collector
assertions, rather than independently revalidated raw responses.

This is one acquisition **command**, not proof of exactly one kernel read under
signals or internal retries. The before/after identity observations are
sequential and do not exclude every intervening mutation. CRC covers only the
96-byte prefix; bytes 98–127 are outside it. Reading the emulator's retained
MISO frame has scheduling, locking and copy cost and does not establish native
consumption, downstream counter state or hardware timing. The sample cannot be
reused as a future input baseline. No mutation rollback or restart was needed;
the diagnostic finished without taking over the emulator for an installation.

## Next action remains gated

The [runtime plan](RUNTIME-PLAN.md) requires a fresh baseline and selective
cleanup for any later installation. Initial observer readiness must be checked
before its capture budget expires; the core startup marker still clears
naturally before cleanup. Never reset either budget or treat quiet ticks as
quarantine evidence.

The unfinished input adapter remains disabled. It must replace the old RAW
aligner, support the planned single clockwise/counterclockwise steps with
receipt-proven host-counter ownership, and obtain fresh progress and counter
evidence inside each action's preflight. Existing progress receipts lack an
age/run binding and an input-review file's existence is insufficient. Complete
and test those paths, including independent host restoration, before any
observer installation or input. The eight scalars do not expose available
transport capacity, losses or coherent Ready state.

MODS 0.1.3 remains unpublished, stems staging remains closed, and USB activation,
native lifetime/cancellation and physical testing remain unresolved. This
preparation is neither a community release nor a stems recovery fix.

## Subsequent action preparation

The [action preparation checkpoint](ACTION-PREPARATION.md) adds full152-byte
neutral-frame validation, fresh action-bound sampling and receipt-owned host
recovery components. It preserves the diagnostic above unchanged. Review
corrected approval capacity, exact hook-log checks and stale/ambiguous empty
markers. Installation/input are hard-disabled until the components are
composed with a tested at-most-once first-frame/neutral-release coordinator.
No emulator connection or mutation occurred in that follow-up.

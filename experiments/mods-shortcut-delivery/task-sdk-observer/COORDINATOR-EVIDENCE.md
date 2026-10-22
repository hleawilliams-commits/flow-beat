# Input coordinator and pre-install refusal

The private input coordinator is implemented and checked offline. The current
emulator attempt stopped during host-settings validation, before any full-frame
acquisition or observer installation. No controller input, host counter change,
service restart or physical access occurred. This is preparation evidence, not
a native task observation or a release. Exact source/result hashes and scopes
are in the [sanitized result](coordinator-result.json).

## Tested composition

A session holds a non-reentrant local thread lock and a stable cross-process
file lock from fresh preflight through host bookkeeping, first-frame attempt,
retained neutral release and final receipts. Alignment, installation and
selective recovery use the same cooperative exclusion. This does not exclude
external UI tools or writers that do not use the lock.

The coordinator consumes the unchanged one-use preflight permit, retains the
exact two payloads and their hashes, and atomically records a knob target before
its first frame. It permits at most two individually requested BROWSE actions,
one CW detent and one CCW detent, in the existing host ownership contract.
Knob actions require an operator's current Waveform confirmation. Each frame
uses one call to the pinned localhost SSH function with the explicit mixer bus;
it bypasses the driver's retrying socket/fallback sender. A zero transport exit
status is not an acknowledgement of native delivery or consumption.

After an attempted first call, ordinary exceptions, timeout, result-receipt
failure or hold-delay failure lead to one neutral-release attempt in `finally`.
That release retains process/core/empty-state, payload and host/mixer ownership
checks, but no continuing Ready or permit-age condition. Failure before arming the first-call boundary sends no release; interruption
at that boundary leaves call occurrence unknown and can enter release even
when the transport call never began. A missing ownership check or inability to
persist the release intent prevents the call and records uncertainty. No press,
release, capture or failed action is automatically retried. The primary failure
is preserved separately from release/receipt failures, and further input stops.

The actual coordinator plus frozen preflight/host functions pass **2,697 total
checks across 70 scenarios**: 67 candidate scenarios and three strict mutated
controls with their expected failures. An independent run has identical result
JSON, SHA-256 `5abc0770a32a372f6ba778a4e4e3f02187726ecd36b3c48e4b4e0fe5f0d47ecb`.
Mocks supply acquisition, scalar status, clocks and transport; real temporary
files and local thread/process locks are exercised. Two cases execute extracted
pinned SSH function bodies with fake subprocesses. The suite checks uncertain
first-call release, stale/closed capture before release, receipt faults,
external drift, payload binding, action sequence, and lock contention. It does
not execute a controller write or native firmware. A first fixture failure
incorrectly expected an active reservation after an endpoint refusal that
happens before reservation; the stop-and-block assertion was corrected and the
failed run retained. The implementation was unchanged by that correction.

A read-only peer review found no blocking composition defect in that source
scope. Separate driver inspection confirms why the retrying sender and ambient
release builder are unsuitable for this coordinator. A fresh full-frame
compatibility helper passes **231 checks across 58 scenarios and two strict
controls**, independently identical. It binds the saved original process,
cores and loaded set, validates each new 152-byte frame and surrounding driver
identity, and cannot issue an input permit. An incompatible first sample stops
before a second acquisition. Its mock evidence is distinct from the actual
attempt below, which acquired no frames.

## Installation and startup boundaries

An externally supplied reviewed source-manifest hash is required before the
runner connects for installation or input. Mere existence of the manifest
cannot enable either. The manifest covers the selected private scripts and
pure policies; code bytes are checked before importing the new helper, and
selected sources are checked again for each gated operation. This is an
operator review mechanism, not a cryptographic signature or proof of readiness.
No enabling manifest was created for this refused run.

The local installation prerequisite helper verifies the frozen build inputs,
selected binary, exact original host-counter bytes and mode, absence of prior
action/recovery state, and retained sibling run-token uniqueness. It runs again
after read-only guest work and before an installation receipt or stop/upload.
Its **132 checks, 118 rejection cases and three strict controls** pass in an
independent run. Results differ only in one temporary fixture path; source,
counts and all other fields match. This is not a universal token allocator or
an atomic reservation against unrelated writers.
Peer review added explicit refusal of a retained coordinator reservation and
malformed prior installation binary identities; earlier passing matrices are
retained as superseded evidence.

The actual runner wiring passes **95 checks and 27 rejection cases**, with an
independently identical result. Extracted runner functions and CLI code execute
with mocked services and lock: the tests check gate-before-connect, both local
checks and fresh compatibility before install intent/stop/upload, one action
dispatch, one lock across transitions, partial-install recovery without an
installed receipt, and host-only continuation after guest recovery. They do not
retest component internals or prove real guest restart/rollback. An initial
mock kept reporting the opt-in after simulated removal; the actual restore
correctly refused it. That harness failure was retained and only the mock fixed.

Pinned source shows that the community loaded file can appear before azstem's
second empty-deck sample. A startup-only helper therefore waits under one
10-second acceptance deadline for an absent marker, a structurally valid old
PID marker, or a current fresh unknown marker. Same process/start identity is
required throughout. Current loaded decks, malformed/future/current-stale
markers, transport errors and deadline expiry refuse startup. Acceptance uses
the unchanged exact current-PID/fresh `EEEE` predicate. Its **74 checks, 58
rejections and two strict controls** pass with an independently identical
result. This neither relaxes action preflight nor bounds a blocked underlying
transport's wall time. The startup guard is never removed or reset.

## Actual read-only refusal and closure

A fresh localhost baseline contained **147 regular files and ten loaded mods**.
The observer was absent; both original cores and the same empty process were
verified. The full-frame compatibility check then rejected the host's saved
mixer JSON: **54 distinct offsets exceed the reviewed 28-offset policy**. The
failure occurred before its first controller acquisition command. The exact
settings and failure are retained privately.

Of the 26 extra offsets, 22 equal the pinned neutral template and four differ.
A separate local preview-source audit labels the differences as crossfader and
channel trim/EQ packed low bits, plus the FX TIME encoder counter. These labels
are static source interpretations, not native observations or proof of the
settings' origin. Stripping the extra fields would alter existing controller
state. No field was discarded, rewritten or normalized, and the frozen policy
was not broadened.

A final read-only comparison verified all baseline file bytes, modes and
inventories; loaded mods; original cores; caches; expected absences; the same
empty process; and an absent startup guard. Host counter bytes, mode, inode and
mtime remained unchanged, and the mixer bytes still matched the retained
refused input. Snapshot-only host recovery closed its local receipts without
writing the counter. There was no guest restoration or restart to perform.
The emulator was released, and this run's receipts cannot be reused for input.

## Remaining limits and next step

Python `finally` cannot deliver neutral after process death or `SIGKILL`.
A persisted intent cannot distinguish an invocation not yet made from uncertain
delivery. Kernel controller overlays can remain held; restarting EP147 does
not prove that overlay was cleared. Guest-file and host-counter restoration do
not prove controller or view restoration. No automatic replay/reset is added.
Any separate crash-recovery input contract still needs ownership and review.

Next design and test a separately versioned complete-overlay policy that
explicitly preserves every supported packed analog field and independent
counter across baseline, first frame and release. Account for all persisted
fields and reject unknown controls; do not silently drop entries or reset
values to idle. Keep this failed attempt immutable. Only after the new policy
and its actual-source interactions pass should a new run token, fresh baseline
and source review permit another bounded compatibility attempt. Native task
provenance, arguments/returns and event ownership remain unobserved by this
adapter. MODS0.1.3 stays unpublished; stems staging/activation, USB activation
and physical timing gates remain open.

# Four native encoder records from one bounded emulator run

On October 22, 2026, one fresh localhost emulator installation of
`encoder-route-burst-observer` 0.0.1 accepted the unchanged SDK claim at
`0x1b01cf8`. Four detents on an empty Waveform screen produced **four complete
off-thread records**, filling reservation slots 1–4. The prior one-shot observer
and its evidence remain byte-for-byte unchanged. This run changes the capture
capacity; it does not add native calls, input suppression or a core patch.

The binary was `be7d3da6131f900fd0d3df6fc9fa882db3d667ae0a4a99d013b86250813e4be0`,
built from observer source
`e59eeadda2497467e5b01dac02d17d1932964878a85d58cc4f035c2ca9567e80`.
The exact firmware SHA-256 was
`6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
[Sanitized result](runtime-result.json), [host results](host-result.json),
[binding](binding-result.json), [build metadata](build-result.json) and the
[pre-run plan](RUNTIME-PLAN.md) scope the evidence. No physical access or package
publication occurred.

## Fresh baseline and input

A fresh backup verified **147 regular files**, modes, inventories, current loaded
mods/settings, both original cores, caches and prior absent paths. The observer
used a distinct ID and explicit emulator-only opt-in. Readiness required its
exact loaded row, a positive ready message and the core's one-hook loaded message.
It did not use a module-ID substring as readiness. No claim or input was retried.

Before input, both initial and pre-alignment samples reported native attempts and
reservations zero, state/live one, and one consumed RAW record with byte 34 equal
to 128. The saved host-driver counter was 133. The reviewed helper preserved its
exact bytes/mode and aligned that host file to 128 without sending a guest frame.
Two native BROWSE pulses then reached a visually inspected four-deck Waveform
screen with every deck empty. The same process received two clockwise detents,
a status sample, two counterclockwise detents and a final sample. Screenshots
remained on empty Waveform; no LOAD, playback or menu action occurred.

## What the copies show

| Bounded input phase | Reservation slots | `w3` bits | `w4` values | Changed RAW byte 34 values |
|---|---|---|---|---|
| Two clockwise detents | 1, 2 | `00000001`, `00000001` | 1, 2 | 129, 130 |
| Two counterclockwise detents | 3, 4 | `ffffffff`, `ffffffff` | 1, 0 | 129, 128 |

Interpreting `w3` as signed 32-bit gives +1, +1, −1, −1. For this run the four
`w4` values numerically match the changed RAW byte 34 values minus the starting
128. That is a bounded observed relation, not proof of the firmware's general
counter semantics or of unchanged final native delivery. RAW copies precede
core edits; these lanes do not share an event token or a coherent clock. The
table groups records by action receipts, not by a proved one-to-one native event
identity. Reservation IDs and worker print order are not global event order.

All four records have the same copied `x0`, `x1`, full-width `x2`, original LR
`0x22c6e08` and Linux TID. Every `x2` has nonzero upper 32 bits. The recorded TID
differs from PID, so the observer selects its off-thread lane; this does not
establish the firmware's owner-thread predicate. Bit equality is not a live
object type, retained lifetime or active-child identity. The original LR is
consistent with the separately verified [notification tail route](../route-observer/caller-review/),
without identifying the live source vtable or selecting its upstream caller.
Opaque values and thread/process IDs remain private.

Nine changed RAW records were logged: byte 34 is 128 for the initial record and
four BROWSE press/release records, then 129, 130, 129, 128. Native attempts and
reservations progress 0 → 2 → 4 across the before-knob and two post-input
samples. There are four logged native records and no main-ring record. This
removes the earlier intentional three-record sampling gap for this run only.
No native return, child execution, queue admission, completion or cancellation
was observed.

## Diagnostics and independent validation

Five status samples read **27 allowlisted own-module scalars each**, totaling
135 retained scalar reads. Each sample validated the exact local/loaded binary,
ELF symbol widths, current mappings and auxiliary-vector page size, then
revalidated the mapping plan and process identity afterward. The slot array,
queue payloads and all native argument objects were excluded. Original maps,
auxiliary vectors and scalar bytes are private. The unchanged mapping planner is
pinned by hash to the parent's reviewed implementation.

The sampled native/RAW loss, invalid-input, nested, TID-failure and state/live-gate
counters were zero; state/live were one in each sample and heartbeat advanced. Native counts
and claimed slots reached four. These are sequential, noncoherent reads: in
several samples the later `raw_heartbeat` exceeds the earlier `raw_attempted`
value. The evidence does not turn those reads into a simultaneous snapshot or
prove every intervening state transition. The final retained pre-removal log
still contains exactly four native and nine RAW records.

Before runtime, 32,339 actual-source assertions across 37 scenarios passed per
plain, ASan/UBSan and TSan mode; six strict controls each failed exactly the
intended assertion with no sanitizer diagnostics. An independent complete run
produced identical metadata. Binding passed 47 checks and 30 rejection controls,
also independently reproduced. ARM64 build/SDK validation passed. Two offline
reviews of the actual new binary each passed 28 layout checks covering 25
four-byte and two eight-byte scalar OBJECTs with fabricated maps. Private sampler
mocks passed 297 checks across nine scenarios, and host-counter mocks passed 82
assertions, independently reviewed. All 30 frozen parent files remain unchanged.
The identical prior assembly fixture is reused by hash, not claimed as a new run.

An independent offline audit passed **758 checks** over the saved receipts,
recomputed before/after mapping plans, all 135 scalar files (580 bytes), native/RAW
records and baseline archive members. It checked restoration receipt consistency
without rereading the guest or independently interpreting screenshots. Its initial
assertion incorrectly assumed a newline from the synthetic host-counter fixture;
that reviewer-only failure is retained, and the corrected check binds exact bytes
and mode to the original alignment receipt. This was not a probe/runtime failure.

The candidate actual-source host suite, ARM64 build and emulator run completed
without an observed failure. During preparation, the
new static verifier's weak-CAS rejection control initially reached a missing-
substring error and was corrected to discriminate its intended invariant; this
was not a candidate-source failure. The private binary-layout helper's copied
26-scalar expected count was corrected to 27 before its first execution. Earlier
experiments' failures remain in their original evidence.

## Restoration and limits

Selective removal restored the fresh baseline's bytes, modes and inventory for
all 147 regular files, current settings/loaded set, both cores, caches, prior
absences and shared logging flag. The observer is unmapped and its opt-in absent
after a clean empty restart. The startup guard expired naturally. The host
counter returned to its original 133 with exact bytes/mode, without guest input.
Separate guest and host receipts verify recovery; the emulator is released.
No old backup was restored over newer development.

Sixteen slots remain a finite sampling capacity. Consumed slots are never reused;
CONSUMED means copied, not logged. Unrelated early entries, contention, logging
limits or producers that finish after capture stops can leave incomplete evidence.
No hardware timing, first-use TLS cost, module shutdown quiescence or whole-engine
lifetime/race proof follows from this run.

Next audit a passive boundary that can distinguish native queue admission from
execution and associate value arguments with an owned event lifetime. Preserve
native checks, input and exclusive hook ownership. Prove its full ABI and policy
eligibility before any new hook, and keep missing boundaries UNKNOWN. Do not
repeat this successful four-detent capture unchanged or use the observed pointers
later. A core-owned menu, focus handback and view cancellation still need that
ownership contract; the stems staging runtime gate remains closed.

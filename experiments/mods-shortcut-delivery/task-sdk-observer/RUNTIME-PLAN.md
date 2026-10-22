# Fresh emulator observation plan

**Current gate:** the [coordinator checkpoint](COORDINATOR-EVIDENCE.md) passes
offline composition tests, but the latest fresh compatibility attempt refused
54 saved mixer offsets against the frozen 28-offset policy, before acquisition.
Four extra offsets differ from idle; dropping them would change state. That
read-only run is closed, all 147 fresh baseline files and host state were
verified unchanged, and no observer/input/restart occurred. Review a separate
complete-overlay preservation contract before using a new run token and fresh
baseline. Do not normalize settings, reuse this attempt or repeat old cleanup.

This is an offline plan for one bounded localhost-only observation of
`encoder-task-observer`. It is not runtime evidence or permission to reuse an
old backup. The candidate has not yet been installed. The earlier
[route-burst result](../route-observer-burst/runtime-evidence.md) remains a
separate completed experiment.

The frozen [build](build-result.json) identifies binary SHA-256
`71d7fc088f4def67d1307e4faeef301020aed04bfa3da7ae4bc6a422ece3e255` and observer
source `538574af5f357131e0fa9ec6e26d54cfef0ec2a57f31baddff1e8e5bc7c134ed`.
The [binding](BINDING.md), [lifecycle](LIFECYCLE.md), compiled-code review and
host results are prerequisites. They do not establish live hook acceptance,
valid task borrowing, callback delivery or physical timing.

## Before changing the emulator

One coordinator owns the session. Confirm the configured SSH and control
endpoints are localhost, preserve the current process identity, and refuse
physical-device endpoints. Use a new private run directory, a fresh nonzero
run token and new ownership receipts. Existing receipts and historical backups
are evidence only. Run-token uniqueness must be checked against retained runs;
a PID or clock alone does not establish it.

Take a fresh baseline of the current emulator, including:

- exact bytes, tar modes, path inventory and symlink targets for all current
  community files, settings, service drop-ins, registration/approval lists and
  shared logging flag;
- both current original cores, loaded-mod set, source and compiled envelope
  caches, and explicit absent paths;
- PID plus process start identity, fresh loaded-file metadata, empty deck state,
  and a screenshot before any input;
- exact host counter bytes and mode, preserved independently of guest recovery.

The last completed run preserved 147 regular files. Record the actual current
count and review any discrepancy; never replace the current contents with the
old 147-file snapshot. Preserve newer files and unrelated modules. Recheck the
baseline inventory, bytes, modes, core identity, loaded set and process before
installation and after stopping. Refuse unowned changes. If the current cores
differ from the recorded originals, stop to reconcile them; never install an
older core to make a hash check pass.

Preflight the existing module limit, a free SDK module slot, exact approval
hash and both native hook conflicts. Reserve capacity in the separate approval
table as well: the pinned core retains 64 distinct approved IDs and evicts the
oldest when adding a new 65th. Refuse a full table; never remove an old approval
to make room for this experiment. The pinned loader has sixteen module
slots; appending a registration line does not prove loading. Reserve every new
module path, service drop-in, owner marker, log path and upload `.new` path,
including the owner-marker temporary. Refuse stale collisions and unrelated
registration tokens. The new drop-in contains only the explicit emulator opt-in
and the fresh run ID. Preserve all unrelated service environment and shared
settings.

The retained fresh-snapshot, archive/inventory, selective-install and recovery
helpers can be adapted into new private scripts with new identities. Their
historical restore actions must never run. The older stems experiment's
installation/loading phases are unsuitable: they replace a core and use a
historical baseline. Only its generic archive, inventory, mode and cache
utilities are reusable here. Adapted scripts need offline mock fault-path
checks before installation, including partial uploads, failed stops, action
failure, daemon reload/start failure, recovery-receipt failure and independent
host-counter recovery.

## Input-counter and sampler gates

The prior host-counter aligner requires that observer's RAW sequence and
byte-34 log. This candidate has no RAW lane; renaming that helper would be
invalid. The saved host counter and the idle template are not evidence of the
current emulator controller value. Every mixer frame, including BROWSE press
and release, carries this counter and can create a turn if it is stale.

Before any mixer input, independently establish a fresh counter from an
emulator-owned interface whose exact source, loaded implementation identity,
read bounds and side effects have been reviewed. The virtual controller's
SPI3 frame read now has a [separate audit and bounded diagnostic](PREPARATION-EVIDENCE.md);
its later [full-frame action preparation](ACTION-PREPARATION.md) adds a pure
152-byte policy and private tested components. The [coordinator](COORDINATOR-EVIDENCE.md)
is now composed and fault-tested offline, but current saved settings fail its
compatibility policy before acquisition and no enabling review manifest exists.
The control-device LED read is unsuitable because it consumes a different freshness stream. Do not
read native task, dispatcher, child or firmware object memory to obtain the
counter. If the owned-frame route cannot be proved and freshly sampled, stop
before all mixer input; an unknown-delta frame is not a substitute for this
precondition.

If that route passes, require two exact 152-byte acquisitions with valid prefix
CRCs, repeated consistent counter values, and equality to the planned neutral
frame at every other position except the two CRC bytes. Freeze and validate
the host mixer settings too. The kernel placeholder must not be silently
normalized. Require the same process and no intervening external input. Align
only the host driver's counter file, retaining its exact original bytes/mode
and ownership receipt. Validate the pinned input driver and the bounded target
range; avoid wrap or boundary normalization. This establishes the controller
frame offered by that interface, not that firmware consumed it unchanged.
Do not claim a RAW/native correlation that this candidate cannot measure.

Validate a new own-module sampler against the exact ELF and its eight scalar
symbols: `state` and `worker_exit_reason` are four bytes each; `heartbeat`,
`attempted`, `state_gated`, `caller_gated`, `tid_failed` and `worker_cycles` are
eight bytes each. Bind exact symbol names, sizes, ELF load segments and actual
page size from auxv. Reuse the frozen map planner only at its recorded hash,
with fresh actual-binary and allowlist checks. Retain each read's exact bytes,
width, maps and process identity before/after. Reject mapping or identity drift.

The sampler must not follow the published transport pointer, original slots
or native pointers, and must not read task records or private transport storage.
Sampling identity must work in Ready, Closed and Failed states, so that failure
evidence and recovery do not depend on passing the input-readiness predicate.
These sequential scalar reads are not coherent snapshots. There is no exposed
transport reservation, publication or loss counter among these eight symbols;
quiet logs and sampled helper counts do not prove available slots or no drops.

## Startup and exact readiness

Complete backups, offline checks, plan review and host recovery preparation
before starting the candidate. The pinned core normally clears its startup
marker after 90 seconds. This observer closes after 120 eligible worker polls,
nominally about 60 seconds with its 500 ms sleeps. Waiting for the marker to
expire before observing would normally consume the capture opportunity.

For the initial start, wait for a fresh loaded file and a new, stable
PID/start identity, then check exact loading and own-module progress while the
startup marker remains untouched. In the inspected core the marker supports
startup/restart quarantine accounting; its presence is not an input-readiness
predicate. It must still expire naturally before any planned cleanup stop.
Do not delete it, shorten its interval or repeatedly restart for another
capture window. Remote file tests must distinguish absent from transport or
command failure; a generic nonzero exit is not proof of absence.

Before the first input, require all of:

1. The exact module ID/version is loaded, with the intended binary hash and
   complete expected mapping. Inspect the actual fresh loaded row, not an ID
   substring or a stale file from the previous process.
2. Exact per-module core evidence reports each target claim successful
   (`0x237a440` then `0x1b029c0`), exactly once, plus two loaded hooks, no refusal or
   init failure, and the configured opt-in/run token match this session.
   The two static target addresses and expected prologues remain the bound
   ones. Do not infer two claims from the initialization text alone.
3. Two same-process, validated own-module samples show `state=2` (Ready),
   `worker_exit_reason=0`, and advancing heartbeat and worker cycles. Check
   current empty decks and the fresh owned counter baseline as well. Obtain
   the pair inside the action preflight; saved progress receipts without a
   verified age/run binding do not authorize a later input.
4. The new log and receipts belong to this run. Any observed entry/return
   record must carry its exact run token; stale or mixed-run records fail the
   record analysis.

These checks establish observed loading and recent progress. They do not prove
that capture stayed open at every intervening instant or that a future task
pointer will be valid. The initialization event explicitly says capture status
is separate and can race closure. A nonzero original or a two-hook count alone
also does not prove target execution.

| Observed condition | Meaning and action |
|---|---|
| Never/Installing (`0/1`) or Failed (`4`) | No input readiness. Preserve diagnostics. Partial first-hook forwarding may remain live; use selective restart cleanup. |
| Ready (`2`), no recorded exit, advancing worker and tick | Eligible to attempt the preplanned bounded input, subject to all other gates. |
| Closed (`3`), no recorded exit, advancing worker | New capture is permanently closed; scanning may still publish/log earlier entries or returns. Stop new input and collect a bounded tail. |
| Exit reason `1/2/3` | Worker recorded init failure, sleep failure or missing storage. Stop new input; preserved slots or later after calls need not yield logs. |
| Exit `0` without advancing cycles | Absence of an exit record is not proof of a running worker. Refuse new input. |

A closed gate is neither core quarantine nor drained callbacks, task completion
or module-unload safety. Five quiet polls, capacity and poll budget can all
close it. The exposed scalars do not identify which condition won. No rearm,
slot reset, original-slot clearing, hot unhook or recovery by watchdog reset is
part of this plan.

## Bounded input and value interpretation

Prefer the already visible empty Waveform view. If navigation is necessary,
permit only the pre-reviewed bounded BROWSE sequence, with host alignment and
current readiness established before its first frame. Verify the resulting
empty Waveform screenshot before knob input. Do not issue SOURCE/menu actions,
load tracks, play decks or reuse old screenshot assumptions. All sent frames
and host-file transitions need unique intent/result receipts; failed commands
must not automatically be repeated.

Plan at most one clockwise step followed by one counterclockwise step, each
with an explicit host target, while preserving native dispatch. The prior
counter-ownership helper only accepts two-detent blocks; it must be narrowly
adapted and mock-tested for these one-step targets before use. Record both
intermediate host ownership and the final release frame. Recheck same process,
empty view and observed Ready/progress between steps. If a gate fails or capture
closes, stop the sequence; do not add inputs to fill missing records. A smaller
sequence may still exhaust capacity through unrelated qualifying calls or
rejections, and the status sampler cannot certify spare slots.

After the bounded sequence, collect a fixed short tail of status/log samples
in the same process without more input. Observe Closed separately if it occurs;
never extend or reset capture to make the expected records appear. Public
analysis must distinguish:

- a copied submission entry (phase 1, copied status) at its exact caller filter;
- a copied executor entry (phase 2, copied status), which is evidence of entry
  into that body, not successful child handling or retained child lifetime;
- a submission normal-return record linked only to its own run/invocation ID
  and full 64-bit result. Its relation to the pre-copy TaskNo is a value
  comparison, not acceptance, cancellation or completion.

Submission and execution use different observer invocation IDs. Equal TaskNo,
ID, delta and cumulative bits permit only a possible value match, never an
automatic event identity. Zero, wrap-like values and duplicate tuples remain
ambiguous. Execution may precede submission return. Worker print order and slot
reservation order are not native execution order. The action receipt records
what the harness attempted; without RAW data it does not prove downstream
counter editing or causality for every observed task. Preserve all unexpected
patterns instead of assigning the expected sign or ordering.

Missing entries/returns remain UNKNOWN: caller filtering, capacity/contention,
invalid capture, closure, exceptions and the void logging path can all limit
observations. Diagnostic counters can wrap and sampled fields are noncoherent.
No logging success, accepted/queued classification or cancellation conclusion
follows from a missing line. Keep raw thread identifiers, scalar payloads,
addresses, screenshots and process details private; publish bounded sanitized
relations and exact binary/source identities.

## Selective cleanup and completion

Cleanup is required after successful, refused or partial claims. Retain any
accepted forwarding path until an ordinary planned process restart. First wait
for the startup marker to clear naturally in the same owned process, with
strict error/absence distinction and a finite diagnostic timeout. If it does
not clear, stop new input and preserve the blocked recovery evidence; never
remove the marker to force a restart.

Revalidate the fresh baseline and all created-path receipts before and after
stopping. Remove only this run's proven module files, owner/temporary paths,
service drop-in, tokens and log. Restore changed lists and shared flags only to
this run's baseline; do not remove a logging flag that pre-existed. Preserve
both original cores, unrelated community files, modes, inventories, loaded set,
settings, cache contents and prior absences. Any unexpected drift blocks an
overwrite or broad cleanup. USB mode allowances, if needed, must be restricted
to verified filesystem behavior for new owned probe files; they cannot relax
baseline mode checks.

Use the reviewed stopped-action recovery pattern: even a stop, mutation or
daemon-reload failure must attempt a clean start; preserve the primary error
and separate recovery errors/receipts. After selective removal, verify the
fresh original loaded set, exact preserved files and cores, absent probe and
both opt-in/run configuration, no probe mapping, and a clean empty restart.
Let its startup marker expire naturally as well.

Restore the original host counter separately, without guest input, checking
exact bytes and mode rather than assuming a trailing newline. Accept only
receipt-proven host values and owned temporary files; refuse unrelated host
drift. Keep guest-restored and host-restored receipts separate so a later host
failure cannot trigger repeated guest mutation. Release session ownership only
after both restoration proofs complete.

A completed report states which claims and value boundaries were actually
observed and which checks failed or remained unknown. This experiment is not a
community release, a direct-menu implementation, a stems fix, or validation of
physical timing, audio quality, native object lifetime, asynchronous unwind or
whole-engine race freedom.

## Action coordinator checkpoint

The [action preparation](ACTION-PREPARATION.md) records the frozen components;
the [subsequent coordinator evidence](COORDINATOR-EVIDENCE.md) records their
tested composition and the unresolved saved-settings compatibility gate. Host counter bytes/mode must be captured before
installation, even if the run never aligns or sends input. Empty-deck checks
must parse the exact four-line publication, match the current PID, and compare
its uptime with a fresh uptime read. An `EEEE` substring is insufficient.
This sampled loaded-track marker does not prove authoritative pause or LOAD
intent for stems recovery.

Each separate BROWSE pulse and each one-detent step needs its own fresh
status/frame/frame/status preflight. At most two BROWSE actions precede one CW and
one CCW action. Preserve a same-process visual Waveform confirmation before
each knob step. Saved receipts or a review-marker file cannot enable input.

Before a first frame, the coordinator must durably retain both exact
planned payloads, consume its fresh permit once, and atomically write only the
owned host target. It must arrange one at-most-once neutral-release attempt
even if the press transport fails with unknown delivery. Release checks retain
same-process and host/mixer ownership but must not depend on continuing Ready
state or permit age. Never retry a press or release, rearm capture, or add more
input to obtain missing records. Any failure stops further input. The composition has
passed actual-function mock fault tests; this does not override the current
full-overlay compatibility refusal or prove runtime delivery. Preserve the
first-call interruption and sticky-overlay limits in the coordinator evidence.

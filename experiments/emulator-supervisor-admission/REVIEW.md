# Supervisor admission source review

This review concerns the inactive source composition. It does not report a build,
test, endpoint or native-runtime execution.

## Existing owners and the access gap

The existing `OwnedStartup` stores the optional supervisor in an
`Arc<Mutex<G>>`. Its private `SupervisorRef<G>` retains that same allocation
through `OwnedLifecycle`; the lifecycle exports a port and local finish/snapshot
operations, not the supervisor. The new specialization for `Child` accesses that
existing allocation in one scope. It does not copy a PID into a new owner or
return a mutable child accessor.

The startup lease and a short products lock select the exact retained lifecycle.
The products lock ends before the child mutex and callback are used. The scoped
adapter permanently binds both caller journals before invoking the callback.
Reused journal, startup/owner absence and failures before resolving a healthy exact
port remain typed refusals without a closure claim. After resolving that port,
missing, busy or poisoned supervisor access records a one-shot `ScopeRefused`
latch and still returns the original error. It neither binds nor polls the child.
The separate latch record preserves whether terminal admission was actually
returned. Current startup snapshots inspect supervisor poison/presence without
locking the child itself.

Holding the supervisor mutex for the callback is intentional. Scope unwinding
can poison it while preserving the actual child allocation; subsequent production
access refuses that poison. A caught inner poll panic need not unwind the mutex
guard, but it still leaves the frozen polling journal armed and terminal-latches
the port before the caller regains control. The existing lifecycle's independent
finish path and all final-destruction limits remain unchanged.

## Why a narrow terminal primitive is needed

The existing `MixerPort::request_stop` first sets the terminal atomic flag and
then acquires mailbox/producer state. Producer closure can wait behind an ACK's
settings callback. `close_generation` also waits and does not permanently stop
the mailbox. Neither full operation belongs in an unwind guard.

The new additive method delegates only to the existing `Shared::stop(Requested)`
and reads its first reason. That path uses compare-exchange plus `try_send` wake,
without mailbox/route/settings locks. It requests the existing worker's stop,
not native or child termination. This supports terminal admission even while a
backend or settings callback is blocked, without claiming bounded system latency.

A source review identified an early draft that silently reused an armed but
unreturned latch record. The revised contract requires refusal before further
polling and no repeated latch call. A nonterminal returned reason also remains
an explicit failure. These are pre-freeze corrections, not passing test results.

## Ordering and honest consequences

Known controller loss closes port admission before invoking the next poll. Newly
observed error/exit closes it after the exact result is retained. The inner unwind
guard handles a poll panic even when the caller catches it; the scope guard closes
on both ordinary return and unwind. The first cause and actual stop reason remain
unchanged across these paths. An unresolved latch is never retried by Drop.

The closure primitive does not cancel an already reserved/dispatched request or
suppress its late ACK/settings. New integration checks must distinguish terminal
port admission from producer closure and joined completion. In particular, a
blocked backend is already dispatched; it cannot stand in for a deterministic
reservation-before-dispatch test. A late successful settings write is compatible
with the atomic latch contract until a separate producer close completes.

Scope cleanup retains the original resource graph and calls no child lifecycle
operation. Actual child liveness, retained stdin and explicit private fixture
cleanup must be checked separately from synthetic backend behavior. No implicit
native stop, guest service, main integration, writer exclusion, restoration or
recovery follows from this source review.

## Preparation and runner review

Preparation retains the frozen startup tree and captured inputs, changes only
main module declarations plus the additive startup/transport code, and binds the
complete generated inventory. The reservation fixture edits are all `cfg(test)`;
the production worker's existing terminal check and dispatch order are unchanged.
Parent suites are compiled/listed but are not selected by this new runner.

Each strict control must identify its own exact final assertion anchor within
the selected function; a single hardcoded assertion shape would misclassify the
different compound oracles. The runner also requires its expected message,
source location, failed-test count and exit status, and rejects fixture aborts.
A compiler failure or earlier unexpected panic is not a passing control.

The command wrapper starts a fresh private process group. Any communication
exception attempts group termination and bounded drain, even if initial timeout
log capture fails. Cleanup diagnostics are separate and the original failure is
preserved. None of these exceptional paths can count as passing execution.
The frozen parent preparers can still leave partial private output before the
outer preparer receives a returned directory; a missing verified receipt is not
success. No such failure or successful matrix is claimed by this source review.

Focused execution and the retained audit correction are recorded separately in
[HOST-TESTS.md](HOST-TESTS.md); this review's source-only claims remain bounded.

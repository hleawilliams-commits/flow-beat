# Scoped supervisor observation and local admission

This inactive successor connects the retained observation journal to the actual
mixer port owned by the existing startup/lifecycle graph. It adds no new resource
custody owner, backend factory, guest process, controller reader thread or main
selection. The frozen parent sources remain separate; preparation applies the
reviewed additive source snippets to a fresh private candidate.

## Borrow the retained child

`StartupHandle<Child>::with_supervisor_observation` takes caller-owned polling
and admission journals and a scoped callback. It uses the existing startup
operation lease, requires a ready startup with its matching lifecycle owner,
and borrows the actual child under its existing supervisor mutex. The callback
receives only the narrow observation adapter and immutable journal access.
There is no production raw child, supervisor mutex or resource extraction API.

Both journals bind once for their lifetime. The polling observer remains bound
for the whole callback, allowing multiple explicit observations without PID-based
rebinding. Fresh journals describe a separate later scope; they cannot reopen a
port terminally latched by an earlier scope. Missing, busy, poisoned, unready or
already-bound inputs refuse the scope without invoking its callback. Before a
healthy exact lifecycle owner/port is resolved, refusal makes no closure claim.
Once that port is available, missing, busy or poisoned supervisor access consumes
the admission journal and attempts one terminal latch with `ScopeRefused` and the
original typed error. The polling journal stays unbound and no child poll occurs.
The original error is still returned; only a retained returned reason other
than `StopReason::None` establishes local terminal admission. Poison is not cleared for operation.

The lease excludes startup and `StartupHandle::finish`; it does not serialize an
already-returned `LifecycleControl::finish` invoked by another caller. The child
mutex remains held for the callback, so this is not a deadline or general callback
reentrancy guarantee. Normal return releases only borrows and the operation lease;
the existing startup/lifecycle owners still retain the child, stdin, backend and
worker/session. Their final Drop and process-death limits remain unchanged.

## Latch before losing observation

The additive `MixerPort::latch_terminal_admission` exposes the existing terminal
atomic stop and nonblocking wake. It takes no mailbox, route or settings lock.
It preserves the first actual stop reason, which may predate this scope. It can
cause the existing worker to exit; it neither joins that worker nor signals,
waits for, closes stdin on or disposes the actual child.

The adapter records the first closure cause and arms its single latch attempt
before calling that primitive, then records the actual returned `StopReason`.
An armed attempt without a return is unresolved and is never replayed. A returned
`None` stop reason is retained as a failure to establish terminal admission,
not treated as success. No later cause or observation replaces the first record.

A supplied controller loss or an already-Hold polling journal latches admission
before polling. A newly returned poll error or child exit is first retained by
the frozen observer, then causes the latch. A poll unwind guard latches before
the panic escapes, including when the caller catches it inside the scope.

Every scope end also latches: normal return records `ScopeEnd` if no earlier
cause exists, while an unwind records `ScopeUnwind`. Ending the one-time binding
must not leave its port open without an observer. These guards call only the
atomic latch; they perform no blocking `request_stop`, join or child cleanup.
A normal scope return can therefore leave the polling disposition `Observing`
while port admission is terminal. The two journals describe different facts.
After the callback completes, an unresolved or nonterminal latch return causes a
typed scope error; callback completion alone is not successful closure.

## Queued work and late outcomes

Terminal port admission applies to all clones of that exact port and prevents
fresh refresh/adoption/enqueue admission. It is not immediate producer closure,
queue disposal, native cancellation or proof of worker exit. A queued envelope
remains accounted for until the existing transport clears it. A request already
past the worker's terminal check can still reach the backend. Its positive ACK
and settings callback may complete before the producer gate closes.

Callers retain the existing explicit `request_stop` and lifecycle `finish` APIs
for their separately scoped work. Those operations may block behind callbacks;
a successful atomic latch must not be reported as their successful completion.
Neither the latch nor a real local join authorizes guest termination, native
release, restoration or restart.

## Evidence limits

The polling journal retains actual returned errors/status and bounded first/latest
records. The admission journal retains one cause and one latch return. They are
in-memory evidence, not complete logs or durable recovery. Caller-supplied events
remain unauthenticated values. Allocation abort, process death and nonlocal exit
can defeat publication or cleanup; no latency or native lifetime guarantee follows.

The test-only child accessor and injected polling callback exist solely for
private fixture inspection and cleanup. They are absent from production builds.
No emulator endpoint, physical deck, installation, settings, package, catalog or
release action is performed by this source slice. Existing runtime/product gates
remain closed. Validation evidence is recorded separately in [HOST-TESTS.md](HOST-TESTS.md)
and does not expand this source contract.

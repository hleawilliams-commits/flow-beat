# Retained native construction graph

This inactive successor composes the existing [CLAIM owner](../emulator-claim-owner/),
[retained adapter](../emulator-retained-adapter/),
[retained broker](../emulator-retained-broker/) and
[C dispatcher](../emulator-broker-wire/). The graph keeps each acquired child
before starting the next operation. A failed construction leaves its existing
children and diagnostic evidence available instead of implicitly closing them.

The first graph accepts injected prestates only. Idle prestates are rejected
before descriptor transfer; no FRAME converts idle acquisition into injection.
The standalone CLAIM owner still supports its separately tested idle path.

Creation copies the plan without taking a descriptor or performing I/O. The
caller retains the returned graph before transfer. Transfer stores a CLAIM
child before consuming the caller's original fd slot. One start attempt runs
preflight, CLAIM, postflight, duplicate attachment, validation, broker setup and
dispatcher construction in order. Failure stops the sequence and keeps prior
custody. Ready allows exchange only through the graph's actual dispatcher;
it is not proof of exclusive native ownership or an authenticated peer.

No raw child owner, backend, broker or dispatcher getter is supplied. Diagnostic
copies omit callable backend pointers, but descriptor numbers remain usable
in-process and are diagnostic only. External serialization, no state-changing
reentry and exclusion of all aliases remain caller obligations. Synchronous
fixture inspection during a callback does not establish concurrent access safety.

## Explicit local disposal

Disposal requires independently established quiescence and resolution/accounting
for native operation uncertainty and ownership. These requirements are external;
a Ready flag, matching snapshot or completed API call cannot establish them.

The graph closes later exchanges before retiring the dispatcher, then tries the
retained broker. If broker destruction fails, it preserves the adapter and
original descriptor dependencies. If the mutex was destroyed but its attribute
remains, the broker is permanently unusable even though its owner remains.

Adapter and original disposers each consume their child and make at most one
close call. Raw close reports and prior child evidence remain in the graph.
An uncertain duplicate close stops before original close. A separately justified
later disposal skips the consumed child and never retries its old fd number.
An uncertain original close also retains graph evidence. Known-success disposal
consumes the graph. Any uncertain close keeps its shell, including after a later call
retires the remaining children. That shell requires separate explicit finalization
after external accounting; finalization frees evidence rather than proving recovery.

This graph adds dependency ordering and observable local outcomes. It supplies
no native RELEASE/RESTORE, authoritative original baseline, stop proof,
deadline/cancellation, both-file restoration or process-death recovery.

## Evidence and boundaries

[Host evidence](HOST-TESTS.md) states the exact tested construction and disposal
paths, strict controls, independent run and retained-file audit. Child source and
operation bodies are pinned and mechanically reused without policy changes.
Private builds, patched source exports, raw logs and audit scripts stay ignored
under private/. No existing parent suite is rerun unchanged.

The separate [ARM64 build record](ARM64-BUILD.md) covers eight production
compiles and two links from the same objects, with exactly seven graph exports in
the candidate. A four-line whitespace-only wire copy fixes the retained initial
GCC warning failure without weakening strict flags; a separate tooling preflight
failure is also preserved. This is pinned-musl compile/link evidence only. No
produced target artifact was loaded or executed, and guest/firmware ABI,
transport and supervisor integration remain unproven.

This is an offline source checkpoint. No endpoint is opened or authenticated;
host transport/supervisor binding and live libc/VFS behavior remain unimplemented
or untested here. No emulator restart, physical deck control, core change,
installation, package/catalog update or release follows. Main and all prior
observer, stems, USB, recovery, lifetime/race, audio-quality and timing gates
remain closed.

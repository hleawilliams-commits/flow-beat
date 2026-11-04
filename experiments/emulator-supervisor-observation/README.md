# Retained supervisor observations

This inactive Rust module observes a caller-owned child without acquiring or
releasing that resource. A scoped `ChildObservation` exclusively borrows the
actual `Child` and its caller-retained journal. Each explicit observation calls
`Child::try_wait` at most once and records the full returned value before
classifying it. It never calls blocking wait, signals a process, closes child
stdin, restarts a process or disposes a guest graph.

The journal is bound once for its lifetime. Its PID is diagnostic only; equality
of PID values never permits reuse. Ending the observation scope releases the
borrows, while the caller still owns both child and journal. Final caller Drop,
process death and external lifetime policy remain separate.

Controller EOF, returned reader error, reporter disconnection, poll error and
observed child exit produce a local `Hold` disposition. Loss/error history does
not reopen after a later successful poll. `Hold` changes no real route admission
and conveys no native stop, cleanup or recovery authority. A poll that unwinds
before returning remains armed and cannot be replayed; an observed exit also
refuses later polling.

The separate one-shot reader helper calls actual `io::copy` into `io::sink` and
retains its exact `Result`. Successful completion records EOF and the returned
byte count. An error preserves its full error object but provides no known
partial-byte count. The helper can block indefinitely and can perform multiple
reads or internal retries; it has no single-read or latency guarantee.

The candidate adds only an inactive module declaration beside the pinned boot
code. The legacy reader still collapses EOF/read error into stop, and reporter
disconnection also stops the child. A direct try_wait error returns status 1;
wait_exit still treats a polling error as completion. These branches are unchanged. This module is not connected to `OwnedStartup`, App/main, a controller
channel, guest service or production stop policy. The motivating concrete source
boundaries are in [GUEST-SERVICE-REVIEW](../emulator-guest-stream/GUEST-SERVICE-REVIEW.md).

Two fresh runs passed eight focused selectors and four strict controls each.
The first also passed the pinned actual-crate production check; the independent
run repeated only the focused checks. [HOST-TESTS](HOST-TESTS.md) and
[result.json](result.json) record the evidence and its limits. [REVIEW](REVIEW.md)
records the separate source reasoning. No native endpoint, running emulator,
physical deck, installation or release is exercised by this source slice.
Existing runtime and product gates remain closed.

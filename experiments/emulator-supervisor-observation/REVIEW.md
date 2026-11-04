# Supervisor observation source review

The source review below was completed before matrix execution. Subsequent test
and build evidence is recorded separately in [HOST-TESTS](HOST-TESTS.md) and
[result.json](result.json).

## Actual child binding

`ChildObservation` owns neither child nor journal. Its exclusive borrows remain
live for the entire binding, so safe callers cannot replace the child while
continuing the same observation scope. `bind` refuses a journal that has ever
been bound, even after the previous borrow ends. The stored `Child::id` is only a
diagnostic value and is never used as a substitute for an owned process handle
or as a rebinding/signal decision. A new journal is a separate observation
history, not resumed evidence from the previous binding.

The journal has no mutable public accessor. Copies of loss/poll records cannot
mutate its binding or transition state. Dropping the scoped adapter simply ends
its borrows; ordinary caller ownership remains. This neither extends custody
after final caller Drop nor protects resources across process death.

## Observation order and limits

The operation refuses a previous armed-unreturned poll, an already observed
exit, or counter exhaustion before replacing the prior event/record. It records
the next attempt and supplied controller event before the one `try_wait` call.
The full actual returned `Result<Option<ExitStatus>, io::Error>` is retained in
an immutable `Arc` before classification. The first poll-error record shares
that same error object rather than flattening it into an errno or text message.
The Arcs retain receipt values, never child resources.

First controller loss and first polling error remain available independently of
the latest poll. Returned running status after loss/error cannot clear the Hold
latch. An armed-unreturned poll also derives Hold immediately after a caught
panic, without waiting for another API call. Subsequent observations refuse that
unresolved attempt. An actual exit status latches Hold and prevents further
polling, but is not a guest-disposal receipt. `try_wait` can reap an exited child;
this module calls no blocking wait.

The reader helper arms its separate journal before actual `io::copy` and stores
the returned `Result` before constructing an EOF/error event. Its returned error
has no partial-count claim because the API supplies none. A reader panic leaves
attempted/unreturned evidence and refuses a second copy. The reader and its
journal remain caller-owned, but caller-provided `Read` behavior is not bounded
or sandboxed. The helper does not send a channel event or install a reader
thread. Typed controller events supplied to the observer remain caller evidence,
not an authenticated communication or native authority.

No source defect was found in binding, refusal order, retained outcome identity
or monotonic Hold behavior. Allocation abort and nonlocal termination can still
prevent result publication; this is bounded in-memory evidence, not a durable
journal. The first/latest records are not a complete event log.

## Focused fixture review

The eight new selectors were reviewed before execution. The fixture stores an
actual private sentinel Child before reading READY. Its normal cleanup sends an
explicit fixture command, observes the expected exit code 23 and calls actual
wait before the final semantic assertion. Emergency kill/wait is an infrastructure
failure path, not a successful oracle or production lifecycle policy.

The reader EOF case uses an anonymous socket and actual io::copy; reporter
disconnection comes from an actual host mpsc receiver after its sender is dropped.
The running/EOF cases also check that stdin remains on the actual Child and the
child stays alive before cleanup. Only the natural-exit selector detaches stdin
into a fixture-owned command handle while the observer borrows Child. Scripted
reader errors/panics and test-only poll errors/panics are labeled separately. The poll
error check downcasts the original custom error payload after a later actual
Running observation; the panic check inspects Hold and the armed record before
attempting a refused replay. Same-child and other-child rebinding are both checked.

The final assertions are after cleanup, including in the planned semantic controls.
Malformed readiness, unexpected process status and fixture deadlines cannot stand
in for those assertions. Counter-overflow behavior is source-reviewed here but is
not exercised by these eight selectors. No fixture result is claimed until its
execution evidence is recorded separately.

## Runner failure accounting

Each compiler/test command runs in a fresh private process group. A timeout
retains available output before group termination and fails the invocation; it
cannot satisfy a strict control. The runner writes its started receipt as soon
as the preparer returns, before additional input copies or compiler-version calls.

One preparation bookkeeping limit remains: an exception after the preparer creates
its fresh private directory but before it returns may leave partial files without
a local failed-status marker. The directory is retained and the exception fails
the caller. A verified preparation receipt and a passed run receipt are required
for success; absence of either is not a passing attempt. This is a tooling-record
limit, not evidence of runtime custody or a reason to relax a test gate.

## Frozen integration boundary

Pinned upstream `az-preview/src/boot.rs` has SHA-256
`f907da478aa758d4cb2a9f740f529fbe256376230d77635a061a746ce75ec1eb`.
Its legacy reader discards the `io::copy` result; EOF or reporter loss reaches
`stop_child`, and `wait_exit` treats a polling error as completion. The candidate
module declaration makes the new source available to compilation without
changing or selecting any of those executable branches. There is no composition
with the retained `OwnedStartup` or a real route-closing operation in this slice.

Therefore local Hold must not be described as stopping submissions, retaining a
guest after host death, fixing the active supervisor or authorizing termination.
The existing supervisor, guest forwarder's reopen/restart behavior and separate
virtio protocol requirement remain in the linked
[guest service review](../emulator-guest-stream/GUEST-SERVICE-REVIEW.md).

Test-only injected polling outcomes can validate error/panic bookkeeping, not
prove a real OS polling failure. Private child fixtures, when executed, must
complete their own cleanup and wait before final semantic oracles. They cannot
supply native teardown authority. No old passing suite, runtime endpoint,
installation or physical-device operation is part of this source review.

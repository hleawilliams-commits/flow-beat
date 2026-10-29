# Startup custody and host/guest boundary review

The previous fixed-session owner retains route/backend/settings custody but does
not retain a worker or supervisor. The actual transport starts a worker before
returning its pair. Dropping `MixerWorker` requests stop and drops its join handle;
it does not join. An unwind between receiving that pair and publishing an
`OwnedLifecycle` can therefore discard the only join handle. A supplied
supervisor held only in the startup caller can also be dropped on error.

This successor retains those actual resources before later startup boundaries,
hides the raw route and worker, and uses the frozen lifecycle for explicit stop
and join. Retention does not itself establish completed startup, native outcome,
guest liveness or safe release.

## Native backend is across a process boundary

The existing [native adapter](../emulator-native64-adapter/adapter.h) requires an
already-CLAIMed stable controller descriptor. Its
[native wrappers](../emulator-native64-adapter/native.c) require native64 Linux.
The current preview runs on the host and controls the Linux emulator guest
through the existing socket path. The
[owned-exit platform review](../emulator-owned-exit/PLATFORM-LIMITS.md) describes
that host supervisor and guest boundary.

A direct C/Rust semantic-struct cast or another synthetic backend cannot bridge
the host to the guest's kernel descriptor. A later native integration needs a
guest-resident broker and validated command/receipt transport, or an explicitly
different same-process placement. Binding and lifetime checks at a C/Rust ABI
would establish only that boundary's properties; they would not establish
acquisition, authentication or cross-process writer exclusion.

The [RESTORE broker](../emulator-broker-restore/broker.h) also distinguishes API
success from COMMITTED and permits cached historical duplicate receipts. A
future route backend must validate complete request/result identity and preserve
unknown outcomes rather than turn a successful function return into an ACK.

## Constructor scope

The component calls the existing fixed-session, transport and lifecycle
production implementations. It adds no guest launcher, endpoint, native adapter,
main wiring, recovery action or community release. The private candidate changes
the parent main only by an inactive Unix module declaration, and appends a
`cfg(test)` transport helper that invokes its existing spawn-error branch.
The helper is a deterministic injected return, not an actual OS spawn failure.

The exact returned pair must be stored before any later hook. Pair transfer into
the frozen lifecycle runs under a short products lock with no user callback or
hook. ChannelArmed without a returned pair remains StartupUnproven; this layer
cannot reconstruct a handle that an interior channel unwind never returned.
Allocation abort and process death remain outside the returning/unwinding cases.

An external finish racing active start returns InProgress without cancelling it.
A later explicit finish is required. Poison can refuse operational access while
resources remain retained and unjoined. Snapshot evidence is separately copied,
not coherent or durable; final Drop does not authorize teardown.

Implementation and test review found no concrete defect in the operation lease,
retained resource graph, publication ordering, poison handling or snapshot lock
ordering. The 12 controls preserve explicit real-worker cleanup before their
designated final assertions. The returned-control-only test distinguishes the
sink's duplicated directory capability from the original File Arc, which expires.

[Host evidence](HOST-TESTS.md) records the production check and focused matrix.
These tests do not establish native acquisition, cross-process receipt authority,
external exclusion or durable recovery. All existing runtime, staging,
native-ownership, USB and release gates remain CLOSED.

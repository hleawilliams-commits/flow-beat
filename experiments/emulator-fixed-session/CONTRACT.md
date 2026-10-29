# One-attempt retained route construction

This inactive Unix component binds the actual settings bridge and the frozen
preview route to one complete baseline returned by an injected backend. It
addresses the settings-sink counterexamples in which a separately supplied
baseline mismatched the route, or a failed route later adopted another epoch
before the closed bridge rejected its callback.

The constructor provides no native backend, acquisition authority, worker or
guest startup, endpoint, main wiring, restoration or restart permission. Its
parent route, producer, transport, lifecycle, sink, mapper and pair remain frozen.

## Public construction boundary

`FixedSession::retain(backend, directory)` consumes the actual boxed backend and
original directory file, returning a session and a cloneable evidence handle.
It performs no backend call, directory observation, duplication or settings
attachment. The caller can retain the evidence handle before initialization.

`initialize(capacity, namespace, absent_mode, selection)` is one attempt.
It retains the full configuration before validation. Invalid capacity, a zero
namespace or a creation mode outside ordinary permission bits fails before the
underlying baseline call. A second initialization attempt refuses without
re-querying the backend, replacing retained products or erasing the first
configuration, baseline result or terminal cause. A repeated call after success
does not invalidate the already returned healthy route.

Initialization proceeds through these published boundaries:

1. `Preflight`: options are retained and checked.
2. `BaselineArmed`: the sole underlying baseline call is about to run.
3. `BaselineCaptured`: the complete returned receipt or error is retained.
4. `SettingsRetained`: the actual settings bridge and its evidence are retained.
5. `RouteRetained`: the actual route and its immutable proxies are retained.
6. `Ready`: initialization completed and the route may be returned.

The phase is the last published boundary. A separate first terminal cause does
not erase it. An unwind leaves the available evidence at that boundary; an
armed phase does not establish that a call happened, completed or had no effects.
There is no initialization retry or automatic repair.

## Baseline correspondence and one generation

The exact captured `BaselineReceipt`, including its full frame and kernel
sequence, is supplied to `SettingsSinkBridge::attach`. The private backend proxy
later returns that same captured value to the route exactly once. It marks the
receipt served before returning it. Subsequent baseline calls refuse before
querying the underlying backend.

The returned route starts unarmed. The caller must use the ordinary route or
port handshake and confirm exact model adoption plus released controls. This
constructor does not assert release or bypass the frozen producer's checks.
If staging is abandoned or adoption is rejected, the cached receipt cannot be
issued again. Closure, definite backend rejection and settings failure likewise
cannot acquire a later epoch through this route.

The proxy does not independently authenticate an envelope's key. The frozen
route owns creation/admission ordering and validates the adopted key, outstanding
envelope and expected ACK sequence. Its endpoint cannot be replaced through the
returned route API. Direct access to that same route therefore cannot replace the
private proxy or obtain a fresh backend baseline.

This is historical value correspondence. It does not prove the captured receipt
is fresh, authoritative or still reflects the backend when first used. A supplied
backend can internally share resources with other callers. Separate session
constructions, other backend aliases and competing native writers remain outside
this route's enforced boundary.

## Resource custody

The backend remains inside its retained shared mutex during baseline and submit
calls; it is never moved into a temporary call closure. The original directory
remains retained while settings attachment uses a duplicated file handle.
Attachment performs the frozen pair's validation and original-file observations.
If attachment fails, its partial observations are not reconstructed or claimed
as complete original settings.

Successful attachment is published as retained settings storage before subsequent
construction steps or test hooks. That storage owns the actual bridge as well as
its independent evidence handle. A successfully created route is likewise
retained before subsequent steps. Constructor error or unwind therefore does
not discard already published backend, directory, settings or route custody.

The ownership graph has no built-in cycle. Route proxies retain only the separate
backend/journal cells or settings storage; they do not retain the session's
container or route slot. A surviving evidence handle retains the session's
products. A surviving route retains its endpoint resources. Final owner Drop or
process death can release them and lose in-memory evidence. No destructor
restores files, neutralizes native state, performs recovery or grants teardown.

The constructor does not create or retain a worker or supervisor. Callers may
compose the returned route with the existing transport/lifecycle APIs, whose
pairing, stop, join, callback-dependency and final-Drop limits still apply.

## Forwarded requests and poison

Before calling the underlying backend, the proxy records the exact envelope as
`SubmitPhase::Armed`. The call runs without holding the evidence journal lock.
When it returns, the proxy retains its exact `BackendOutcome` as
`SubmitPhase::Returned` before the frozen route processes it.

That outcome is a wrapper observation. A returned positive ACK can still be
malformed and refused by the route. It does not establish producer acceptance,
settings invocation, file completion or native authenticity. The retained latest
attempt is bounded diagnostic state, not a complete history.

A local refusal before forwarding uses a distinct positive rejection reason.
It does not send, retry or replace the request. If the backend panics, the
retained backend mutex is poisoned and the exact attempt remains armed.
A panic after the underlying return but before journal publication can also
leave an armed attempt; no return value is guessed from later state.

If journal poison is detected after a backend result actually returned, diagnostic
salvage retains that exact outcome, keeps poison visible and returns `Unknown`
to the route. This closes admission conservatively rather than treating the
salvaged observation as an accepted receipt. Poison is never cleared.

Initialization refuses observed backend, journal, slot or retained-writer poison.
Poisoned slots may be salvaged only to retain newly created resources. The slots
hold custody rather than constituting a runtime admission interlock: poison
introduced after Ready is visible in snapshots but does not by itself alter the
private backend proxy. Runtime writer poison is rejected by the settings proxy;
the frozen callback still occurs after a known native commit. The one-generation
restriction prevents rearming afterward; it cannot undo that commit.

## Settings, late ACKs and snapshots

The settings proxy calls the frozen bridge under the retained writer mutex.
It neither changes projection policy nor adds a write/recovery path. Mapping
failure, partial file replacement and callback panic preserve the bridge/pair
evidence and the frozen producer's separate native-ACK accounting.

A positive ACK arriving after route closure can be recorded by the producer
while settings remain skipped. This constructor does not persist that late ACK,
reopen admission, retry the native request or reconcile host files automatically.

`EvidenceHandle::snapshot` copies the initialization/backend journal, clones
retained settings handles under the slots mutex, then reads the bridge's
independent diagnostic evidence. It does not acquire the backend or writer mutex,
call route diagnostics, invoke a callback or perform filesystem I/O. It exposes
journal, slots, backend and writer poison separately.

These sequential copies are not a coherent instant or a fresh filesystem/native
observation. Wrapper return, producer ACK, bridge completion and pair completion
remain distinct. Evidence permits inspection while an injected call is blocked;
it supplies no deadline, cancellation, durable journal, callback-return proof,
recovery token or resource-release authority.

## Remaining runtime gates

Authoritative backend acquisition/FFI and receipts, enforceable native and host
writer/reader exclusion, authoritative original files and stopped-process
identity, native dwell/consumption, callback deadlines, both-file and overlay
restoration, durable custody, final Drop/death and guarded restart remain open.

Frozen parents, official cores, protections, packages, catalog and unrelated
work are preserved. This component does not change an emulator or physical deck.
All runtime, stems-staging, native-ownership, USB and release gates remain CLOSED.

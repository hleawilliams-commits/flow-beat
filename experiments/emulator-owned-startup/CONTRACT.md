# Retained startup and explicit finish

This inactive Unix composition retains startup resources while connecting the
fixed-generation session to the actual mixer channel and existing lifecycle
owner. It provides no guest launcher, backend acquisition, main wiring, native
activation, restoration or guarded restart.

## Retain before operation

`OwnedStartup::retain` consumes an injected backend, explicit directory and an
optional supplied supervisor. It returns the startup object and an external
handle before baseline queries, settings attachment or worker creation.

The fixed session and its independent evidence remain retained. The actual
supervisor stays in separate shared custody; the lifecycle receives a private
reference wrapper. Moving that wrapper into a lifecycle or rejected pairing
does not take the actual supervisor out of the retained storage. Presence of a
supplied resource does not prove guest identity, liveness or stop authority.

The external handle exposes copied diagnostics and explicit finish. It does not
extract the supervisor, raw route or unique worker. Only successful Ready
construction returns an erased lifecycle-control interface and its ordinary port.

## One startup attempt and operation lease

Start is a single attempt. Its configuration, phases and first error remain
diagnostic state after failure or unwind. A later start does not recreate the
route, query another baseline, spawn another worker or replace retained inputs.

Start and external-handle finish share an atomic operation lease. The lease
guard releases that lease on return or unwind; it performs no stop, join,
restoration or resource cleanup.

If finish sees an active operation, it returns InProgress. This does not request
cancellation, prevent startup from reaching Ready or establish any worker state.
The caller must invoke finish again after the active operation completes.

A successful finish call before startup permanently closes future start and
reports that no worker was retained. It does not manufacture a joined receipt.
Concurrent direct calls to an already returned lifecycle remain governed by the
frozen lifecycle's own idempotent finish state.

## Publish each actual resource

Start initializes the fixed session, retaining its route, then calls the actual
transport channel. The route remains unarmed; construction does not adopt the
baseline or assert released controls.

The exact returned port and unique worker are published into retained storage
before PairRetained is recorded and before any subsequent startup hook or
callback. Pairing then moves that pair
through the frozen `OwnedLifecycle::try_new` and publishes either the resulting
owner or every rejected input.

The pending-pair take, pairing call and owner/rejection publication form one
audited interval under the products mutex with no injected hook or callback.
The frozen pairing function checks shared-object identity and constructs its
state; it does not invoke user code. Blocking calls and test hooks run outside
the products mutex.

The retained owner is published before Ready or exposure to the caller. An
unwind after pair or owner publication therefore leaves those actual resources
available to the external handle. Failed pairing preserves the exact port,
worker and supervisor reference; refusal is not a completed join.

## Transport constructor boundary

The channel starts a real host worker before returning its port/worker pair.
The startup composition cannot retain a worker handle that the frozen channel
has not returned. It records ChannelArmed before that call.

A returned spawn error is a known constructor outcome. The actual error branch
signals its failed shared state and closes its route; no returned worker pair
or joined receipt exists.

If an unwind leaves ChannelArmed with no captured pair, the outcome is
StartupUnproven. Absence of a captured worker is not proof that no worker
started. Post-return fault hooks do not inspect an interior allocation failure,
process abort or an uninstrumented unwind inside the frozen channel.

The private candidate's test-only helper invokes the actual internal
`channel_with_spawn` with a closure returning an error. This exercises that
error branch without pretending to induce an operating-system spawn failure.
It changes no production channel body.

## Explicit finish and retained results

After a startup unwind, finish can promote a retained matching pair into a
retained lifecycle owner. It invokes the owner's real finish outside the products
mutex. The owner requests stop and joins, retaining the exact joined session and
copied accounting.

Only an actual joined result establishes local worker completion. InProgress,
NoWorkerRetained, StartupUnproven, stop-only status, poison or pairing refusal
cannot substitute for it. A known native ACK, settings callback outcome and
partial file-pair result retain their separate meanings from the frozen parents.

Finish may block on the existing backend or settings callbacks. The lifecycle's
direct worker-thread check prevents direct self-join, not arbitrary callback
dependency cycles. A join does not cancel native work already started, restore
host files or overlay state, authorize supervisor release, or grant restart.

## Poison, diagnostics and lifetime

Operational journal or products poison refuses further operations; poisoned
state is not cleared to admit work. If an in-progress constructor actually
returns a route or pair after poison, custody publication may still insert that
actual resource into poisoned products. Exact returned outcomes may likewise be
recorded through diagnostic salvage. This prevents dropping a newly returned
worker; it grants no promotion, admission or finish through poison.

Snapshots may salvage copied diagnostic state and retain resource references
with explicit poison flags. Refused finish can leave a retained worker unjoined.
That state requires a separate decision; retention alone is not successful
cleanup or recovery. If the actual lifecycle finish completes while the startup
journal becomes poisoned, its observed result is retained diagnostically and
the external call still returns poison.

Snapshots copy the startup journal and retained references under short locks,
then read fixed-session and lifecycle diagnostics separately. They do not
acquire the mutable fixed-session operation lock, invoke the backend or settings
writer, inspect the port's live mailbox through a blocking operation, poll a
supervisor, or perform filesystem I/O. These copies describe separate instants.

The ownership graph avoids a built-in cycle. The lifecycle retains the
supervisor reference and transport resources; transport retains the route;
the startup container retains those products and fixed-session evidence.
Neither lifecycle nor transport retains that startup container.

While a handle survives, it retains the published products and supplied
supervisor. Final handle/owner Drop can still release resources. A worker's
ordinary Drop requests stop and drops its join handle; it is not a join.
Process death, abort and final-Drop recovery remain unresolved.

For a returned-control-only survivor, the startup's original File Arc may expire.
The frozen settings sink retains its own duplicated directory descriptor. That
capability is sufficient for its existing directory-relative operations; it is
not proof that the original descriptor or startup diagnostic container survives.

## Limits preserved

Captured baseline correspondence is historical, not freshness or authentication.
Separate constructions, backend-internal aliases and competing native or host
file users remain outside this constructor's exclusion guarantees.

Native acquisition/FFI and receipt authority, authoritative originals and
stopped-process identity, external writer/reader exclusion, native dwell,
callback deadlines, both-file/overlay restoration, durable recovery and guarded
restart remain open. Main stays inactive. No guest, emulator endpoint, physical
deck, official core, community package or catalog is changed. All existing
runtime, stems-staging, native-ownership, USB and release gates remain CLOSED.

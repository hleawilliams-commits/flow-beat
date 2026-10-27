# Offline single-broker contract

`broker.c` is a C library over injected backend callbacks. It contains no device,
socket, service, file-restoration or daemon operations. It neither opens nor
closes the controller. The backend represents one retained live controller open
file description, with no other broker or writer using its authority. Those
lifetime and exclusivity conditions are caller obligations, not facts proved by
initialization or a matching kernel snapshot.

The public structs are semantic values, **not ioctl wire layouts**. A future
backend must translate every field to the reviewed native64 V2 ABI, validate the
wire version, reserved fields and exact command encoding, and retain the same
module/controller identity. No wire adapter is implemented here. Its kernel
counterpart is the separate [instance/generation candidate](../emulator-overlay-handback/README.md).

## Initialization and adoption

`mb_create` allocates one opaque broker, captures the backend function/context
values and verifies a fresh backend snapshot against the complete caller-supplied
owned state. Required state is nonzero instance/run/epoch, exact flags 31
(enabled, fenced, active, caller-owner and injected), CRC prefix 96, exact kernel
sequence and all 152 raw bytes. Only a complete matching snapshot publishes the
broker object. This is not a CLAIM, a proof that a previous broker died, or
permission to recreate lost in-memory history while a syscall might remain
pending.

Admission starts closed. `mb_adopt` verifies another fresh exact snapshot and the
complete supplied baseline before issuing one producer grant for the current
epoch. A grant contains instance, run ID, kernel epoch and nonzero producer ID.
The initial kernel sequence may be nonzero; the broker does not reset it.
Producer request IDs start at 1 independently of kernel sequence.

A grant is issued at most once in an epoch. Quiescing, a known rejection or
positive reconciliation never resets this rule. A successful or positively
reconciled `ROTATE` creates a new epoch with sequence zero and clears the old
grant; a separate fresh checked adoption is still required. A failed adoption
does not issue a grant or silently accept a different kernel baseline.

## Requests and bounded receipts

`mb_stamp` is a pure copy helper: it copies a grant, producer request ID and all
152 bytes into a by-value request. It cannot prove when the payload was
created or detect old content reauthored with a new grant. The producer must
stamp at creation before enqueue and must never retag old queued/cached input.
The separate producer gate owns that local queue discipline.

`mb_submit` checks the full grant and the next producer request ID before invoking
the backend once. No ID is silently skipped or renumbered. Request ID zero is
invalid; `UINT64_MAX` is refused so the stored next ID cannot wrap. Kernel
sequence exhaustion also refuses new submission. Both overflow refusals close
admission and preserve the retained receipt. An invalid request or stale/gapped
ID never advances IDs or overwrites the last valid receipt.

The broker retains one last kernel-operation receipt. A FRAME receipt includes
the entire producer grant, request ID, command, predicted post-state and all 152
request bytes, including CRC-area and tail bytes. An exact last FRAME duplicate
returns that cached outcome without a snapshot or submission callback, even
when admission is closed. That return does not rearm admission. A same-ID request
with different bytes is a conflict: it closes admission without replacing the
valid receipt. An older receipt is not a history database: after a newer
operation replaces the last record, an old request is refused. Old-epoch grants
are refused before cache lookup after rotation.

A ROTATE receipt has a distinct operation tag and a zero producer-request field.
It replaces the last receipt. There is no automatic replay or deduplication of
ROTATE calls: a second explicit call after a known successful rotation requests
another new generation. Callers must reconcile an uncertain operation, not call
ROTATE again as a retry or repeat a verified recovery phase.

## Backend result and uncertainty contract

The submit callback returns exactly one of:

| Result | Required meaning | Broker effect |
| --- | --- | --- |
| COMMITTED, error 0 | This exact command committed once | update local expected state |
| REJECTED, error greater than 0 | This exact command definitely did not commit | retain rejection; close admission |
| UNKNOWN, error at least 0 | Delivery or outcome is uncertain | retain exact unresolved command; close admission |

A malformed outcome/error combination becomes UNKNOWN because it cannot prove
absence of a side effect. A negative diagnostic error is not used as proof of
rejection. The backend must not implement retries, change its controller file,
throw through the C callback, mutate the command or reenter the broker. A
transport timeout or nonzero transport status alone is not a definite kernel
rejection. False callback claims are outside the contract.

`MB_OK` from submit, rotate or reconciliation means the receipt must be inspected;
it does **not** necessarily mean COMMITTED. Output receipts are initialized on
all API paths. API refusals are separate negative `mb_result` values and leave
retained valid history intact. Input values are copied before clearing an output
so a request nested in an output receipt is not accidentally erased.

UNKNOWN blocks new submission, adoption and rotation. An exact duplicate returns
the cached UNKNOWN without resubmitting. `mb_reconcile` performs only a snapshot
and permits positive reconciliation when the entire expected post-state matches:
instance, run, epoch, sequence, owner flags, CRC and all 152 bytes. This relies on
the same live owner, one unresolved command and no intervening writer. A lower
sequence, partial snapshot, different payload or any ambiguous state stays
unresolved; it never proves cancellation. Failed snapshots retain the command.

Positive FRAME reconciliation updates the receipt and local expected state but
keeps admission closed and the grant consumed. Positive ROTATE reconciliation
also clears the old grant for the new epoch; it still keeps admission closed.
Neither resumes a producer, repeats an ioctl, claims native event cancellation
or authorizes a service restart. `mb_inspect` copies local bookkeeping only and
is explicitly not a new kernel acknowledgment.

## Closed-state rotation

`mb_quiesce` closes producer admission without any backend operation. Once closed
and with no unresolved command, `mb_rotate` first verifies a fresh kernel
snapshot exactly equals local expected state. It then submits one opaque full
frame using the current instance/run/epoch and exact next kernel sequence.
Successful rotation retains owner/run, advances epoch and resets kernel
sequence to zero. Epoch or sequence exhaustion refuses without wrapping.

A known rejection permits a later explicit rotation because the callback
certifies no commit; that rotation still requires the fresh exact snapshot.
Unexpected kernel drift is refused rather than adopted. The library never
reclaims an owner or replaces its retained backend context. The opaque frame is
not certified neutral and the operation does not prove the application stopped.
[Stopped recovery](../emulator-overlay-handback/RECOVERY-CONTRACT.md) remains a
separate unimplemented coordinator protocol.

## Serialization and lifetime limits

Every operation on a published broker is serialized by one pthread mutex,
including backend callbacks. Concurrent callers wait; a same-thread callback
reentry is outside the contract and the error-check mutex refuses it instead of
recursing into state changes. The mutex provides no latency guarantee, no
cross-process lock and no exclusion against external writes or a second broker.
There is no I/O timeout, cancellation, background thread or implicit retry.

`mb_destroy` requires caller-proved absence of concurrent methods and callbacks.
It does not close the backend, issue RELEASE, neutralize input or resolve a
pending command. Refusing to free a mutex reported busy is defensive behavior,
not a general lifetime proof. Destroying or crashing a broker can discard its
only receipt; constructing a replacement from STATE alone does not recover
unknown pending calls. Immutable requests and caller-owned buffers must remain
stable while a method copies them.

Memory-only receipts are not durable process-death recovery. Module reload,
VM clone/restore, open-description loss, producer process lifecycle, real ioctl
transport, snapshot freshness, native sampled-event lifetime and physical timing
remain outside this library. Tests over callbacks or the extracted kernel can
exercise the declared state machine without proving those external conditions.
No runtime identity, enabling manifest, device action or installation is created
by this source.

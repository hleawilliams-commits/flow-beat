# Retained RESTORE contract

The parent [broker contract](../emulator-mixer-broker/BROKER-CONTRACT.md) continues
to define callback truth, one immutable backend context, mutex serialization and
external lifetime obligations. This successor adds a separate restore identity
and recognizes the kernel's selected idle mode. It does not acquire or close a
controller fd. Successful construction still requires an exact fresh snapshot.

| Operation | Local precondition / result |
| --- | --- |
| create | Complete owned state: nonzero instance/run/epoch, flags exactly23 or31, CRC96; exact backend snapshot |
| adopt | No UNKNOWN, closed admission, no grant issued in the epoch, flags31, full exact baseline plus fresh state |
| FRAME | Existing producer grant/request accounting; target mode explicitly injected, kernel sequence +1 |
| ROTATE | Closed admission, no UNKNOWN, exact fresh state; target mode injected, epoch +1/sequence0 |
| RESTORE | Bound full source, independent next restore ID, strict0/1 mode, closed admission, no UNKNOWN, exact fresh state; epoch +1/sequence0 |
| reconcile | With UNKNOWN, only a complete exact retained expected result commits; leaves admission closed |

## Identity, caching and refusal

`mb_restore_request` is a value object. The caller binds `source` before enqueue.
Every instance word, run, epoch, sequence, flags, CRC length and all 152 source
bytes participate in identity, as do mode and all 152 target bytes. Comparisons
are fieldwise and include raw CRC positions/tail; struct padding is not identity.
The function copies input before clearing output, so an input within the output
receipt is supported. Caller mutation concurrent with that initial copy remains
invalid; the caller must synchronize ownership as for the parent API.

Nonzero ID and valid pointers are required. Under the mutex, the retained last
RESTORE with the same ID is examined before new-request shape/state/capacity
checks. An exact duplicate returns the existing outcome without snapshot,
submit, counter change or admission change. It is a historical response, not a
fresh assertion of current state, native recovery or permission to restart.
After a separate successful adoption, it does not revoke that adoption either.
A same-ID conflict, including malformed source or mode, closes admission and
preserves the valid receipt. Invalid new source/mode is refused without attempt.

IDs start at1. Only the next ID may attempt, and UINT64_MAX is not attemptable.
The ID advances immediately before the sole backend callback, including known
REJECTED and UNKNOWN outcomes. Local shape/source/snapshot/admission/capacity
refusals consume no ID and preserve the last receipt. FRAME, ROTATE, adoption
and RESTORE commit never reset this counter. If a different operation replaces
the one cached receipt, an older ID cannot be resubmitted. This is a one-receipt
cache with a high-watermark, not complete history or durable deduplication.

The request's source must equal the entire locally expected state, then a fresh
backend snapshot must match that state. Sequence and epoch must both have
capacity. A known rejection consumes its ID even though local expected state is
unchanged; an exact duplicate returns that rejection. A later explicit request
with the next ID and unchanged exact source can be a distinct attempted action.
The kernel itself enforces that mode0 target bytes match its immutable idle
frame. The broker has no authority to infer that template or normalize bytes.

## Uncertain outcomes and custody

The broker retains source, target, ID, one kernel command and expected post-state
before callback. A malformed response becomes UNKNOWN. No operation resends the
command, changes its sequence or constructs a replacement on timeout. New
mutations and adoption remain blocked while UNKNOWN; cached duplicates and
read-only inspection remain available. Reconciliation queries the same backend.
Only full exact positive state, including selected mode and every byte, updates
the expected state and retires the old grant. An old/lower state, a similar frame,
mode mismatch or failed snapshot leaves uncertainty unresolved.

Same-fd custody and a single mutator are external assumptions, not consequences
of a state comparison. Another actor sharing those capabilities could make a
matching state and invalidate causal attribution. The library cannot prove the
native application stopped, original baseline authority, host-file identity,
writer exclusion, native event ownership, cancellation, neutral buttons or a
recovery permit. Main/preview wiring and all runtime gates remain closed.

Destroy requires external quiescence. It neither waits for native threads nor
closes a live endpoint. Process death loses request history; reconstruction from
STATE alone cannot settle a lost pending command. Callbacks have no deadline.

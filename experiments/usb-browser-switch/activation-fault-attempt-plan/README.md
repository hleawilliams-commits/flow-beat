# Fault-attempt admission plan

This is a **source-only plan** following the [fault-reader operating contract](../activation-fault-reader-contract/README.md) at `02989eff605289767cacf9761f2cd83edaee6182`. It proposes a separate, value-only experiment for one terminal fault attempt. It changes no consumer, handler, core, package, quarantine policy or live gate. All live gates remain **CLOSED**. Ownership is not fault causation.

The [machine-readable plan](admission-plan.json) contains six transformation groups, eight future case groups and eight exact safe-control failure sets. None is implemented, compiled or executed here. [Retention](result.json) and [independent verification](verification.json) describe only pinned source bytes and source review. Existing successful fixtures, failed packets and audits remain frozen; no passing matrix is replayed.

## What the retained source requires

The exact 962-byte `cm_blame_fault` consumes its plain volatile once flag before lookup and I/O. It returns a selected module ID despite ordinary open/write/fsync/close failures; missing ownership also consumes the attempt. Its separate unblamed provider has live-mod, time and append behavior that the preceding fixture replaced wholesale. The existing body stays byte-identical and is **not called** by the proposed successor.

The proposed single admission transition preserves that one-attempt intent while explicitly changing the orchestration in an independently named authored module. It is not a wrapper that claims to make the frozen non-atomic consumer concurrent. Main reporter post-claim same-thread fencing, separate consumer routes, handler installation/chaining, native frame lifetimes, alternate stacks and libc/canary behavior remain separate contracts.

## One permanent attempt, two kinds of publication

One fixed process-image record starts Virgin, becomes ActiveIncomplete at admission, and may become OrdinaryReturned only after the winner finishes all expected returned steps. It never returns to Virgin. Independent cases start fresh processes; no reset, retry, reopen, disposal or reclamation entry exists.

The planned authority is one strong C11 compare-exchange from Virgin to ActiveIncomplete (success acquire/release, failure acquire), without a loop or preceding-read permission. Only that winner may touch working data or call a provider. A losing request returns Suppressed immediately and reads no unfinished payload. Suppressed is a request result, not another record state. A request nested before the outer admission may win; one nested after admission must lose. This is specified behavior, not evidence of actual signal delivery or concurrent C execution.

Working receipts belong exclusively to the admitted request. Public observation acquire-loads the state once: Virgin exposes NotAttempted; ActiveIncomplete exposes **no payload**; OrdinaryReturned exposes the complete immutable result. The winner initializes every final field before one release-store of OrdinaryReturned. Even a provider return followed by a pause before final publication remains incomplete to observers. This avoids presenting plain mutable diagnostic fields as concurrently readable.

The ordinary result separates:

- Copied ownership: resolved Community index, process identity and canonical zero-padded 33-byte ID, or NoCommunity.
- Which value-provider stages may have been entered and which have returned and been recorded.
- Each raw result, requested write length and failure-errno-valid bit.
- Independent known-failure and short-write flags; both may be true when a short write is followed by a sync/close error. OrdinaryReturned means all planned calls for that selected branch returned and were recorded, never durable persistence, fault causation or recovery.

No owner or raw outcome is publicly inferred while the attempt remains incomplete. A serial test driver may inspect stopped working data and a separate raw-return oracle; that private inspection is not a production snapshot API. No cross-thread visibility, lock-free signal or whole-handler guarantee follows from this plan.

## Selected ownership and returned operations

The future experiment receives bounded **authored selection values**; it does not repeat module/generated attribution, frame scanning or DSO loading. A resolved Community value is copied into permanent working storage. Core and unresolved identity do not become fabricated blame IDs: the authored NoCommunity disposition represents the selection adapter finding no Community owner after its own fallback policy. The predecessor's exact reader and receipt cut remain untouched.

For a Community selection, a modeled open of fd zero or another nonnegative fd permits one write of the ID bytes (no newline), then one fsync, then one close, provided each preceding stage returns. A short or failed write still proceeds to fsync/close, as the retained consumer does. An open failure ordinarily completes without those later operations. No returned error, including EINTR or an uncertain close effect, permits a retry or reopening.

Each authored result records its raw return before classification. Failure errno is valid only for an authored failure; an old errno sentinel after success is not a failure observation. All expected return values can be observed without proving a durable file or real syscall behavior. Known short/error returns are distinct from a call with no observed return.

NoCommunity enters exactly one **opaque unblamed operation**. The plan observes its entry and ordinary return only. It neither implements nor certifies `g_cm_live`, time conversion, append I/O or the next-start quarantine behavior. Preserving these policies means leaving the actual code and files unchanged, not treating an observation count as equivalent.

Six fixed event slots cover selection plus four blame operations, or selection plus one unblamed operation. The future module allocates no event storage and must not silently drop observations. Authored values and scripts stay alive through their use; there are no native pointer reads or retained DSO-owned slots.

## Incomplete does not authorize another attempt

Before entering a provider, its stage records “may have been invoked”; this is not proof of a completed side effect. A withheld authored return leaves it unknown. A raw value supplied by the harness but not yet committed to the working receipt is a separate oracle fact; observers cannot borrow it.

The fixture will stop deterministic progression at entry, return-before-record and record-before-completion cuts. It will not trigger a signal, execute a nonlocal exit, cancel a thread or pretend that a real process crashed. Later requests remain suppressed in every cut. Stopping or abandoning an authored schedule never rolls back ownership, repeats I/O or supplies a recovery permit.

The future tests cover ordinary missing-owner/error outcomes and incomplete stages independently. A normal failed open is a known return; a held open is not a refusal. A returned ID is ownership; it is not evidence that writing the ID succeeded.

## Planned schedules and controls

Eight case groups cover competing admission, nesting at every selected boundary (including opaque unblamed entry/return), ID copying and NoCommunity, open, write, fsync/close, incomplete cuts, and permanent result observation/repeat suppression. They use explicit sequential step ordering. Two actors preobserving Virgin still need the one winning transition; no test runs the frozen volatile read/store as an actual data race.

Each control has a dedicated fresh selector and one declared semantic failure. All other checks in that selector are infrastructure checks. The actual mutation must affect admission, custody or operation/result behavior; changing an assertion or diagnostic counter is not a control. No downstream duplicate veto may mask it.

| Safe control | Exact required failure |
| --- | --- |
| Admit second contender from cached Virgin | `terminal-admission-single-winner` |
| Reopen a held unknown attempt | `unknown-attempt-stays-terminal` |
| Omit copying a still-live authored owner ID | `selected-owner-copy-survives-input-change` |
| Derive selected ownership from write success | `selected-owner-independent-of-io-outcome` |
| Invent a return for held write | `armed-write-without-return-remains-unknown` |
| Retry a short write in the value model | `short-write-retains-single-write-sequence` |
| Skip fsync/close after returned write error | `returned-write-error-still-fsyncs-and-closes` |
| Treat stale errno after success as valid failure errno | `successful-return-has-no-valid-failure-errno` |

Control-only reopening/extra writes alter authored values only; they do not add real reset/retry APIs, invoke syscalls or mutate the frozen consumer. The ID control changes a still-live array to another valid same-length 32-byte ID so it detects copy omission without a dangling pointer or validity failure. The invented-return control changes authoritative working receipt fields that would feed completion, inspected serially while public completion remains absent; it does not merely change diagnostics. The admission controls stop before a second provider sequence. Each baseline must pass; a control must fail exactly its singleton by recognized assertion output, never by crash, timeout or undefined behavior.

## Evidence boundary and next step

This plan reuses local pinned sources and line-byte anchors. The retention helper only reads/copies them and writes a new closed packet. It runs no child commands, SDK Git capture, compiler, loader, installer, runtime, signals, native frame or crash-file providers. Structural byte checks are not behavioral test cases. Prior fixture source and codegen evidence are not replayed.

Before a first build, the separately versioned successor must freeze a complete source transformation ledger, source hashes, value-only scheduling seams and exact control failure sets for independent review. Its first plain and ASan/UBSan runs may test only this new authored state machine. No old scalar forwarding, ABI, exception, DSO, attribution or installer matrix should be repeated unchanged.

Actual Linux code generation, ordinary concurrent operation, signal/reentry behavior, native provider outcomes/lifetime, transitive dependencies, handler previous-action publication, external writers, quiescence and recovery remain unproved. No production integration or live observer composition is authorized. Keep all native/runtime gates closed.

# Immutable task value transport — host candidate

This adds a bounded C++17 transport around the **unchanged actual**
[`capture.h`](../task-capture-contract/capture.h). It publishes entry values and
normal submission-return values separately. It adds no hooks, native calls,
automatic joins, admission/success/cancellation/completion state, or module
unload protocol.

`transport.h` is header-only. The default capacity is 16; a constructed instance
represents one run and never reuses a slot. It requires always-lock-free 32-bit
and 64-bit integer atomics on the compiling platform. One source-level strong
reservation CAS is attempted; generated instructions or a library's strong-CAS
implementation can still retry internally. This is not a measured latency or
wait-free claim.

## API and caller contract

```cpp
tvt::Transport<16> transport(fresh_nonzero_run_id); // construct at quiescence
transport.start();                               // one shot

// Submission helper, before forwarding to the original:
tc_context context{}; // private, writable, disjoint from native task/transport
bool captured = transport.begin(&context, TC_SUBMISSION, original_lr,
                                valid_task, thread_observation);
// Forward native call exactly once regardless of captured. No transport state,
// lock, TLS admission guard, or active reference is held across that call.
// Only after normal return, using the private value copy and full 64-bit result:
if (captured) transport.finish(context, native_result);

// Execution helper uses TC_EXECUTION and a private local context. No after:
tc_context execution{};
transport.begin(&execution, TC_EXECUTION, execution_lr,
                valid_execution_task, thread_observation);

// Reader must check each bool independently:
tc_context entry{};
tc_return_record returned{};
bool have_entry = transport.read_entry(slot_index, entry);
bool have_return = transport.read_return(slot_index, returned);
transport.close(); // logical gate only; never a quiescence/unload certificate
```

The pseudocode states helper ordering, not an implemented hook adapter. Original
LR values must already satisfy the pinned capture contract. A wrapper still
needs its own ABI/provenance/exception/lifecycle evidence. The input pointer
must refer to stable native-owned task storage for all five entry reads; the
vptr/alignment checks do **not** make an arbitrary or freed pointer safe. The
caller must not concurrently mutate the task or context. Private output must
not be observed until `begin` returns. The capture contract requests only the vptr and selected scalar bytes; no
dispatcher/child/tag/padding range is requested. The fixture counts those
`TC_READ_BYTES` requests, not the machine-load extent of the default `memcpy`
or its library implementation. No native address is retained in a context,
slot, or return record.

The transport chooses observer invocation IDs as `slot + 1`. These IDs are
unique only within that storage's run and include reservations whose capture
was rejected. They are unrelated to native TaskNo, event order or identity.
Run ID and thread observation are caller-supplied values, with no allocator or
thread-provenance validation here. The caller must choose a fresh, nonzero run
ID, keep the entire object alive, and safely publish its address. There is no
reset/reconfigure/recycle operation. Never reconstruct or reclaim the same
storage while any before/after helper or reader can reference it. Different-run
contexts are rejected by `finish`; that check does not legalize stale pointers,
reused run IDs, or destroying the old storage.

## Publication and close semantics

- `begin` clears its caller output before every attempt. Disabled, wrong-phase,
  wrong-LR, full, and contended attempts do not read native task memory or grant
  a return context. Wrong phase/LR is filtered before slot reservation. A failed
  native capture consumes one immutable slot and publishes its rejection entry;
  the caller output stays zero/unqualified and cannot authorize a return.
- The one reservation CAS never retries at source level. A competing successful
  reservation can cause a contention drop even with spare capacity. If the
  failed CAS observes the capacity limit, it records a capacity drop. IDs and
  reservations do not wrap because capacity is strictly below `UINT32_MAX`.
- A private entry copy is written before its own release READY publication.
  Readers acquire READY before copying it. Readers can skip an unpublished
  earlier reservation and observe a later one, then revisit the earlier slot.
  Reservation ordinal is not publication order or native event order.
- `finish` first acquires the entry publication and compares all named context
  fields. It never compares struct padding. A single return-state CAS claims
  EMPTY → WRITING; only that winner writes the return record, then release
  publishes READY. A duplicate while WRITING or READY is rejected permanently.
  Return slots are never consumed or reset by polling.
- An entry can be visible while its return is missing. An execution entry can
  precede a submission return. A missing return remains unknown; exceptions,
  incomplete capture and other causes are not classified. Native zero, wrap,
  duplicate TaskNo and equal payloads remain values, not identity or completion.
- The gate linearization is the acquire Open load in `begin`. If it passes,
  `close()` can finish before that attempt reserves or publishes. Such an entry
  is permitted. A later attempt that observes Closed is rejected. `finish` for
  an already published eligible submission is permitted after close. `start`
  cannot reopen a closed or previously started instance.
- Each reader method has its own acquire; combining two reads or counters does
  not provide an atomic snapshot. A false read leaves the caller's old buffer
  untouched, so its bool must be checked. Diagnostic counters use separate
  relaxed loads, can lag READY, and can wrap. They are not loss-free event totals,
  a coherent state snapshot, a lifetime proof, or a quiescence test.

All public methods save and restore `errno` locally. There is no explicit
allocation or mutex in the transport, but `errno` access can call libc TLS
machinery; first-use cost and hardware timing are unmeasured. Its local guard
ends before the helper returns and never spans the original call. Test-only
scheduling seams compile to no calls in the normal header. The native read seam
defaults to the already documented `TC_READ_BYTES` memcpy calls.

## Validation retained here

`python3 run-tests.py --result test-result.json` binds the capture header to
commit `5213910` resolved to its full commit ID and checks its exact SHA-256
`aa9b82311065fa3481cda29883f6dae40d1685c213d520271e1f4530039291ea`.
Every invocation saves fresh source snapshots, compile outputs and run logs in
ignored `out-history/`; raw outputs are not release assets.

The candidate passes **220 assertions across 9 scenarios per mode** in plain,
ASan/UBSan and TSan. The deterministic schedules cover:

- admission filtering, rejected captures, exact five requested read ranges, cleared
  rejected output, errno preservation, bounds and unchanged false-read buffers;
- execution-before-return, absent returns, full-width result, context mutation,
  cross-run rejection, zero/wrapped/duplicate native TaskNo, and no slot reuse;
- a paused early entry reservation followed by a later READY entry; separate
  return WRITING while entry is readable; duplicate after while the first
  publisher is paused; close between Open and reservation; return after close;
- simultaneous one-CAS contenders with spare capacity and at the last slot;
- sixteen reserved writers released to publish payloads concurrently. The
  reader acquires each READY record before acknowledging it to that writer,
  and reads returns before producers can complete/join. Reservation order is
  deliberately scheduled; payload/return writes are concurrent. This isolates
  the per-record release/acquire publication rather than borrowing a join;
- real `mmap`/`munmap` between before and after, nested private contexts and
  reversed normal-return order, with no native reads in the after path.

Four discriminating controls run **only the serial, reentrant polling fixture**
(9 assertions each), separately in every sanitizer mode. They deliberately
publish before copying entry or return (one expected failure each), bypass
exclusive return claiming (two), or truncate the 64-bit result (two). The runner
requires the exact failure names, exit status and no sanitizer diagnostics.
They do not run intentionally broken publication through concurrent schedules
and then count unrelated races as the desired regression.

These are synthetic owned tasks and caller IDs, host schedules and mocks. TSan
covers exercised paths only. No firmware, device or runtime was contacted by
this experiment. No claims are made about native ownership, cancellation,
whole-engine races, module retention, audio quality, physical timing, watchdog
behavior, or readiness to install. The [independent review](REVIEW.md) records
the identical host repeat. A separate [ARM64 integration](../task-observer-integration/)
now combines these actual sources with both wrappers against synthetic callees;
it supplies no native SDK adapter, installed hook or firmware provenance.

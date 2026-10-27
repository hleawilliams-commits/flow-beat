# Prospective Rust producer gate

`producer.rs` is a standalone, std-only queue and acknowledgement gate for a
future mixer producer. It has no transport, ioctl, preview integration, settings
I/O or unsafe code. **The existing preview and forwarder are unchanged.** Passing
this fixture does not establish a usable handback or permit an observer run.

## Value/API contract

The creation key is `{instance[2], run_id, epoch, producer_id}`. `Envelope` keeps
that key, a request ID and all 152 frame bytes in private fields with read-only
accessors. A caller can copy an envelope for a channel or cache; the API has no
retag or frame-replacement method. Request IDs start at 1 for each successfully
staged new key and are unique together with that full key. The maximum ID is
refused so increment cannot wrap, matching the C broker's request-ID contract.

`Producer::new(capacity)` is closed, with capacity limited to 1–64 entries.
Creation and all admission/lifecycle decisions use one mutex. The capacity counts
both queued and unresolved entries; only one unresolved request exists. The
queue and latest-created cache contain immutable envelopes, not untagged frames.

| Method | Bounded behavior |
| --- | --- |
| `stage_baseline(BaselineReceipt)` | Stage an externally verified `{key, frame[152], kernel_sequence}` while closed; retain closure until ACK and release observation |
| `acknowledge_baseline(receipt)` | Require equality of the complete staged receipt; mismatch invalidates staging |
| `confirm_released(receipt, released)` | Require the exact acknowledged baseline and a caller-supplied release observation before opening |
| `create(frame)` | Under the lock, capture the active key, allocate the next ID and enqueue the complete canonical envelope; queue-full rejection consumes no ID |
| `admit(envelope)` | Validate a later-delivered envelope against the current key and exact queue front, then mark that one operation unresolved |
| `transport_written(envelope)` | Validate that it names the unresolved request; do not resolve or reopen anything |
| `acknowledge_frame(FrameAck)` | Require the exact unresolved envelope and next kernel sequence; clear only that operation, without reopening a closed gate |
| `reject_known(envelope)` | Resolve only an exact definitive broker rejection, close admission and discard unsent queue/cache entries; require a newer generation to resume |
| `connection_lost()` | Close, discard unsent queue/cache entries and retain the original unresolved request marked uncertain, without replay |
| `close_generation()` | Close local admission and invalidate queued/cache work; retain unresolved work |
| `with_host_settings(key, callback)` | Check the captured key and open admission, then run the callback under the same transition lock; an error closes the gate and retains unresolved work |
| `inspect()` | Return a coherent local value snapshot; no kernel query or authority grant |

Baseline and frame acknowledgements are different types. The baseline's kernel
sequence can be nonzero when adopting an existing owned state. A positive frame
ACK must name its exact successor. Exhausting kernel sequence or the last allowed
request ID closes admission immediately and removes queued/cache work. No
settings callback remains admitted under that exhausted gate.

The module cannot authenticate a receipt. A future trusted adapter must obtain
baseline grants/readbacks and definitive outcomes from the C broker, bind its
current ownership and lifecycle, and check the external neutral/full-frame
policy. A low STATE sequence, local enqueue, socket write or reconnect is not a
positive ACK or definitive rejection. The Rust values are not serialized ioctl
structures and no Rust/C FFI mapping has been implemented or tested here.

## Generation and unresolved-operation rules

For one `Producer` instance, later staging requires the same module instance and
run, and a strictly higher epoch; changing only `producer_id` cannot reopen an
old epoch. Even abandoned or mismatched staging consumes that epoch locally.
A module/run change requires a new producer plus a separately reviewed lifecycle
protocol, not automatic replacement of the old identity.

Any unresolved request blocks new baseline staging, including after close or
connection loss. An exact later positive receipt may reconcile it, but keeps
admission closed if it was already closed. A known rejection may clear it, also
without reopening. No method infers cancellation or erases unknown work to make
adoption succeed. There is no automatic resend or recovery write.

The canonical queue is deliberate: if an external mpsc message for ID 1 is lost
and ID 2 arrives first, admission refuses ID 2 and retains the queue. It does not
skip the broker's expected next ID. Explicit closure discards those unsent
entries and requires a new epoch. The external sender must itself use bounded
transport buffering; this module cannot bound a caller's unrelated queues.

Callbacks must not recursively invoke the producer while its mutex is held. A
callback error may follow a partial side effect, which this module cannot undo.
A callback panic poisons the mutex and all subsequent methods refuse access;
there is no poison-recovery path that silently reopens admission. Host recovery
and process-death behavior need a separate coordinator.

## Concrete preview integration still required

The existing pinned `az-preview/src/controls.rs` has SHA-256
`b9145e4b41439761abbfdec461b854412748af2fe95183fd5843ad34d4298445`.
Its `CtrlMsg` contains only bus/frame, `LatestFrames` caches untagged frames, and
`ctrl_thread` drains into that cache while disconnected and replays it after
connection. It records cache state before `write_all` and persists mixer levels
after successful socket writes. These are the unchanged paths audited in the
[writer-ownership investigation](../mods-shortcut-delivery/task-sdk-observer/overlay-policy-v2/writer-ownership/)
and [producer review](../emulator-overlay-handback/PRODUCER-REVIEW.md).

A future integration must carry the envelope from initial UI input creation
through the actual sender queue and reconnect cache. Attaching a fresh key when
old `CtrlMsg` data is dequeued is insufficient. The old mixer cache/replay and
socket-success settings writes must be replaced with explicit generation and
receipt handling; merely adding this module beside them gives no exclusion.
Other buses and LED traffic need their own retained behavior.

The UI must adopt the exact acknowledged broker baseline into its represented
controls, counter and optimistic sent-frame state while input admission is
closed. Held buttons, pending minimum-hold timers, pad state and gestures must
be released and rearmed deliberately. `confirm_released(true)` only expresses a
caller observation; it performs none of those UI operations. No preview close,
bridge stop, process control or emulator contact occurred in this work.

Two accepted limitation tests keep the scope explicit:

1. After a newer valid baseline is adopted, calling `create` again with copied
   old frame bytes creates an accepted new envelope. The gate cannot know when
   arbitrary caller-provided bytes were originally authored. Actual UI state
   adoption and creation-time stamping remain mandatory.
2. Two settings callbacks carrying the same valid generation can execute in
   reverse creation order. The settings gate checks generation and serializes
   the callback, but does not bind it to a request ID, order or positive ACK.
   The future adapter must establish those relationships rather than treating
   generation checking as complete host-state consistency.

This fixture therefore does not compose or replace the original `LatestFrames`
implementation. The previous actual-source reconnect counterexample remains
valid and was not rerun unchanged.

## Reproducible offline checks

From the repository root, using the installed local Rust compiler:

```sh
python3 experiments/emulator-mixer-broker/run-producer-tests.py \
  --result experiments/emulator-mixer-broker/private/producer-reproduction.json
```

The final candidate passes **19 tests** with all 19 exact names required by the
runner. They include real std mpsc delayed delivery across generation adoption,
a bounded canonical queue, concurrent creation, exact full receipt equality,
unknown-outcome retention, release-to-rearm, overflow, settings lock exclusion,
callback poison, and the two accepted limitation cases above. All frames and
side effects are synthetic owned memory. Compilation treats warnings as errors.

Eight strict source controls each compile and fail exactly one selected test:
retag at dequeue, compare only the baseline key, erase unresolved state on close,
accept socket-write success as ACK, allow same-epoch readoption, omit the release
barrier, consume an ID on queue-full rejection, and leave admission open after
an exhausting ACK. The runner requires exact test inventory, failure name,
counts and exit code. A compile failure, unexpected panic/test failure or timeout
cannot satisfy a control. No Rust sanitizer or Miri execution is claimed.

Every run retains the exact source and runner, mutant source, compiler identity,
list output, raw test output and binaries in a unique ignored private directory.
Public result fingerprints normalize only that private root and libtest's elapsed
duration; original diagnostics remain private. Inputs are checked unchanged at
completion. The initial 16-test preliminary pass is retained separately; the
final source added exhaustion closure and explicit limitation tests before the
19-test/eight-control run. No candidate failing run is omitted.

| Final artifact | SHA-256 |
| --- | --- |
| Producer | `70ec0056c3f02ae24e7d2a90e8fd2310208b1092e54da4bc2469eab794f29a06` |
| Runner | `0cec1296a802f6138d5a706d853fe8ed34b91d7270a5c5d6b3d8ffa57ce2b01e` |
| Sanitized result | `6d8072a63016ad4ac4bba093bcf72651a24d9fd2cb98aebaed3774c1ff71d082` |

An independent reviewer rebuilt and ran the same 19 tests and eight controls
from a new retained snapshot. Source, compiler, test inventory, cases, counts and
exit codes match exactly. Its JSON hash is
`a742ef4056add3c16bdfae9ea4a66fec1f7d9c504e7bd28e130ceef0cd4ac553`,
which is different from the author result: libtest includes a numeric thread ID
in expected-panic diagnostics, and the frozen runner intentionally remains as
executed, normalizing only private paths and durations. A retained read-only
comparison confirms all nine output bodies agree after additionally removing
only those named libtest panic-thread IDs. Comparison-receipt SHA-256 is
`ce1df20929be601589abf05c26cdb028b5501a6c55ef90fdd2ec3f36bc34e202`.
This is functional reproduction and equivalent failure bodies, not byte-identical
result JSON. No source change or additional unchanged run was made to erase
that difference. Both raw output sets and original run snapshots remain private;
the [sanitized independent receipt](producer-independent-result.json) is retained
alongside the author receipt.

The result does not prove real socket acknowledgement, genuine broker grants,
actual UI quiescence, physical control release, native event consumption,
cancellation, settings order, stopped recovery or runtime module ownership.
The observer runner remains disabled; no community package or physical deck
state changes follow from this standalone gate.

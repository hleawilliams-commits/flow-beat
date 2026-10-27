# Independent offline transport review

No blocking correctness issue was found within the inactive, injected-backend
scope reviewed here. The dedicated mailbox removes owned values from the shared
legacy message lane and gives the owned worker an explicit terminal-stop and
actual-join boundary. It does not implement a real broker connection, native
cancellation, settings-file adapter or complete producer handback.

The review covers `mixer_transport.rs`, the changed App/controls/main integration,
the unchanged producer and route contracts, and the new lifecycle test design.
The original emulator checkout and older experiments remain unchanged. No device,
emulator, socket, preview application or installed module was run for this review.
The independent full Rust run below used the frozen source inventory and runner
in a new private export. The reviewed successor patch is
`d630fd6c7b15b8d0f10f0020a9079833b42f839b8d7c1e84bffdbe833e1f485c`;
[the candidate inventory](candidate-pins.json) binds the complete prepared tree.
The frozen transport source SHA-256 is
`81611d8a098e6d47ed7c43873905c4d4660e34c4bc2d7aeb93403b5b934823d0`.

## Reviewed ownership and scheduling

`main` still supplies `None`. This preserves the existing legacy preview path.
`Some(port)` selects owned mode even when the port or generation is closed; it
never falls back to raw mixer traffic. App sends the key captured before its UI
update to `enqueue_for_key`. Enqueue failure does not advance its sent cache.
The cache means accepted into the local mailbox, not committed or consumed.

`CtrlMsg` contains legacy values only. The controls classifier receives an owned
mode boolean, rejects raw bus 2 locally, and retains the exact other-bus wire and
cache representation. It has no route/port/backend argument and cannot call
logical close or a settings callback. The same classifier is used before both
disconnected cache admission and connected sending. This removes the specific
synchronous owned-callback dependency from that classifier. It does not establish
a whole-process latency bound: legacy socket writes, OS scheduling and unrelated
locks still exist. LED reception remains a separate existing thread.

The transport mutex serializes the capacity check, canonical producer creation
and mailbox push. Capacity counts queued plus reserved/in-flight values across
generations. A full mailbox consumes no new request ID, discards unsent values
and closes that generation. Other creation errors also discard unsent work and
close. Values keep their original full key and request ID; refresh cannot occur
while a queued or reserved value remains. Repeated closure/adoption therefore
does not accumulate owned envelopes in the old unbounded legacy mpsc lane.
The bound is on this mailbox, not all application/backend allocations.

Worker reservation happens under the mailbox lock; backend dispatch and ACK/
settings processing happen after releasing it. A single retained last receipt
and optional reserved envelope bound result bookkeeping. A separate one-token
wake channel cannot consume a payload slot. An already-retained wake covers a
full wake channel; a wake sent between the empty check and blocking receive is
not lost. Idle stop and Drop use the same wake route.

The code assumes **one port family exclusively manages its route**. This is a
trusted construction precondition. Passing an `Arc<MixerRoute>` does not prevent
other code from retaining it, invoking route methods directly, or constructing a
second port family. The current inactive `main` does none of those things, and no
live factory or ownership-enforcing adapter is introduced by this candidate.

## Stop, join and retained uncertainty

Terminal stop is an atomic flag independent of queue capacity. It rejects later
creation, baseline refresh and adoption. Explicit stop also discards unsent work
and closes the route; it may wait for a creation/settings operation. Stop is not
a cancellation or a joined receipt.

A request reserved before stop may already have passed the last terminal check
and may still be admitted or forwarded before logical close wins. The code has a
separate `NotForwarded` outcome when its own pre-dispatch terminal check skips the
call. A reserved envelope alone is not evidence that the backend was invoked.
A late positive ACK after prior route closure records the known commit without
running a late settings callback. Terminal request time is not a guarantee that
all already-admitted callbacks have finished.

`MixerWorker::join` consumes its unique handle and performs a real thread join
without holding mailbox or route locks. The receipt distinguishes ordinary stop,
panic and poisoned state. Tests must inspect that exit classification; `Ok` alone
is insufficient to call an exit normal. Join may wait indefinitely for an
uncooperative backend/settings callback. It proves completion of this local
worker thread, not that native queues are drained, another writer has stopped, or
the last overlay was consumed.

Last-port and worker-owner Drop only set terminal state and signal wake. They do
not block, join, emit a join receipt or prove that a detached worker has ended.
The Drop-body test retains the real handle privately and joins it for fixture
cleanup; it does not change the public Drop guarantee. Spawn refusal closes the
route without constructing a worker handle. The spawn test injects a refusal at
the construction seam and does not claim a real OS resource-failure experiment.

Unknown backend results retain the exact unresolved route request and close
admission without replay. Panic diagnostics retain the worker's reserved value
where available. A backend panic and a settings panic have different possible
native outcomes: settings may panic after a known ACK and cleared unresolved
state. `Panicked` therefore does not classify native commit/non-commit. Retention
is in the surviving in-memory objects; durable process death, reconstruction or
recovery after their destruction remains unimplemented.

## Inherited limits and test review

The producer and route are unchanged from the prior candidate, with SHA-256
`7abf1e4a73cb01a058900c31835936fca2ce01e3d020e92fe709b09bb9ca7817`
and `f2ade716b121f9129b35be26cdc1fa6594f31172eeb9ca70009b38fda9d3d7fd`
respectively. Exact ACK validation and ordered settings callbacks keep their
[prior scope and limits](../emulator-preview-gate/REVIEW.md):

- Backend receipts and a suitable full baseline must come from a future trusted
  adapter. A value, transport-write success or local byte equality cannot prove
  broker ownership or native consumption.
- A malformed positive ACK closes admission and retains the unresolved request,
  while the inherited diagnostic `uncertain` flag may remain false. That flag
  cannot establish non-commit; unresolved work still prevents refresh/replay.
- Only current-key and staged-baseline UI observations use nonblocking lock
  attempts. Diagnostic `inspect`, creation, refresh, close and explicit stop may
  wait. Temporary observation unavailability is not proof of native quiescence.
- Settings callbacks can fail after partial side effects and cannot reenter the
  route/producer. No real file sink, atomic multi-file write, durable rollback,
  authenticated transport or broker-death protocol is supplied here.
- App `on_exit` requests stop but does not own or join the worker. A future live
  lifecycle owner must use the explicit join before relying on local worker
  completion. The current `on_exit` then follows the existing guest quit/stop
  path without joining an owned worker. That ordering blocks safe runtime
  enablement of a future owned adapter until shutdown is coordinated and
  unresolved outcomes retained before handback. Current
  `main=None` has no owned worker to stop or join.

Read-only test review checked deterministic startup barriers, queue-full and
reserved-capacity refusal, stale-generation backlog rejection, concurrent creation
order, terminal clone refusal, pending backend versus settings callbacks, unknown
retention, ordinary/exceptional join exits, idle wake and injected spawn refusal.
The other-bus oracle holds an actual fake backend or settings callback until the
legacy classifier finishes. It executes the classifier and wire/cache helpers;
it is not a live socket-loop or physical-latency test.

One review finding was a fixture assertion that a nonblocking `current_key`
observation must succeed while the worker could briefly hold its mutex. The
author corrected the fixture before its first execution, using controlled worker
startup and a blocking diagnostic for the nonterminal check. This was not a
production lock defect. Subsequent source comments also distinguish reserved
from admitted work and settings panic from unknown commit state.

A separate App review found a production edge case: a successful staged-baseline
observation did not guarantee the later confirmation try-lock would remain
available. Treating either mailbox `Busy` or nested route/producer `Busy` as fatal
closed the generation unnecessarily. The revised App leaves adoption pending
without capturing a key or closing on those two transient outcomes. The first
combined pre-correction source snapshot compiled successfully but was not tested;
it remains a superseded checkpoint, not validation of the revised behavior.
The final confirmation-contention test and its strict control both produced
their expected outcomes in the primary and independent full runs. The test
forces the actual mailbox mutex `WouldBlock` arm. Nested endpoint `Busy` is also
handled defensively; no separate App endpoint-contention schedule is claimed.

The separately executed [dwell counterexample](DWELL-BOUNDARY.md) shows that two
successful full-overlay commits can precede one SPI read, which sees only the
latest value. Its marker bytes are arbitrary synthetic values, not a Browse
handler test. Queue capacity, lifecycle tests, commit ACKs and UI-time `MIN_HOLD`
provide no native minimum dwell guarantee. The focused serial plain/ASan-UBSan
result was independently reproduced exactly; it is not folded into the preview
Rust test counts.


## Independent reproduction

The reviewer inspected the frozen preparation/runner and then executed it once:

```sh
python3 experiments/emulator-preview-transport/run-tests.py \
  --emu-source ../xdj-az-emu \
  --result experiments/emulator-preview-transport/test-result-independent.json
```

The new private export was built with locked, offline Cargo, using no primary-run
source tree, build directory or result logs. Production `cargo check` succeeded.
The compiled crate listed exactly 115 tests; the reviewed set of 37 ran and
passed: 15 App, five legacy-classifier and 17 transport cases. The other 78 were
listed but not executed, including unchanged route/producer suites and tests
outside this changed transport scope.

All 11 source controls compiled and failed exactly their selected test with exit
101 and the intended assertion's source file, line and column. These cover stale
key retagging, untagged mixer fallback, cache advance on failed enqueue, omitted
App stop, fatal transient confirmation Busy, capacity excluding the reservation,
retained unsent work after generation close, nonterminal stop, premature baseline
refresh, a falsely committed unknown result and a falsely normal panic exit.
Compile errors, timeout, a different assertion, unexpected diagnostics or a
changed test inventory could not satisfy a control. The reviewer independently
recomputed each intended assertion location from the retained mutated source and
matched it to the raw test output.

The compiler was Homebrew Rust/Cargo 1.98.1, host `aarch64-apple-darwin`, LLVM
22.1.8. Production compilation retained 32 unused/dead-code warnings because the
owned constructor is inactive; no warning policy was suppressed. This host build
is not Linux ARM64, sanitizer, firmware, audio or physical-timing validation.
The independent execution had no failure, retry, source change or workaround.

| Evidence | SHA-256 |
| --- | --- |
| [Primary result](test-result.json) | `3578c5473e084b02e05067b065d2bad00c0fa1a597c7ee933eae35ad8c16a108` |
| [Independent result](test-result-independent.json) | `138f962a41937c32b47d7ccd2ebe86b00ca327c2f6ccbae7113e4cba620f4362` |
| [Comparison](comparison.json) | `a0e076e0540dbd4b8ccd2ef87ee8172675d57d2de85ea5bba1e518c6917ba12d` |

Every source/input fingerprint, compiler field, inventory, run name, test count,
passed/failed test name and exit code agrees. Only the 25 explicitly enumerated
raw compilation/test log hash fields were excluded; 24 of those hashes differ.
Private logs contain export paths, elapsed times and runtime thread identifiers.
Neither whole-result byte equality nor raw-log equality is claimed. Both result
sets, inputs, full source variants, compiler outputs and logs remain retained.
The serial C dwell evidence is separate and was not added to these Rust counts.

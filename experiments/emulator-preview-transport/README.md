# Inactive bounded preview transport

This successor to [preview-gate](../emulator-preview-gate/) moves owned mixer
requests out of the shared unbounded control-message channel. A dedicated worker
uses a bounded mailbox and the existing exact-ACK route. Legacy traffic has no
owned-envelope variant and its classifier holds no reference to the owned route,
so rejection of untagged bus 2 cannot wait for an owned settings callback.

The new patch is applied after the frozen preview-gate patch on emulator commit
`06fc981707e69982c0554bb508e0c646c005a979`. Main continues to select `None` and
constructs no owned backend or worker. There is no live IPC, ioctl/FFI adapter,
host settings writer or enable flag. The original emulator checkout, all older
experiments and community packages remain unchanged. Runtime dispatch stays
CLOSED.

Patch SHA-256: `d630fd6c7b15b8d0f10f0020a9079833b42f839b8d7c1e84bffdbe833e1f485c`.
The complete [17-file inventory](candidate-pins.json) changes App, controls and
main and adds `mixer_transport.rs`; the other 13 parent files, including producer,
route, config and lockfile, remain byte-identical.

## Lifecycle boundary

The mailbox bound includes queued requests and the worker's reserved request.
Canonical creation and mailbox insertion serialize together; a full mailbox
cannot consume an ID and continue with a missing request. Generation closure
discards unsent entries, retaining an in-flight route operation. A staged
baseline cannot replace queued/reserved work. Every envelope retains the key
captured before UI mutation. No dequeue-time retagging or owned legacy replay is
available.

Terminal stop is separate from generation closure. It bypasses the data queue,
prevents new producer work and wakes an idle worker. Explicit stop also closes
the route and may wait for serialized creation/settings work. Joining consumes
the actual worker JoinHandle without holding locks needed by the worker. Only
that completed join can produce a joined receipt; worker panic is distinguished
from a clean exit. No terminal port can rearm or replace its worker.

Drop requests terminal stop and wakeup, but does not join or prove termination.
A callback can remain blocked. Work reserved/admitted before stop may still
reach the backend; a stop request, empty queue or thread-finished observation
does not cancel it. A joined local thread still does not establish native input
quiescence or release a held kernel overlay. The exact pending/known-commit
rules from preview-gate remain applicable, including its unknown-result limits.

The App still captures a generation before handlers, adopts the complete frame
and UI models, and consumes a later clean release-only update before accepting
input. Its enqueued-frame cache advances only on successful owned mailbox
insertion. Other bus frames retain their legacy representation and replay.
App creation, explicit close and host callbacks are not claimed nonblocking.
Only current-key and staged-baseline observations use nonblocking lock attempts;
diagnostic inspection can wait. Callbacks must not reenter the port, route or
producer. One port family must exclusively manage its route; the `Arc` argument
does not enforce that construction precondition in the type system.

A review found that treating transient confirmation `Busy` as a fatal error
could discard a valid staged baseline. App now retains pending confirmation and
requires another clean update. The regression forces the actual mailbox
`try_lock` failure, proves the exact receipt remains staged, and checks that the
eventual confirmation update still cannot create input. Nested endpoint `Busy`
is also deferred defensively; that separate contention schedule was not run.

## Input timing boundary

Read [the focused dwell analysis](DWELL-BOUNDARY.md) before assigning timing
meaning to a FRAME ACK. The kernel's transaction receipt and a later bus poll
are different events. A native consumer can miss a pressed overlay replaced
before it polls, even if both commits were acknowledged. UI-time `MIN_HOLD`
does not repair that after queue delay. A future commit-to-commit delay alone
would still not prove SDK or native-handler consumption.

## Reproduction and evidence

The full source export and every test variant stay ignored under `private/`.
[Parent pins](parent-inputs.json) bind the frozen first patch and helper. The
successor preparation checks the base, intermediate candidate and final complete
inventory before building. Cargo is locked and offline. Only selected headless
App/control/transport tests execute; main, the actual legacy socket loop, native
application and filesystem settings are excluded.

```sh
python3 experiments/emulator-preview-transport/run-tests.py \
  --emu-source ../xdj-az-emu
```

[Primary results](test-result.json) record successful production compilation and
37 selected actual-crate tests: 15 App tests (including two retained originals),
five classifier tests and 17 transport tests. The full binary lists 115 tests;
78 inherited/unrelated cases are filtered rather than rerun unchanged. All
11 [negative controls](controls.json) compile and fail exactly their selected
functional assertion with exit 101. The runner checks the source filename,
line and column as well as exact counts and names. Compiler failure, a different
assertion, timeout or sanitizer diagnostic cannot count as a passing control.

An [independent fresh export/run](test-result-independent.json) reproduced every
source, compiler, inventory, count, exit and exact-assertion field. The
[comparison](comparison.json) excludes only raw log hashes, whose local paths,
durations and thread IDs differ; the two full result files are not byte-identical.
Primary result: `3578c5473e084b02e05067b065d2bad00c0fa1a597c7ee933eae35ad8c16a108`.
Independent result: `138f962a41937c32b47d7ccd2ebe86b00ca327c2f6ccbae7113e4cba620f4362`.

The controlled schedules include concurrent creation, a backend call held
through stop, blocked settings while the legacy classifier progresses, and a
resolved route whose mailbox reservation has not yet been cleared. Two small
test-only seams force confirmation contention and the post-dispatch bookkeeping
boundary. They are absent from the production configuration. Substantive
transport mutation assertions run after releasing barriers and joining workers.

Rust/Cargo 1.98.1 compiled on `aarch64-apple-darwin`. The 32 production unused-code
warnings from the inactive seam remain visible. These are host tests, with no
Rust sanitizer run, Linux ARM64 emulator execution, audio check or hardware
timing claim. See [independent review](REVIEW.md) and the separate
[serial kernel counterexample](DWELL-BOUNDARY.md); its 20 assertions per plain
and ASan/UBSan mode are not included in the Rust count.

An earlier combined source snapshot compiled successfully but was superseded
before test execution after the confirmation-Busy review finding. Its full
source and log remain private. Final test results apply only to the pinned
successor above; no prior failing assertion or runtime state was discarded.

## Remaining work

This is an in-process transport boundary, not an authenticated remote transport.
Receipt authority, process death, reconnection, bounded callback/deadline policy,
unknown-result reconciliation and native button-consumption semantics remain
unresolved. Legacy traffic keeps its existing queue; the new bound concerns only
owned mixer envelopes. Real host-file persistence and
[owned stopped recovery](../emulator-overlay-handback/RECOVERY-CONTRACT.md)
are still unimplemented. Preserve the original neutral overlay, injected mode,
host bytes/modes and verified stopped process before any guarded restart.

App `on_exit` requests terminal stop, then continues its existing guest Quit/
`boot::stop` path without owning a worker join. This is an explicit blocker to
runtime enablement: a future live owner must coordinate real join, retained
unknown work and guest/broker shutdown before claiming handback. `main=None`
means no owned worker uses that exit path in this candidate.

No emulator/device endpoint, service or physical control was used for this
checkpoint. MODS publication/native menu ownership, stems staging/activation,
USB activation and physical validation remain open. Nothing here is a community
release or a permit to enable the old runtime runner.

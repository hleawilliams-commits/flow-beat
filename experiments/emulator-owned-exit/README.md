# Retain owned lifecycle resources through local exit handling

This inactive successor composes a local lifecycle owner with the pinned preview's
App exit and returning framework-runner paths. It follows the frozen
[joined-session candidate](../emulator-joined-session/). The owned path requests
terminal stop, joins the real mixer worker and retains its non-Clone session
alongside the available guest/supervisor value. Joining supplies no permission
to quit the guest, restore files, release the last owner or restart.

Main still selects no owned port/worker pair. No backend factory, enable flag,
installed emulator, community package or core changes here. Runtime dispatch
remains closed. This does not resolve MODS native delivery, stems activation,
USB activation or physical timing.

## Ownership and returning cleanup

The coordinator accepts a matching port/worker pair. Identity is checked against
the same internal shared owner; rejection returns the inputs rather than silently
detaching a worker or discarding a guest value. An optional supervisor value
means only that this caller holds that value. Its absence does not prove that
the preview successfully attached to an already running guest.

A short owner lock claims the worker once. Stop and real join happen outside
that lock. Repeated calls during joining report that state, while later calls
reuse retained diagnostics. An error from the stop request does not skip the
actual join. A worker cannot initiate its own join: that first-call path is
refused while the owner retains the worker. Other callback reentry, blocking
callbacks and deadline policy still require a separate contract.

The retained session includes the same backend owner, exact bounded ACK and
baseline accounting, and every unresolved envelope from the prior candidate.
Owner-mutex poison before the worker is taken refuses completion and can leave
the phase Running and the worker active. Poison during join does not discard
the completed stop/session result: completion stores it without clearing poison,
and read-only diagnostics expose that flag. No poison outcome alone proves terminal admission or a join.
Copied status is diagnostic; it is not a release or recovery capability. There
is no owned guest Quit/stop or resource-release API.

The App's owned exit path invokes this owner. A mixer port without its lifecycle
owner refuses the legacy teardown path after requesting terminal stop. The
legacy branch remains selected in production. A separate owner remains outside
the App creator closure; the returning runner wrapper invokes its idempotent
cleanup even if no App was created or the App was dropped without on_exit.

## Platform and final-destruction limits

The [static framework audit](PLATFORM-LIMITS.md) pins the inspected cached eframe
and winit sources. An injected runner verifies local behavior when a call
returns. It does not execute the real native event loop. On macOS, Cmd-Q can
enter an already-terminating path where the runner never returns. App on_exit
cannot cancel that termination.

Unix retains a Child and its supervisor stdin pipe. Dropping the final value or
process exit can close that pipe and trigger the existing supervisor stop
policy. The coordinator only retains it through its local boundary; final Drop,
unwind and process death remain unsupported for a live owned session. The
candidate neither leaks resources nor changes platform termination policy.
Windows Launched is only a PID, not the process-scoped job handle, so the real
owned constructor is unavailable there. Synthetic generic host tests are not
Windows lifetime evidence.

## Offline validation

`prepare-candidate.py` exports pinned Git blobs from emulator commit
`06fc981707e69982c0554bb508e0c646c005a979` and verifies nine frozen parent inputs.
It applies gate, transport, joined-session and this successor in a fresh ignored
private directory, checking each inventory. The [patch](owned-exit.patch) is
`b278c09b4db47e341ebc5542386dfa79d185586c905b8c4904c430f2aef51a3b`.
All [18 candidate files](candidate-pins.json) are pinned. App, main and transport
change, and owned_lifecycle.rs is added; the other 14 files, including producer,
route, controls, config, boot and the dependency lockfile, are byte-identical.

```sh
python3 experiments/emulator-owned-exit/run-tests.py \
  --emu-source ../xdj-az-emu \
  --result experiments/emulator-owned-exit/test-result.json
```

Cargo is locked and offline. Production host compilation succeeds with 32
visible unused-code warnings from the inactive seam. The crate lists 138 tests;
only the 12 new tests run: eight owner tests and four actual headless App tests.
All pass. The 126 inherited tests are compiled/listed and filtered, not repeated.
Cases cover mismatched pair rejection without losing resources, returning error
before App creation, a created App dropped without on_exit, repeated App and
return cleanup, concurrent Joining, stop errors followed by real join, retained
post-join poison diagnostics, explicit pre-take poison limits, self-worker
refusal and missing-owner terminal stop. The legacy no-supervisor branch is
also checked. The created-App tests use no real supervisor; synthetic Drop
counters separately exercise optional generic guest-value retention.

All [seven strict controls](controls.json) compile and fail exactly their selected
test at the intended file/line/column with exit 101. They bypass pairing, omit
returning cleanup or App completion, send an illegal owned Quit to a local
channel, discard a joined session under poison, forget the exact stop error or
drop a synthetic guest prematurely. Real worker joins and fixture cleanup occur
before their substantive failure oracles. A timeout, compile error or different
panic cannot pass a control; no self-join/deadlock mutant is accepted as proof.

The [primary result](test-result.json) is
`e9e1fafe9ac95cc677e9e582db49ab19c0690e87b5ec1268a1379d1eb23ee9d5`.
Rust/Cargo 1.98.1 ran on aarch64-apple-darwin with LLVM 22.1.8. No failed test,
retry or source revision was needed during that run. The source review fixed
the post-join poisoned-lock evidence-loss path before freezing this candidate.

The [independent fresh run](test-result-independent.json) passes the same checks;
its result hash is
`5c651e081a5fd7e4ed8e644642bb91d10666a1fec1562c02cd8fcc831dcd67e6`.
The [comparison](comparison.json) matches every non-log field, excluding exactly
17 enumerated raw log-hash fields (16 differ). All eight test-output bodies
agree after only private-path, panic-thread-ID and libtest-duration normalization;
raw JSON/log or full compiler-log equality is not claimed. The [reviewer](REVIEW.md)
independently reconstructed all variants and verified all 14 intended panic
file/line/columns across the two runs without importing the runner's location
routine. Neither run required a failure retry or source change.

No App main/window, socket loop, supervisor process, guest, kernel, real backend,
host settings file or device endpoint was executed. These host tests do not
validate native event-loop termination, Linux ARM64 execution, Windows process
ownership, sanitizers, audio or hardware timing. Raw candidate trees, logs and
binaries remain private. The static framework/source pins were checked against
eleven inspected source files and the frozen lockfile; hashes are not runtime
proof. Owned guest failure-text polling is deliberately deferred rather than
exposing its retained resource through a new mutable accessor.

## Remaining runtime gates

A live integration still needs owned stopped recovery, authenticated backend
and receipt authority, unknown-outcome reconciliation, native dwell and event
ownership, and a reviewed shutdown/termination policy. Settings persistence
needs original values and partial-progress accounting for both host files plus
writer exclusion. A successful callback does not verify file contents or
durability. Bounded ACK diagnostics are neither a full journal nor an original
recovery baseline. No existing runtime, stems staging or publication gate is
reopened by this experiment.

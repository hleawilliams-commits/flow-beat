# V2 writer ownership and recovery checkpoint

**V2 remains disabled for runtime use.** The new offline work establishes two
specific recovery counterexamples and demonstrates pending/replayed preview
input. Passing the earlier byte-preservation checks does not exclude those
writers or restore the controller after an uncertain release. No emulator,
physical deck, service, input, core, package or catalog was accessed or changed
in this follow-up. The last released emulator state was not revalidated.

## Queued writers remain active beyond a quiet observation

The [writer audit and actual-source fixture](writer-ownership/README.md) bind
13 local sources. They pass 58 static checks and 18 rejection controls, plus
17 assertions on exact extracted Rust sender definitions and three strict
executed controls. Independent execution produces the identical result.

The real `std::mpsc` queue drains messages even after its producer disconnects.
The extracted sender retains the last full frame and replays it on reconnect,
including after an uncertain write. Fake transport/sleep/persistence make these
deterministic source-behavior checks; they prove attempted sends, not actual
socket delivery, kernel queue drain or native input consumption.

The reviewed kernel has no exclusive-open or per-file overlay owner. Direct
guest writes bypass the preview socket, and the CRC-prefix parameter remains
writable. The cooperative coordinator lock excludes none of these paths.
Closing the preview may stop its owned QEMU process; stopping the forwarder
also affects LED, level and MOSI output and does not exclude direct writers.
No such process change was performed. A process/socket snapshot or two equal
frames cannot be promoted to enforced ownership.

## Restart does not restore the sticky input overlay

The frozen runner's `while_stopped` restarts EP147 in its `finally` path before
any overlay recovery check. Its fallback can also start the service after a
failed fresh-start attempt. Later `_finish_restoration` restores the host
counter and writes completion flags, but never reads or restores the overlay.
The exact kernel `spi_release` leaves the overlay in place.

The private regression passes **87 checks**: 33 behavior/harness checks plus
54 source/provenance/extraction checks. Independent execution reproduces the
complete result byte-for-byte. It extracts those two Python functions and the
exact C
`ctrl_write`, `spi_open`, `spi_release`, `spi_read` and CRC function. It executes
the unchanged host-counter helper against owned temporary files. Two controlled
cases establish:

- An uncertain BROWSE release can leave the first held frame offered across
  the modeled stop/open/restart, while existing completion flags still succeed.
- A delivered neutral CW release removes the held press but can retain a browse
  counter different from the successfully restored original host counter.

A neutral-release control excludes the held-button finding, and a paired
neutral frame with the restored host counter excludes the counter mismatch.
These distinguish the two failures instead of treating every restart as bad.

Kernel locking, sleeping and usercopy are host stubs over owned buffers.
Process identity, empty-state checks, service operations, selective file
cleanup and the `guest-restored` receipt are mocked. The whole `_restore`
function, native EP147 and complete module/service lifecycle are **not**
executed. An uncertain-delivery outcome is deliberately selected in the
fixture; it is not a new observed timeout. No concurrency or hardware proof
follows. Exact source, extraction, harness and result hashes are recorded in
[the sanitized result](runtime-gaps-result.json); raw frames and scripts stay
private. All 13 fixture/source inputs are checked before fixture import and run
from retained snapshots. The initial extraction failure and original 41-check
result remain retained; the revised 87-check result adds complete input binding
without changing those 33 behavior/harness checks.

This does not invalidate earlier file/core/host-byte restoration evidence. It
narrows what that evidence proves: those checks alone do not establish overlay
neutrality or host/overlay counter agreement at restart.

## An inert V2 planner covers the missing source inventory

Separate private `run_v2.py` and `runtime_prerequisites_v2.py` now check exact
externally authenticated review bytes, canonical source roots, 19 selected
task/transport Python sources, the frozen observer build, and a proposed fresh
scope. They include the previously omitted common/archive helpers, transport,
driver-identity policy, delegated layout and frozen startup predicate.

The focused actual-source suite passes **151 checks across 89 scenarios**, with
86 rejection scenarios and four strict controls. Independent results match
exactly. Missing/swapped/drifted sources, invalid review data, prior receipts,
existing scopes and changed build inputs are refused. All live dispatch,
including recovery, is unconditionally disabled even after a valid plan.

No runtime modules are imported by the planner. The source inventory is not
interpreter/stdlib/native dependency closure or proof of future imported object
identity. Sibling receipt checks supply bounded uniqueness evidence, not a
reservation service. Tests create synthetic scopes and manifests only inside
ignored fixtures. No actual run identity, runtime directory or enabling review
manifest was selected or created. The original runner, helpers and closed
compatibility refusal remain unchanged.

## Next work

Review a separately scoped, emulator-only arbitration route at the final shared
overlay mutation boundary. It must cover existing descriptors, full and clear
commands, CRC configuration and queued generations, while preserving other
buses and forwarder outputs. Producer resumption needs a fresh-state handshake;
ending an exclusive interval must not release an old cached frame. No arbiter
or kernel change is implemented by this checkpoint.

For input-bearing runs requiring a stop/restart, recovery must then be designed
around a verified owned stop: retain the fresh complete original neutral overlay
and original host bytes/mode; verify the pair, restore only where required while
EP147 is stopped, and verify again before a guarded restart. Read-only refusals
and already verified restoration phases need no repeated guest stop or write.
Host-only recovery keeps its own receipt-bound path and does not prove overlay
completion. One narrower
admission option is to require the original host counter to equal the fresh
overlay counter and refuse mismatch before installation. Do not normalize the
baseline to make that condition pass. This option is a proposal, not a new
implemented policy or authorization to write a frame.

Unknown overlay ownership or failed restoration must not fall through an
unconditional restart or a successful release claim. Keep file cleanup,
overlay restoration, host restoration and restart receipts distinct. Lost-owner
and process-termination behavior, saved preview state and queued handback need
explicit treatment before runtime dispatch can be enabled. A later live loader
must also bind the actual imported module identities and preserve recovery
diagnostics without making a fresh installation/input permit necessary.

Only after those requirements pass appropriate source/interaction checks should
a new run, selective backup and bounded compatibility check be considered.
Do not reuse the closed run, repeat its refusal or restore its old snapshot.
Native task provenance/returns/ownership, direct menus, MODS 0.1.3 publication,
stems staging/activation, USB activation and physical timing remain unresolved.


## Subsequent offline ownership candidate

The separate [emulator ownership experiment](../../../emulator-overlay-ownership/) now addresses final mixer mutation and CRC serialization in a default-off kernel patch. It deliberately retains a permanent fence after owner release/close. It has no queued-producer handback, stopped recovery, runtime adapter or installation, so the gaps and closed runner gate above remain applicable. Source, tests and review are recorded separately; no old helper is replaced.


## Subsequent instance/generation primitive

A [separate successor](../../../emulator-overlay-handback/) now adds instance-bound
V2 mutations and same-broker atomic frame/generation rotation after phase one.
It remains uninstalled. Actual-source tests distinguish final old-generation
rejection from two unsupported conclusions: a low-sequence STATE is not
cancellation, and the kernel cannot recognize a stale payload relabeled by a
trusted broker. Preview queue/transport generation tagging, acknowledged baseline
adoption and stopped recovery are still unimplemented. The recovery target must
also preserve injected/idle mode; equal frame bytes alone do not restore it.
Keep the old helpers unchanged and runtime dispatch disabled. The next work is
broker/producer integration, not an unchanged repetition of passing kernel tests.


## Subsequent offline broker / producer libraries

The [mixer-broker candidate](../../../emulator-mixer-broker/) implements local
grant/request accounting and a standalone creation-time envelope gate. Its C
library runs against seven exact extracted V2 kernel methods; the Rust suite
covers bounded queues, delayed mpsc messages, unknown ACKs and release gating.
They are separate tested libraries, with no real wire/FFI adapter, preview patch,
IPC authority or stopped recovery. Tests retain counterexamples for reauthoring
old raw bytes and reversing settings callback order within the same generation.
Next compose the actual queue/cache/reconnect/settings sources with explicit
event and ACK ordering, then implement the owned stopped-recovery transaction.
Keep both kernel candidates uninstalled, old runtime helpers unchanged and all
live V2 dispatch disabled. No new live run or emulator/device action occurred.


## Subsequent inactive actual-preview patch

The [preview-gate candidate](../../../emulator-preview-gate/) now composes the
actual pinned App/control-message classifier with captured generation keys,
complete baseline/model adoption, release-only rearming, typed owned cache
exclusion and exact-ACK ordered synthetic settings callbacks. It is an inactive
in-process trait seam: main supplies no owned backend and host config remains
unchanged. Selected actual-crate tests and strict controls pass offline; no
socket worker, native delivery or real settings writer was executed.

The next transport must bound external backlog and account for delayed native
press/release, callback latency and unknown-result reconciliation. It must not
retag queued payloads or treat socket success as an ACK. Real receipt authority,
host-file effects and owned stopped recovery are still missing. Keep both kernel
candidates uninstalled, all V2 dispatch disabled and old helpers unchanged. No
emulator/device contact or new live run occurred for this patch.


## Subsequent inactive bounded transport

The [transport successor](../../../emulator-preview-transport/) replaces the
owned external mpsc lane with a dedicated bounded mailbox and local worker.
Its bound includes reserved/in-flight requests; full/stale paths close the
current generation and clear unsent work. Out-of-band terminal stop prevents
rearming, while only a completed real join supplies worker-completion evidence.
Drop, empty queues and stop receipts remain insufficient. Confirmation Busy
now defers the release-only UI transition instead of closing a valid baseline.
Selected actual-crate tests and exact-assertion negative controls pass, with
independently matching functional results; main
still selects None and the original emulator checkout is untouched.

The separate actual-kernel serial test proves that two successful commits can
replace the overlay before a SPI read. Its synthetic marker is not a native
button mapping. Commit ACK and UI MIN_HOLD do not establish native consumption
or dwell. A fixed delay alone cannot resolve that ownership boundary.

Runtime remains closed. The real authenticated backend, settings-file writer,
callback/deadline policy, unknown-result query/reconciliation and owned stopped
recovery are missing. App on_exit still proceeds to guest Quit/boot::stop after
requesting stop without an owned join; a live lifecycle must coordinate that
ordering and unresolved requests before enablement. Exclusive one-port use of
the route is a constructor contract, not a type-enforced capability. No device,
emulator endpoint, service, package, core or catalog changed for this successor.

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


## Subsequent inactive retained joined session

The [next local successor](../../../emulator-joined-session/) retains the same
route/backend after a real worker join, plus the latest exact full ACK/settings
disposition, latest acknowledged/released baseline and all unresolved envelopes.
Poison salvage is read-only and remains explicitly flagged. These bounded copied
values are diagnostic, not recovery capabilities or a durable event history.
Stopped/empty does not resolve Unknown; uncertain=false on a malformed positive
receipt also cannot establish noncommit. Dropping the session can close backend
resources without neutralizing the overlay.

Eleven new actual-crate host tests and seven exact-assertion source controls pass,
with independent evidence retained. The 115 inherited listed tests were not
repeated. No App/main, config or installed emulator change occurred. Main still
supplies None and all V2 runtime dispatch remains closed.

The next lifecycle owner must retain both worker and supervisor resources
through real join before considering orderly guest shutdown, covering App exit
and startup/return paths. Process-death supervisor/job policies remain separate.
Real settings persistence also needs original mixer.state alongside the counter
file, exclusion of preview/driver/boot writers and partial two-file accounting;
the earlier recovery contract specifies counter restoration only. There is no
implemented backend/receipt authority, native dwell guarantee, settings writer,
unknown reconciliation or owned stopped recovery. Preserve all earlier evidence
and do not infer a runtime permit from this joined lifetime holder.


## Subsequent inactive exit owner

[emulator-owned-exit](../../../emulator-owned-exit/) now composes paired local
worker/supervisor custody with App exit and returning-runner fallback. Twelve
new host tests and seven strict compiling controls pass with separate
independent evidence. The exact port/worker identity is checked; real join runs
outside the owner lock even after a stop error. Completed accounting survives
post-join owner poison with a visible flag. Pre-take poison remains a refusal
that can leave Running/unjoined work. No status alone permits recovery.

The owned branch has no guest Quit/boot::stop action and holds the session and
optional supervisor value through that local boundary. Main still selects None.
Its final return would nevertheless drop that owner; Unix supervisor EOF,
macOS nonreturning termination and Windows process-scoped job lifetime are not
solved. The Windows value is merely a PID, so the real owned constructor is
unavailable there. App/runner host fixtures do not validate native platform
shutdown, and no guest process or emulator endpoint was executed.

Live dispatch stays CLOSED. Do not attach a factory until final custody and
owned stopped recovery, authenticated backend/receipts, unknown reconciliation,
callback deadlines, native dwell/ownership and two-file host settings restoration
with writer exclusion have appropriate evidence. This local owner does not
restore a native baseline, cancel a queued task or authorize resource release.


## Subsequent offline selected-mode restoration primitive

[emulator-overlay-restore](../../../emulator-overlay-restore/) adds an exact
208-byte tagged request which changes selected raw bytes and injected/idle mode
while revoking the old generation under the kernel mutation lock. Mode 0 rejects
any mismatch from the pinned 152-byte idle template. Owner/run/instance,
legacy fencing and CRC policy survive. Existing operations and STATE layout
remain unchanged. This fills the frozen kernel's mode-0 restoration gap only;
the old broker and runtime runner have no adapter for it.

The focused extracted-source suite passes 148 checks and 11 strict controls per
plain, ASan/UBSan and TSan mode, with 26 static checks and byte-identical
independent results. Strict ARM64 compilation/modpost passes with 36 matched
exports. The experiment retains independent review, build evidence and the corrected draft
fixture-barrier failure. No kernel candidate assertion or stems gate was weakened.
Earlier SPI snapshots can complete after this mutation, and newly retagged old
bytes remain accepted. Selected-mode restoration is not native quiescence or
neutrality; hidden dormant-buffer history is not restored.

Runtime stays CLOSED. A live recovery transaction still needs authoritative
original baseline/process identity, all-producer closure, verified scoped stop,
unknown-outcome reconciliation, both original host files with writer exclusion,
full overlay/mode/host verification and separately guarded restart/adoption.
Lifecycle final Drop/process death, callback deadlines, native dwell and event
ownership remain unresolved. No module installation, emulator/device endpoint,
service action, physical access, package/core/catalog change or old cleanup occurred.


## Subsequent offline retained RESTORE broker

[emulator-broker-restore](../../../emulator-broker-restore/) now supplies a
separate C successor with exact source/mode/payload request identity, independent
restore-ID high-watermark, cached historical receipts and no automatic resend.
A fresh exact source snapshot precedes any attempt; UNKNOWN blocks new work
until the entire expected result is positively observed. Idle flags23 are
valid bookkeeping but cannot be adopted by a producer. Successful/reconciled
RESTORE retires the old grant and stays closed. A same-ID malformed conflict
also closes admission; source review and strict controls cover that ordering.

207 focused assertions and 16 strict controls pass per sanitizer mode, with
clean standalone C compilation and separate ARM64 object evidence. The
entire result matches a fresh independent run byte for byte. No old passing
suite was repeated.
A test-only translation executes seven frozen kernel methods with host shims;
no native fd/wire/FFI adapter or preview integration exists. The original
broker and kernel candidates remain unchanged.

This does not prove native stop, immutable original baseline custody, host-file
restoration/writer exclusion, causal attribution under shared capabilities,
process-death recovery or a guarded restart. Lower STATE remains uncertainty,
not cancellation. Historical cached results do not prove current recovery.
The live runner stays CLOSED; no endpoint, installation, core/package/catalog
change or physical action occurred. The remaining recovery contract above
still applies.


## Subsequent offline native64 adapter

[emulator-native64-adapter](../../../emulator-native64-adapter/) now maps the
frozen RESTORE broker to the exact Linux UAPI with one direct libc ioctl per
operation. A private CLOEXEC duplicate retains the already-claimed controller
reference; attachment verifies every expected STATE field and raw byte. STATE
failures publish nothing, known pinned precommit errors are REJECTED, and
unexpected replies remain UNKNOWN without retry/query/automatic disposal.
Disposal is explicit, consumes before one close and requires external resolved
lifecycle custody. It does not authorize recovery or erase the sticky fence.

137 checks and 13 strict controls pass per sanitizer mode, with strict
standalone compiles and a byte-identical independent result. ARM64 Linux
compilation/shared linking pass with actual Linux headers and inspected musl
imports; artifacts were not loaded or executed. The host fixture substitutes a
serial fd table and syscall boundary around actual adapter/broker and seven
extracted kernel methods. Native VFS/cancellation/fork/concurrency are not
proved. Other duplicate writers, module provenance and acquisition remain
external requirements; no actual preview/FFI or runner integration exists.

Both-file host settings restoration remains unimplemented. The legacy writes
are separate, and an injected-only SettingsSink does not retain per-file
replacement progress or exclude competing writers. A later transaction must
retain authoritative originals and uncertain outcomes without treating two
renames as pair-atomic. Native-stop/process identity, durable custody,
callback deadlines, final Drop/death, guarded restart, event ownership and
dwell remain open. Runtime stays CLOSED. No endpoints, services, installation,
physical access, core/package/catalog or old cleanup were touched.


## Subsequent offline ACK-bound settings pair

[emulator-settings-pair](../../../emulator-settings-pair/) supplies a separate
inactive Unix primitive for the two explicit host file images. It retains exact
original absence/bytes/mode/identity and the current transaction's per-file stages
before operations. One retained directory fd, complete ACK identity/order and
exclusive temporaries bind the operation; partial or uncertain replacements close
new writes and remain inspectable after writer Drop. There is no rollback,
implicit cleanup, native FRAME replay or restoration API. Current images are last
individually verified values and can represent a partial pair.

Production host compilation and 24 focused tests plus eight strict controls pass;
138 inherited tests remain filtered. Actual synthetic filesystem cases cover
partial replacements, drift, panic, custody and ordering. Two earlier fixture
failures remain recorded. Final permission evidence separates actual sticky-mode
attach from copied-metadata setuid/setgid policy; the observed launcher masking
mechanism is still unproven. Fault hooks surround real calls but do not prove
actual kernel partial-write/fsync failures or power-loss durability.

The writer audit identifies independent preview, az-drive and boot/test paths.
No common exclusion is established by this component. A directory fd/nofollow
open/identity check is not a conditional rename, and two files are not an atomic
pair. There is no ACK-derived mapper, real SettingsSink bridge, enforced external
writer/reader exclusion, durable journal or recovery coordinator. Native baseline
and scoped stop authority, both-file/overlay restoration, final Drop/death,
guarded restart, callback limits and native dwell/event ownership remain open.
Main and runtime dispatch remain inactive. All staging/product/runtime gates
stay CLOSED; no endpoint, service, install, physical access, existing core/package/
catalog, cleanup or snapshot was changed.


## Subsequent offline ACK settings mapper

[emulator-settings-mapper](../../../emulator-settings-mapper/) adds a separate
pure projection from complete ACK to counter/analog settings, with explicit
selector/TIME refresh and preservation of supported unselected entries. Strict
bounded parsing refuses ambiguous maps, unknown offsets, prior selector button
bits and incomplete TIME pairs. Full shared analog bytes and the exact full ACK
are retained; this is not receipt authentication or a native-consumption proof.

Production compilation, 27 new tests and 13 strict controls pass with an
independent fresh reproduction; 162 inherited tests remain filtered. The initial
custom-assert diagnostic failure and equivalent fixture repair remain recorded.
No filesystem operation, real SettingsSink, writer exclusion or restoration is
added. Main and runtime dispatch stay inactive. Next compose the ordered
validated callback with mapper/pair evidence and retain failures without native
FRAME replay. The external exclusion, baseline/stop identity, durable custody,
both-file/overlay recovery, final Drop/death and guarded-restart requirements
above remain unresolved. No endpoint/device access or gate opening occurred.


## Subsequent offline retained settings callback

[emulator-settings-sink](../../../emulator-settings-sink/) connects the actual
SettingsSink to the unchanged mapper and pair writer, with one fixed baseline/
selection and internally paired evidence custody. It retains full ACK/source
before mapping, full targets before pair operations, typed failures and armed
panic phases. The first refusal and last accepted plan remain separate. Exact
duplicates are refused before I/O; no native FRAME replay or file repair follows.

Production compilation, 22 new tests and 14 strict controls pass with independent
reproduction; 189 inherited tests remain filtered. Actual route/producer/worker
types use synthetic backend values and fresh private host files. The initial
preparation-anchor failure is retained. A known pair commit with missing bridge/
producer completion stays ambiguous; snapshots are separate diagnostic copies.

The frozen route can adopt a newer epoch after settings failure, allowing another
native FRAME before the closed bridge refuses its callback. A mismatched
constructor/backend baseline likewise refuses too late. Both counterexamples
are tested. Next enforce baseline correspondence and failed-route admission
closure in an owned construction path before native submission. Callback closure
alone is not that native fence.

External writer/reader exclusion, authoritative originals/stop identity, real
backend/FFI, durable custody, actual both-file/overlay restoration, final Drop/
death, guarded restart and native event ownership/dwell remain unresolved.
Main and all runtime/staging/product gates remain CLOSED. No endpoints, services,
installation, physical device, existing packages/cores/catalog, cleanup or
snapshot restoration were changed.


## October 29: fixed-generation route construction remains inactive

The [fixed-session successor](../../../emulator-fixed-session/) now retains the
actual injected backend and original directory before a one-attempt constructor,
then the actual settings bridge and immutable route. Both use the same complete
captured baseline. The wrapper serves that cached grant once and refuses later
baseline requests without querying the backend; a failed or closed route cannot
use a newer epoch to submit another native FRAME through this wrapper.

Production host compilation, 25 new focused tests and 16 exact-assertion source
controls pass with an independent fresh reproduction; 211 inherited cases remain
compiled/listed and filtered. Wrapper-return evidence, producer ACK, settings
bridge and pair completion remain separate. Poison salvage never authorizes
receipt acceptance. Blocked calls retain independently readable evidence, and
actual worker/lifecycle tests retain custody after join or callback panic.

This is historical receipt correspondence and a local immutable-route fence.
Separate constructions, backend-internal aliases and external writers/readers
remain outside it. There is no native backend/FFI acquisition, worker or guest
startup, main wiring, authentication, timeout/cancellation or recovery action.
Final Drop/death, authoritative original/stopped identity, native dwell, full
overlay and both-file restoration, durable custody and guarded restart remain
open. All live dispatch and existing runtime/staging/release gates remain CLOSED.
No endpoint, service, settings/core/package/catalog change or physical access
occurred.


## October 29: retained startup closes only the returned-pair custody gap

The [owned-startup successor](../../../emulator-owned-startup/) composes the
fixed route, actual worker channel and frozen lifecycle with retained supplied
supervisor custody. Exact returned products precede later hooks. Explicit finish
after a tested publication unwind can promote and join the retained pair.
Busy finish remains InProgress without cancellation; ChannelArmed without a
returned pair remains StartupUnproven, and poison can leave retained unjoined
work. Final Drop/death and the unreturned channel interior remain unresolved.

Production compilation, 16 new focused tests and 12 strict controls pass with an
independent fresh run and retained-file audit; 236 inherited tests stay filtered.
Synthetic backend/supervisor values, real local worker threads and private host
files do not establish native ownership or recovery authority. Control-only
custody preserves the sink's duplicated directory capability, not the original
File Arc or startup evidence container.

The [boundary review](../../../emulator-owned-startup/REVIEW.md) distinguishes
the native64 adapter's required guest Linux descriptor from the host preview. A later
integration requires an owned guest broker/transport (or explicit relocation),
complete request/receipt validation and conservative unknown outcomes. A host
FFI wrapper alone cannot bridge that process boundary. Acquisition, external
exclusion, authoritative originals/stopped identity, native dwell, callback
deadlines, full restoration, durable custody and guarded restart stay open.
Main and runtime dispatch remain inactive. No endpoint/device access, service,
settings/core/package/catalog change, cleanup, snapshot restoration or gate
opening occurred.


## October 29: bounded host/guest broker value seam

The separate [broker-wire successor](../../../emulator-broker-wire/) now
implements bounded canonical C/Rust records and full request/receipt identity.
The actual frozen broker is exercised with a synthetic C backend over private
pipes; no guest endpoint is acquired. Cached provenance cannot become a fresh
host ACK. Nonzero API results remain uncertain even beside a positive receipt;
a known earlier commit cannot become a local rejection on duplicate submission.
Armed intent and returned I/O evidence are retained through tested panic/poison.

Production compilation, 20 focused Rust tests/seven strict controls and 5,248 C
checks/five controls per sanitizer mode pass with an independent reproduction.
The retained audit verifies all variants and captures; 252 old tests stay
filtered. These are host checks only. A correlation label does not authenticate
a peer, a copied journal does not retain stream custody after backend Drop,
and a borrowed broker pointer does not exclude aliases or prove native ownership.

Next bind an owned guest acquisition/adapter/broker lifetime to its host transport
and supervisor, preserving unresolved outcomes across failures. Authoritative
originals/stopped identity, external exclusion, native consumption, complete
overlay/both-file recovery and durable custody remain unresolved. Main, live
observer dispatch and all existing gates stay CLOSED. No endpoints, services,
installations, physical access or earlier cleanup were used for this checkpoint.


## October 29: wire/native64 actual-source composition

The separate [wire/native64 integration](../../../emulator-wire-native64/)
now links unchanged C wire/dispatcher, broker and adapter translation units to
seven exact extracted driver methods. Fourteen new scenarios and six strict
controls per plain, ASan/UBSan and TSan mode pass in two fresh matrices. Complete
result JSON is byte-identical. The retained-file audit checks all source and log
inventories; its first declaration reconstruction retained a trailing comment,
was corrected without weakening equality, and then passed. That audit failure
is recorded separately; no tests or captured inputs changed.

The chain verifies full raw and canonical bytes, duplicate-fd identity through
original-number reuse, fresh adoption state, exact modeled EPERM/EFAULT,
UNKNOWN after actual modeled commit plus synthetic EIO, historical cache
retrieval and same-capability alias divergence. A cached response remains
historical and cannot exclude another mutator. These are serial host checks;
native libc/VFS, Rust transport, external ownership and real input do not run.

The [acquisition review](../../../emulator-wire-native64/ACQUISITION-REVIEW.md)
narrows the next implementation gate: retain the original before one-shot CLAIM,
retain exact pre/post state and returns, and separately revise failed attachment
to preserve usable duplicate custody. Current mb_destroy has no observable
success result, so checked teardown remains separate. CLAIM preserves content
and injected mode; matching STATE is neither baseline nor stop authority.
No automatic FRAME may turn idle acquisition into ready input.

No emulator/device endpoint, service, core, package, catalog, settings or prior
cleanup changed. Main and runtime wiring remain inactive. Stems staging,
native ownership, USB completion/cancellation, recovery, audio quality and
physical timing remain unresolved. Preserve all prior source and evidence.


## October 29: retained-before-validation adapter

The separate [retained adapter](../../../emulator-retained-adapter/) allocates/copies an owner before separate
one-shot duplication and STATE validation. Failed validation preserves the held
duplicate, raw syscall return and all 208 initialized/returned bytes. Diagnostic
bytes are not a usable semantic state; full version/shape/identity/counter/mode/
CRC/all 152-byte equality is required before backend admission. Stage repeats
make no I/O and preserve first evidence. The frozen snapshot/submit prefix is
mechanically included byte for byte; legacy public lifecycle is not linked.

Fourteen new focused selectors and nine strict compiling controls per plain,
ASan/UBSan and TSan mode pass in two fresh matrices. Whole result JSON is
byte-identical; the retained-file audit independently verifies source generation,
variants and every recorded log. Both matrices and the audit passed on their
first invocations. No unchanged parent suite ran. Review corrected descriptor
wording, a harness field typo, verifier optimization guards and disposal coverage
before either matrix.

The new disposal case checks explicit owner consumption, held/unheld close
branches, exact raw uncertain close reports and no retry under synthetic
quiescence. Its fallback fixture cleanup is not runtime teardown permission.
The original fd remains borrowed; inspection exposes actual descriptor numbers,
so aliases and external writer exclusion remain unresolved. Same-thread syscall
inspection does not prove blocked/concurrent inspection or durable custody.

Next retain an original fd before one-shot CLAIM, exact pre/poststate and raw
returns, then compose retained adapter/broker/dispatcher ownership through failed
construction. The frozen broker's void destroy still cannot certify graph
teardown. Matching state supplies neither authoritative originals nor native
stop; idle acquisition must never be converted automatically to injected input.

No endpoint, emulator start/restart, install, physical control, settings, core,
package, catalog or prior cleanup changed. Main and observer runtime dispatch
remain inactive; stems staging, USB completion/cancellation, lifetime/race,
recovery, audio quality, native timing and community-release gates remain open
work. Frozen parents, previous evidence and unrelated deck-lights/ are preserved.

# Host evidence: retained worker startup

This checkpoint compiles the pinned actual preview crate and executes only the
new startup composition with synthetic backend/supervisor values, actual host
worker threads and real isolated host files. It never executes main, controls,
an application/window, socket, native adapter, guest or physical device.

## Primary matrix

[host-result.json](host-result.json) records production compilation, **16 passing
focused tests** and **12 compiling source controls**, each failing at its exact
designated assertion. The **236 inherited tests** are compiled/listed and
filtered; **252 names** are pinned in [the inventory](test-inventory.json).

The production configuration compiled with **132 inactive-code warnings** on
aarch64-apple-darwin, Rust/Cargo 1.98.1 (Homebrew), LLVM 22.1.8. This is a normal
host build, not sanitizer, Linux/Windows, ARM64 guest, native integration or
physical timing evidence.

Primary result SHA-256:
`02a220b721145b87d49a4eebbfe8babd783432e4b9ef417743b2af57e0f63a32`.
Source:
`c82c949ec8af5d9d5b3579b2542af3fafab246d33c2c880add7570a0a9ef504d`.
Tests:
`94bb8bb225a2ab6525784658c123d20a74952a277d7fe108d898144d2457cbed`.

## What the focused cases establish

| Area | Observed host behavior |
|---|---|
| Initial custody | Supplied backend, original directory and supervisor survive caller loss while the handle remains. Retain makes no backend query. Finish before startup prevents later startup without a joined receipt. |
| Constructor errors | Invalid configuration, baseline refusal/unwind and the actual channel's injected spawn-error arm retain their distinct evidence. A ChannelArmed hook unwind with no returned pair remains StartupUnproven. |
| Publication | Pair and owner checkpoints observe their actual retained resources. Unwind at either boundary leaves the external handle able to promote the exact pair if needed, then perform an actual join. |
| Ready | The returned lifecycle starts unarmed. Exact baseline adoption and explicit release precede submission. Repeated startup refuses without replacing the first route. |
| Concurrent operations | Finish racing blocked baseline or pair publication returns InProgress without cancellation. Startup may still reach Ready and needs a subsequent explicit finish. Snapshot remains readable while startup or a real backend/join is blocked. |
| Outcomes | An unknown returned backend result retains the exact unresolved request after join. A settings callback panic retains known ACK, pair Committed, bridge PairArmed and producer CallbackArmed separately. |
| Poison | Journal/products poison refuses startup or handle finish. An independently returned lifecycle can still perform its own explicit join. Journal poison during an already-started real join retains its actual completion but the handle reports Poisoned. |
| Last custody | Handle-only and returned-control-only survivors retain backend/supervisor custody. Control-only custody retains the sink's duplicated directory capability: a private directory rename followed by a real ACK-bound write succeeds after the original File Arc expires. Explicit join precedes final-drop/no-cycle assertions. |

Busy finish is not asynchronous cancellation. No returned pair is manufactured
for an unobserved channel interior. The injected spawn-error helper selects the
frozen real error branch; it does not induce an operating-system spawn failure.
Poisoned pending-worker custody without an independently returned control may
remain unjoined; these tests do not invent authority to clean it up.

The files and backend receipts are synthetic. These cases establish no guest
supervisor acquisition, native descriptor ownership, receipt authentication,
external exclusion, native input duration, callback deadline, durable recovery
or permission to dispose of retained resources.

## Controls and captured inputs

[controls.json](controls.json) changes only the new production source in
separate private variants. It distinguishes initial and lifecycle supervisor
custody, exact channel-return evidence, unattempted versus uncertain startup,
pair/owner checkpoint ordering, busy-finish semantics, operation exclusion,
independent snapshots, poison refusal and observed completion retention.

Each control must compile, list all 252 names, run exactly one selected test,
exit 101 with exactly one failure, and end its panic output at the designated
assertion location with an assertion diagnostic. Earlier caught test panics,
compile failures, process timeouts and runtime/sanitizer fatal diagnostics do
not count. Real-worker variants preserve an explicit join before their final
oracle; the blocked-snapshot control releases and joins its threads first.

The preparer verifies 33 frozen inputs and the 26-file parent candidate, then
produces 28 pinned files. The only changed parent files are an inactive Unix
module declaration in main and an appended cfg(test) helper invoking the
unchanged transport spawn-error implementation. No dependency, production
transport body, frozen source or main wiring changes.

Eight public inputs are captured per run. The runner checks captured candidate
bytes against pins before compilation and again after execution. Cargo uses
locked/offline dependencies and a fresh target directory. Inherited AZ/target/
flag variables, encoded/build Rust flags and compiler wrappers are removed.
Full source exports, scripts, logs, binaries and filesystem fixtures remain
private. Source and sanitized result hashes are published separately.

## Independent reproduction and retained-file audit

[host-result-independent.json](host-result-independent.json) repeats the new
matrix once with a fresh private candidate, target directory and fixtures.
Its production check, 16 focused tests and 12 strict controls pass. SHA-256:
`a4cc880e853efe9fd83fdc6c897444844c2737d4c0099c26ebaa7c72ce030cea`.

[comparison.json](comparison.json) records a separate audit of retained bytes.
It reconstructs all 26 variant trees across the two runs, verifies 28 candidate
files, 33 frozen parent inputs and eight captured runner inputs per run, and
checks inventories, compiler completion, exact final assertion locations and
retained log hashes. Removing only the module declaration and appended test
helper reconstructs the frozen 26-file parent. Each run retains 72 regular
fixture files and 36 directories; their exact private bytes and modes are
inventoried without rerunning tests.

Exactly 27 raw-log hash fields are enumerated as exclusions: the production log
and compile/test logs for 13 variants. Of those, 26 differ; the candidate test
output hash is equal. Every other result field is structurally equal. Whole
JSON files are unequal, and no raw-log normalization or general equivalence is
claimed.

Comparison SHA-256:
`f30bcccab28a05f0bc5a9d2b2b830a1a53bee468ad0a10a127369af491920187`.
Private audit-script SHA-256:
`ed953be2a6a9fbe3017655caaa1ee41ffe86511750c1b60c1a14419c06e2183f`.
Private audit-receipt SHA-256:
`ecf25551db3bfcfaa11cb045c993e2dd39fdc4189d1c09e31054431d5e49283f`.

Both matrices passed on their first invocation. No failed preparation attempt
was discarded. These results do not reopen any runtime, staging, native
ownership, USB or release gate.

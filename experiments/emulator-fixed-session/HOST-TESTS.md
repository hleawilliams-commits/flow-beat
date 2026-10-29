# Host evidence: retained fixed-generation route

This checkpoint compiles the actual pinned preview crate and executes only the
new fixed-session tests with synthetic backend values and real isolated host
files. It never executes main, controls, an application/window, socket, native
adapter, emulator or physical device.

## Primary matrix

[host-result.json](host-result.json) records production compilation, **25 passing
focused tests** and **16 compiling source controls**, each failing at its exact
designated assertion. The **211 inherited tests** are compiled and listed, then
filtered; **236 names** are pinned in [the inventory](test-inventory.json).

The production configuration compiled with **113 inactive-code warnings** on
aarch64-apple-darwin, Rust/Cargo 1.98.1 (Homebrew), LLVM 22.1.8. This is a normal
host build, not a sanitizer run, Linux/Windows execution, ARM64 guest build,
native integration or physical timing result.

The primary result SHA-256 is
`ba15dd07093a87ba90f62b89c9a3485d104b793607ac271befbc5574246a6635`.
The source is `c6a325d9f8ccf748bfd14eeccc267da807d7144a6393e2aa23139d16587db418`;
tests are `ac159e7b519fc7b18ba5442d2b8ce0b97b6ce79a9014cf75f74851f7ae6aa35d`.

## What the focused cases establish

| Area | Observed host behavior |
|---|---|
| Initial custody | Retain performs no backend call. Actual backend/directory survive session loss while evidence remains. |
| Construction | Invalid options precede query; errors, malformed baselines, attachment refusal and hook unwinds cannot retry. Published resources survive each tested boundary. |
| Baseline correspondence | Sink, pair and initial route use the same full captured receipt. Changing the synthetic backend's later receipt cannot substitute it. The route stays unarmed until exact adoption and explicit release. |
| One grant | Mapping failure, known rejection, explicit closure, abandoned staging, wrong adoption and delivery unwind cannot obtain another grant or query a newer epoch. The recorded owner refusal distinguishes this from inherited old-epoch rejection. |
| Repeated initialization | A second call after Ready retains original configuration/result and refuses without invalidating the usable first route. |
| Observability | Snapshot completes while baseline, submit or settings callback is blocked. Tests release and join the blocked threads before checking timeout assertions. No operation mutex or fresh file/native observation is needed. |
| Submission evidence | Full request and exact returned Commit/Reject/Unknown are retained separately from producer and settings evidence. Submit panic leaves the exact armed request and backend poison. |
| Poison | Constructor journal/slot poison refuses before query; settings-writer poison refuses Ready. A returned baseline survives journal poison without construction. A returned positive submit survives as diagnostic evidence while Unknown prevents ACK acceptance and settings. |
| Lifecycle | Actual transport/worker/lifecycle joins retain matching route/backend/settings evidence. Callback panic after pair completion retains pair Committed separately from bridge PairArmed and producer CallbackArmed. |
| Late completion | Closing while submit is blocked records a later positive ACK with SkippedClosed and leaves files unchanged; it cannot rearm. |
| Last ownership | Evidence-only and route-only survivors retain actual resources. Final fake-backend drop counters and directory Weak ownership detect built-in cycles. Final release is not safe teardown or restore authorization. |

These cases do not authenticate the fake receipts. They do not test native
driver acquisition, FFI, backend-internal aliases, arbitrary competing processes,
real worker-spawn failure, power loss, callback deadlines or durable custody.
The constructor does not start a worker; the lifecycle cases compose existing
APIs explicitly in fixtures.

## Discriminating controls and input custody

[controls.json](controls.json) mutates only the new production source in separate
private variants: captured grant, consumed-grant marker, preflight ordering,
single construction attempt, returned baseline evidence, armed-before-call phase,
settings custody, route custody, full submit outcome, conservative poisoned
return, snapshot/backend-lock separation, snapshot/settings-lock separation,
one-grant refusal, sink/baseline correspondence, settings poison before Ready
and repeated-initialize preservation.

Each variant must compile, list all 236 exact test names, run one selected test,
exit 101 with precisely one failure and end its panic output at the designated
assertion location with an assertion diagnostic. Earlier caught panics, compile
errors, timeouts and runtime/sanitizer fatal diagnostics do not count.

The preparer verifies 28 frozen inputs and a 24-file parent candidate, captures
the inputs, and produces 26 pinned files. Only an inactive Unix module declaration
changes in the parent files. The runner verifies all captured compilation bytes
before execution and afterward. Eight public inputs are copied into each private
run; full source exports, binary artifacts, logs and filesystem fixtures remain
private. Cargo uses locked/offline dependencies and a fresh target directory.
Inherited AZ/target/flag variables, encoded/build Rust flags and compiler wrappers
are removed by the runner.

## Independent reproduction and retained-file audit

[host-result-independent.json](host-result-independent.json) reproduces the
production check, 25 focused successes and all 16 strict controls using a new
private candidate, target directory and fixtures. Its SHA-256 is
`ba6a3ce730badba911c4e453a593bfce73b5e274988975dfbdc03eaab519aae5`.

[comparison.json](comparison.json) records a separate retained-file audit which
reconstructs all 34 variant trees, verifies 26 candidate files, 28 frozen parent
inputs and eight captured runner inputs per run, checks exact inventories and
final assertion locations, and hashes all retained logs. Each run retained 121
regular fixture files and 62 directories. No additional execution was needed
for that audit.

Exactly 35 explicitly enumerated raw-log hash fields differ: one production log
and compile/test logs for 17 variants. After excluding only those fields, every
other result field is structurally equal. Whole JSON files are not identical;
raw paths, timing and runtime thread identifiers are not normalized or treated
as equivalent.

Comparison SHA-256:
`37e0e591952c5f755bb4da5ab561cf6b71e25fcd268c6c869f53d615717416af`.
Private audit-script SHA-256:
`7af1a66ceb60e9782f10cf6e3887092e7158fce53f842208ff001f36a6e276bb`.
Private audit-receipt SHA-256:
`b8f04698045628570d92797355e161e438a3ea5a57d5c597e0fa98dad851bc43`.

Both final matrices passed on their first invocation. These checks do not reopen
any runtime, staging, native ownership, USB or release gate.

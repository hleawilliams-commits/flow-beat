# Host validation scope

The final primary matrix compiles the actual pinned az-preview crate for
`aarch64-apple-darwin`, then executes only 22 new `settings_sink::tests::`
cases. The 189 inherited cases are compiled/listed and filtered; 211 total
test names must match the frozen inventory exactly. Production compilation
passes with 91 inactive/dead-code warnings, not warning-free.

The test fixtures use actual producer, route, worker, mapper and pair code.
Backend replies are synthetic values. All filesystem activity is confined to
fresh explicitly supplied private directories; files remain retained afterward.
No main/window, socket, emulator/device endpoint, guest service, installed driver
or actual settings path is executed.

The focused cases cover:

- Two queued frames persisted from their corresponding exact ACKs, complete
  transient ACK bytes, fixed optional selection and the prior verified image.
- Unknown, rejected and malformed receipts excluded before callback entry.
- Mapping refusal after a known native commit, partial first-file effects,
  pair errors/panics, bridge intent/completion gaps and explicit poison.
- Inspection while a callback is blocked, using channels and bounded waits;
  the callback is released and both threads joined before assertions.
- Duplicate/key/order refusal before pair calls, retained first refusal and
  accepted plan, drift checks, sequence exhaustion and handle/worker custody.
- Newer-route-epoch rearming and constructor-baseline mismatch counterexamples:
  a native FRAME can commit before the bridge refuses its callback.

The output oracle enumerates the 51 analog offsets independently, formats
expected selected/preserved fields and compares actual disk bytes. Mapping and
pair checkpoints distinguish historical verified images from actual names after
partial failure. Bridge completion and producer callback completion remain
separate assertions.

## Strict controls and runner

Fourteen fresh compiled variants change actual bridge source, never test
predicates. Each executes one exact selected test and must exit 101 at its
designated final assertion: zero passed, one failed and 210 filtered.
Compilation failure, timeout, a different assertion, earlier caught panic or
fatal runtime diagnostic is a failure of the validation run.

Controls remove or alter full ACK retention, the last verified image, retained
targets, fixed selection, terminal/key/duplicate/armed checks, first-refusal
preservation, successful sequence advancement, capacity closure, completion-gap
ordering, lock-free inspection and poison visibility. The lock control is a
bounded diagnostic timeout followed by release/join and a normal assertion,
not acceptance of a hung process.

The new runner verifies its captured compilation bytes against all candidate
pins before building. It requires the last captured panic header to match the
exact assertion source location and the immediately following diagnostic to be
an assertion failure. This closes a substring-only acceptance gap found during
review where an earlier caught panic could otherwise supply the matching text.
No claim is made that such a false pass occurred in a previous experiment.

Preparation pins 23 frozen inputs, verifies the 22-file mapper parent and exports
24 candidate files. Only main's inactive Unix module declaration changes among
parent files. Both scripts explicitly refuse optimized Python before writes,
preserving the inherited preparers' assertion checks. The root verified both
optimized entry points exit 1 without creating a candidate.

## Preserved initial failure

[initial-failure.json](initial-failure.json) records the first invocation, which
exited 1 before parent preparation, compilation or tests. Its redirect substring
appeared twice in the frozen mapper: once in an anchor literal and once in the
actual assignment. The successor now matches the exact indented assignment
line including its newline. The original eight inputs remain privately retained.
Frozen parents, production source, fixture assertions and runner criteria were
unchanged by this repair.

The final primary result is [host-result.json](host-result.json), SHA-256
`423e7984f02c32be96c5e497fb5d97096630b1c50e90eb872baafb8e4e27f5d9`.
The fresh independent run is [host-result-independent.json](host-result-independent.json),
SHA-256 `37106dc2febd2d936ef73345296534fc45f99ef01fec738984bc93a63bc37e52`.
Both runs pass all 22 cases and 14 controls. The retained audit reconstructs both
24-file candidates and all 30 variant trees, eight captured inputs and 23 frozen
parent inputs per run. Each run retains 105 regular fixture files in 53 case
directories.

[comparison.json](comparison.json) compares every result field after excluding
exactly 31 enumerated raw-log-hash fields. All 31 differ; remaining fields agree.
Whole result JSON and raw logs are not identical. Toolchain: Rust 1.98.1 and
Cargo 1.98.1 (Homebrew), LLVM 22.1.8, aarch64-apple-darwin.

Private audit script SHA-256:
`4b061e1dcf8912655f6958b3b34d5fc80402620bc637e0bb90fc4628e5326471`.
Private audit result SHA-256:
`6d4053f76e49bcc78068c813f32878da7debd05c69a918b1e10b8d675919741f`.
The audit reads retained evidence without executing another test repeat.

## Reproduction and limits

```sh
python3 experiments/emulator-settings-sink/run-tests.py \
  --emu-source /path/to/pinned/xdj-az-emu
```

Cargo uses locked offline dependencies and a fresh private target directory.
Full exported sources, fixture directories, logs and binaries remain ignored.
The explicit fixture-root variable is supplied separately to each variant.

These are ordinary host builds/tests, without ASan, TSan or UBSan. Boundary
injection does not reproduce real partial syscall errors, power loss, durable
recovery or concurrent external writers. Successful backend mock receipts
prove no native ioctl, live ownership, native poll/dwell or hardware timing.
Windows/Linux behavior is not runtime-tested here. All existing gates stay CLOSED.

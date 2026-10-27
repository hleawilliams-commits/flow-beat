# Retain the joined mixer session and exact acknowledgment evidence

This inactive successor prevents the local worker's completion from immediately
discarding its backend owner and latest exact acknowledgment evidence. A real
join now returns a non-Clone `JoinedMixerSession` holding the same route/backend
and copied local accounting. It is a separate emulator-source patch after the
frozen [bounded transport candidate](../emulator-preview-transport/).

Main still supplies `None`. No backend factory, enable flag, live adapter,
community package, core or installed emulator changes here. The runtime runner
remains closed. This does not fix MODS native menu delivery, stems recovery,
USB activation or physical timing.

## Exact, bounded accounting

The producer retains one latest validated full `FrameAck`: its immutable
envelope includes the instance/run/epoch/producer key, request ID and all 152
frame bytes; the ACK also retains the exact kernel sequence. It records this
only after matching the pending request and next sequence. Native commit is
recorded before invoking settings, so a callback error or panic cannot turn that
known commit back into work eligible for replay.

The checkpoint distinguishes five callback dispositions:

| Value | Local evidence |
| --- | --- |
| `NoSettingsRequested` | Direct exact acknowledgment, with no settings callback requested by that operation. |
| `SkippedClosed` | Exact late ACK accepted while admission was already closed; callback skipped. |
| `CallbackArmed` | Validated ACK retained before callback; no return outcome recorded. |
| `ReturnedOk` | Callback returned success; file contents and durability are not verified. |
| `ReturnedErr` | Callback returned an error; partial side effects may remain. |

Logical close, a rejected or malformed later receipt and newer baseline staging
do not erase the checkpoint. A later exact ACK replaces it: this is bounded
latest-event accounting, not a full transaction history or durable journal.
Checkpoint keys must be read directly; an older ACK can coexist with a newer
generation's unresolved request.

A separate bounded baseline checkpoint retains the latest exactly acknowledged
full baseline and whether that handshake opened locally after the release
assertion. A merely staged newer `last_key` is distinguishable from that older
receipt. Neither is the original recovery baseline, current native state or
independent proof that physical controls were released.

## Joined owner and diagnostics

`MixerWorker::join` requests terminal stop, joins the real thread without holding
mailbox/route locks, then copies mailbox and producer accounting while retaining
the same internal shared owner. Its exit classification remains distinct from
the native request outcome. Terminal port clones cannot resume the worker.

The session exposes the last mailbox attempt, queued/reserved status, stop/exit
fields, producer state, every unresolved envelope, baseline/ACK checkpoints and
separate mailbox/endpoint/producer poison flags. Read-only salvage preserves
diagnostic fields without clearing poison. An old `admission_open=true` field
under poison is not usable admission. Likewise, the inherited malformed-positive
path can retain `uncertain=false`; the unresolved envelope still must be treated
as unresolved, not as a proven noncommit.

The session's private fields and lack of Clone/public construction distinguish
retained ownership from its ordinary Copy diagnostics. Those values cannot
authorize recovery. Holding the session prevents destruction of its last local
backend owner; dropping it can run that destructor without neutralizing the
overlay. Process death, external route clones and remote ownership remain outside
this local lifetime guarantee. See [lifecycle and host-file limits](LIFECYCLE-LIMITS.md).

## Offline reproduction

`prepare-candidate.py` exports only pinned Git blobs from emulator commit
`06fc981707e69982c0554bb508e0c646c005a979`, verifies the frozen gate/transport
patch inputs, then applies this successor in a fresh ignored private directory.
It verifies each intermediate inventory and all final files. No source checkout
is changed. The runner builds the production host configuration and executes
only the new focused actual-source tests and their strict compiling controls.

```sh
python3 experiments/emulator-joined-session/run-tests.py \
  --emu-source ../xdj-az-emu \
  --result experiments/emulator-joined-session/test-result.json
```

Cargo is locked/offline and requires already cached dependencies. No App window,
application main, socket loop, real settings files, native process or device
endpoint runs. Fake callbacks use owned channels and records. A timeout,
compile error or unrelated failing assertion cannot pass a control. Every
variant's full source, logs and binaries stay private; public evidence contains
source hashes and sanitized results.

## Verified host evidence

The [successor patch](joined-session.patch) is
`4267249c94882120ad9b8e2b37e3894704db95876d51b5c42a10a5f244596cad`.
The [complete candidate inventory](candidate-pins.json) pins all 17 files.
Only producer, route and transport change; the other 14 parent files, including
App, main, config, controls and lockfile, remain byte-identical.

Production `cargo check --locked --offline` succeeds with 36 visible unused-code
warnings from the inactive seam. The crate lists 126 tests. Only the 11 new
cases run and pass: five producer and six joined-transport tests. The 115 inherited
tests are compiled/listed and filtered, not repeated. Cases cover exact ACK field
validation, all callback dispositions, poison, baseline stage/ACK/release
distinctions, old checkpoints beside newer unresolved work, backend retention
after every outside owner drops, terminal clones and last-attempt replacement.

All [seven controls](controls.json) compile and fail exactly the selected test
at its intended source assertion, with exit 101. They remove strong ownership,
omit pre-callback ACK recording, erase ACKs on close, hide unresolved requests
whose `uncertain` bit is false, hide producer poison, promote an unacknowledged
baseline and accept a foreign ACK payload. The ownership control changes Arc to
Weak while preserving the real join; it never leaves a test worker detached.
Source file/line/column, exact names/counts and exit code are required for every
control. Substantive transport assertions follow actual joins and cleanup.

The [primary result](test-result.json) is
`0daa7fe345d41eb5b63520bcde429bd9db06c36adb046bce407c3cb89c9536d5`.
Rust/Cargo 1.98.1 ran on `aarch64-apple-darwin` with LLVM 22.1.8. This is host
execution with injected callbacks, not Linux ARM64 runtime, sanitizer, audio,
native-consumption or physical-timing validation. There was no failed test or
source revision during this primary run.

An [independent fresh export/run](test-result-independent.json) reproduced every
source, compiler, inventory, count, exit and intended-assertion field. Its result
hash is `c19a654b5a6e4e6a67724c38d19ec21bf9aaa8ea6a486f43cf1711185f08593e`.
The [comparison](comparison.json) excludes exactly 17 enumerated raw log-hash
fields, 16 of which differ. All eight test-output bodies agree after only the
declared private-path, panic-thread-ID and libtest-duration normalization; raw
log/result equality and full compiler-log equality are not claimed.
The [reviewer](REVIEW.md) independently reconstructed each mutant and checked all
seven intended panic locations in both snapshots, without importing the
runner's location routine. No failure, retry or source change was needed.

## Next integration boundary

App exit remains unchanged: it requests stop and continues its legacy guest
Quit/stop path without owning a worker join. Startup error, Unix supervisor EOF
and Windows job behavior require explicit lifecycle composition. The next owner
must retain worker and supervisor resources through real join and preserve
unresolved outcomes; this session supplies no teardown or restart permission.

Real settings persistence additionally needs writer exclusion and separately
retained originals/partial progress for both host files. Native dwell,
authenticated receipt authority, callback deadlines, unknown reconciliation and
owned stopped recovery remain unresolved. No old cleanup was repeated and no
runtime, publication or staging gate was reopened.

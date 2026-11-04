# Reader reporter source review

This source review covers the pinned topology and the inactive one-shot
reporter implementation. It is not an executed build/test or an assertion that
the active supervisor behavior has changed. Focused fixture execution evidence
remains separate.

## Exact process topology

The upstream commit is `06fc981707e69982c0554bb508e0c646c005a979`.
Selected source hashes are:

| File | SHA-256 |
| --- | --- |
| `az-preview/src/boot.rs` | `f907da478aa758d4cb2a9f740f529fbe256376230d77635a061a746ce75ec1eb` |
| `az-preview/src/main.rs` | `a13cf052beca509185cb71a56e4196624e0cf6e34d5e62ea43e8fe5f03abecde` |
| `tools/az-boot-m1.sh` | `65b4c3b298c51819ccb6dbed526883bdc9a47ce74e29e096c4eccf1d9b2bc285` |

`boot.rs:10–14` identifies Unix `Launched` as the supervisor Child.
`start_if_needed:50–56` launches this same executable with `--supervise-qemu`
and piped stdin. `main.rs:40–43` enters that branch and exits through
`boot::supervise` before creating App routes. The supervisor separately starts
bash at `boot.rs:73–77`; the script execs QEMU at `az-boot-m1.sh:123`.
Therefore these are two different Child objects in two different processes.

The supervisor's detached reader discards its actual `io::copy` result at
`boot.rs:86–88`. The inner loop treats its unit notification and channel
disconnection as the same stop condition at `103–114`. Poll error instead
returns status 1. The later `wait_exit:141–145` treats poll error as completion.
No app-side admission latch changes those branches or sends an observation back
across the existing one-way stdin pipe.

The recent admission scope borrows the app's retained Child while holding its
existing supervisor mutex and operation lease (`startup-scope.inc.rs:46–58`).
Its normal return closes admission and its journals cannot rebind. A future
continuous consumer must respect that scope rather than repeatedly opening one
scope per message. This reporter creates no such consumer, event bridge or
second Child owner.

## Concrete seam selected

The frozen reader helper already retains the actual returned copy result and
one-shot attempted/unreturned state. It does not own a spawned thread, publish
an event or preserve the reader across a failed spawn closure. A small owner
for precisely that reader and newly created thread addresses this gap without
duplicating existing startup/backend/Child custody.

The design requires retained reader/journal storage before spawn, a private
read-start gate released only after JoinHandle publication, and immutable full
journal publication before unit notification. The gate uses a nonblocking
one-shot release, rather than a zero-capacity send that waits for the reader.
Private channel construction prevents caller injection of arbitrary typed loss
events; it does not authenticate the underlying reader or remote peer.

Explicit join must retain the actual panic payload and must not run from Drop.
Inspection must not take a blocking lock held across `Read`. Diagnostic poison
salvage is separate from permission to read again. The focused checks should
exercise real host reads/threads and label scripted spawn/error/panic seams;
all semantic controls must release and join fixtures before their final oracle.
No failed spawn or read may be reported as replay-safe or as guest disposal.

## Implementation review

The supplied `R` and fresh `ReaderJournal` reside in the owner's shared cell
before the spawn closure is created. The closure captures reader custody through
an additional Arc; dropping that closure after a failed spawn cannot discard the
owner's reader. The returned spawn error is stored intact. Success moves the
actual JoinHandle into the owner, records `Ok(())`, then releases the capacity-one
gate with `try_send`. No user Read or fixture hook can run before handle retention.
The exact original success Result is not retained as a single object.

The worker holds the reader-cell mutex while the frozen helper mutably borrows
both reader and journal. A Read panic leaves that journal in the poisoned cell.
After an ordinary return the complete journal moves to OnceLock; no external
callback intervenes between that move and publication. Only then can the private
unit send occur. A published error remains its original `io::Error`, even if send
fails or a later fixture callback panics. Publication is immutable and not a
notification or join receipt.

`try_report` performs at most one actual `try_recv` per call and records that
return. Empty remains Pending. A returned unit without publication is a typed
protocol error. Disconnection without publication remains ReporterDisconnected;
when publication exists, the completed event and disconnected notice remain
separate facts. The first terminal report is stored once and later calls refuse,
so a later disconnect cannot erase the actual first unit observation.

Snapshot uses `try_lock`, never a reader-cell blocking lock. A busy cell, a
poisoned cell, the independent call-armed flag, immutable completion and join
state are separate samples. Diagnostic poison salvage performs no further read.
An explicit join consumes the sole handle after arming its journal, then stores
the actual returned WorkerExit or original panic payload before classifying it.
No custom Drop, reader extraction, Child poll, port call or process action exists.

Review caught and corrected the test-module filename mismatch before freezing:
the generated sibling and module path now both use `reader_reporter_tests.rs`.
No other operational defect was found in the reviewed custody/publication order.
Allocation abort and process death remain outside the in-memory contract.
Counter exhaustion, self-join refusal and impossible duplicate-publication paths
remain source-reviewed unless separate execution evidence explicitly covers them.

## Preparation and runner review

The new preparer pins 48 inherited inputs and retains the exact 32-file admission
candidate. Composition adds reporter source/tests and one inactive main module
declaration, yielding 34 files. No parent function body or executable main
selection is changed. The fresh Git apply context belongs to the inherited
preparer; it is distinct from the candidate source inventory.

The focused runner captures eight new inputs, compiles the complete actual
candidate crate, and selects only the eight new tests and four strict controls.
Its 267 inherited selectors are listed and filtered. Each variant's executable
is copied into its own private directory and rehashed after every selection.
The unique assertion anchor, message, line/column, failed-test count and exit
status must identify the selected control's final semantic oracle. Timeout,
fixture abort and compile failure cannot count as passing negative controls.

Private process-group exception cleanup and separate failure receipts are
preserved. The inherited preparer can still fail before returning its output
directory to this successor; a missing verified receipt is never a passing run.
No build, selected test, parent-suite replay or runtime operation was performed
as part of this source review.


## Focused fixture review before execution

The eight new selectors were reviewed as source. They use actual private host
threads, Cursor values and anonymous UnixStream pairs; spawn refusal, custom
Read error and Read panic are explicitly scripted. The error selector downcasts
the original error payload and verifies that a repeated receive refuses without
changing the first terminal report or attempt count. The panic selector downcasts
the actual join payload while checking that the retained reader journal stayed attempted and
unreturned. Drop counters distinguish a retained reader from final destruction.

The handle-publication fixture keeps its owner in an outer thread even if its
checkpoint unwinds, releases or closes the private start gate and explicitly
joins before the final oracle. Publication and receiver-loss fixtures release
their checkpoint before join; the latter drops the real private receiver so the
worker's actual send returns failure. The blocked-socket selector inspects on a
separate fixture thread and always supplies EOF before joining, including when
a blocking-lock control misses the early result deadline. This deadline is a
fixture discriminator, not a product timing claim.

Each selected semantic assertion follows resource release and explicit join
where a worker was created. The four reviewed controls move actual gate release before handle publication,
move full journal publication after actual send, misclassify actual send failure,
and replace the inspection try-lock with a blocking lock. They preserve resource
flow and reach those exact final assertions after fixture cleanup. A failure to compile, a fixture timeout or an earlier panic cannot
substitute for them. No fixture execution result is claimed here.

## Subsequent evidence review

[Host evidence](HOST-TESTS.md) records the completed focused matrices. Review of
the evidence verifier found that the runner's final-oracle check alone could
accept an additional earlier fixture panic. The required public
[auditor](audit-results.py) now verifies exact panic inventories and locations,
with four separate parser controls. Both retained matrices passed that first
audit; no source test or build was repeated for this evidence correction.

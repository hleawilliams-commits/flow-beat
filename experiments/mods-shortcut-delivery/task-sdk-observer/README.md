# Encoder task observer — passive SDK candidate

This new `encoder-task-observer` adapter records bounded copied values at the
submission and executor boundaries. It uses the frozen capture contract,
immutable transport and ARM64 wrappers from development commit `5b0f5f3`.
It has **not been installed or executed in the device emulator or physical
deck**. Its host tests call the actual C++ helpers with mocked SDK services and
owned tasks; they do not execute the assembly wrappers or firmware.

There are no USB buttons, direct menu, input consumption, native callback calls
from helpers, RAW filter, stems changes, or completion/cancellation decisions.
Matching task values remain weak candidates. The adapter is research source,
not a released community feature or installation recommendation.

The [action preparation](ACTION-PREPARATION.md) validates separate preflight and
host-recovery components. The later [coordinator checkpoint](COORDINATOR-EVIDENCE.md)
composes them and passes 2,697 offline checks, independently reproduced. A fresh
read-only emulator attempt refused incompatible saved mixer settings before
acquiring a frame or installing the observer. All 147 baseline files and host
state checked unchanged. The separate [complete-overlay V2 policy](overlay-policy-v2/)
now has source-bound parsing, copied-frame construction and private adapter
copies; see its [integration checkpoint](overlay-policy-v2/INTEGRATION.md). The
frozen runtime runner remains unchanged and no enabling source-review manifest
exists. Competing overlay writers and exact V2 runner/recovery wiring still need
review before a new run. These checks provide no runtime readiness or native
task evidence.

## Explicit activation and installation sequence

Initialization requires both of these exact environment inputs:

- `AZ_ENCODER_TASK_OBSERVER_EMULATOR=1`
- `AZ_ENCODER_TASK_OBSERVER_RUN_ID=<fresh nonzero decimal uint64>`

The run string must contain 1–20 decimal digits with no sign, whitespace,
suffix, overflow or zero value. Full consumption is checked. Freshness and
uniqueness are external requirements; this code does not generate IDs from a
PID, clock or previous file. Its one-shot init guard is claimed before any
validation and never reset, including on failure. A second init attempt is
refused without rebuilding observer state.

The adapter checks nonnegative SDK size, ABI and all used callbacks before
calling them, requires exact firmware tag `ep147-9e9454e357389ece`, then checks
two immutable static slots and all four opening instructions of both entries.
These runtime compatibility checks are narrower than hashing the entire
firmware. The separate [binding](BINDING.md) pins the exact offline ELF, caller
constants, selected fields, prologues and unchanged SDK policy.

| Role | Entry | Static slot | Accepted original caller LR |
|---|---:|---:|---:|
| Submission | `0x237a440` | `0x321b138` | `0x1b01e04` |
| Execution | `0x1b029c0` | `0x2d444e8` | `0x237d800` |

There is no synthetic caller remapping in this adapter. Static slots are read
only as compatibility evidence; no executable-segment vtable claim is tried.
Static policy eligibility does not establish that the live SDK will accept
an exclusive hook or find a usable island.

After preflight, init constructs one `Transport<16>` in aligned process-lifetime
storage and release-publishes its pointer while capture remains closed. It
creates the worker before either hook claim. Each claim receives the permanent
original-pointer slot used directly by its wrapper: `az_task_call_original` or
`az_task_exec_original`. Original slots are never cleared, copied through a
temporary, replaced, or unhooked by this adapter. A claim can redirect execution
before it returns; helpers remain gated during that interval.

Only after both claims return success with nonnull originals does init start
the transport and release-publish Ready through a compare/exchange from
Installing. A fatal worker sleep error during Installing changes state to
Failed and closes transport; state checks and the final CAS prevent subsequent
readiness. A check cannot stop an already concurrent SDK claim: any original
successfully installed before or during failure is retained. Refused second
claims leave the first forwarding path installed and capture inactive. There
is no retry, takeover, rollback of instructions, or resource reclamation.

The [lifecycle review](LIFECYCLE.md) distinguishes SDK thread creation from
worker execution, failure callbacks, normal forwarding, and permanent loaded
code. Worker setup success is not a measured altstack, scheduler, native cache,
trampoline or installed-path unwind result.

## Helpers, records and closure

The before helper accepts the unchanged 64-byte `TaskCallContext` plus separate
box/task/original-LR inputs. It ignores box. It clears its private context,
checks observer state and exact caller before TID lookup or task reads, then
passes the task to the unchanged capture contract. The executor helper has the
known task/original-LR ABI and publishes only an entry record. A qualifying
attempt makes one `gettid` syscall; there is no cache or native owner-thread
interpretation. Nonpositive or out-of-range results reject capture before task
reads. Local errno guards cover all helpers and end before original forwarding.

The entry copy requires valid, stable native-owned task storage. Its requested
reads are the vptr plus TaskNo64, ID64, delta32 and cumulative32, excluding the
raw dispatcher field. Null/alignment/vptr checks do not make arbitrary native
pointers safe. The host read seam verifies requested ranges, not the machine
loads performed by the default memcpy/library. No native task, box or
dispatcher pointer is placed in a record. Full native function originals remain
intentionally retained in their separate forwarding slots.

The normal-return helper receives only the private copied context and full
64-bit native result, and never reads the task. It uses the published transport
without requiring Ready, so an earlier admitted submission may publish after
capture closes. It retains no guard, lock or active reference across the native
call. Exceptions that skip after produce absent returns, which stay unknown.
The fixed slots are never reset or reused during the process. Cross-run context
checking does not authorize storage reclamation or reuse of a run ID.

Capture permanently closes when the worker observes any of:

- five consecutive polls with unchanged tick counter;
- 120 eligible worker polls;
- all sixteen reservations consumed, including capture rejections and holes.

These are observation and capacity budgets. With nominal 500 ms sleeps they
are **not guaranteed wall-clock deadlines**, and missing heartbeat is not proof
of core quarantine. Synchronous logging or scheduling may delay polls. A new
tick cannot reopen capture. An entry that already observed Ready/Open may
reserve or publish after close; close never proves quiescence. The worker keeps
scanning immutable slots for late entries and returns after closure.

Fatal sleep errors other than EINTR close capture and stop the worker. An
in-flight after can still publish into retained storage, but no later log is
promised. `worker_cycles` and `worker_exit_reason` expose separate own-module
diagnostics: exit reason 0 means no recorded exit, 1 init failure, 2 sleep
failure, 3 missing storage. They are noncoherent observations, not a proof that
a thread is currently running or all callbacks have ended. Storage and both
original slots remain valid regardless of scanning state.

## Worker output and delivery limits

The sole worker separately acquires each entry and return publication before
copying. It skips holes and revisits them later. Its `entry_copied` and
`return_copied` flags are set only after a successful read and before the void
log call. They mean copied with one log attempt, never acknowledged delivery.
False transport reads leave output untouched and are checked before use.

There are at most 16 entry attempts, 16 return attempts, four diagnostic
attempts and one closure attempt: **37 worker log calls**, plus init's separate
initialization-complete event. The SDK's log callback returns void and can
silently suppress, truncate or fail. No log is retried, and durable delivery
cannot be inferred. Init's event says capture status is separate because the
worker may already have closed capture before that text is emitted.

Record lines contain only copied scalar fields, observer run/invocation IDs,
phase/status or normal-return relation. They contain no native task/box/
dispatcher address. Invocation IDs come from immutable slot reservation, not
TaskNo or temporal event order. The entry and return reads, diagnostics,
heartbeat and transport counters do not form a coherent aggregate. Counts may
lag publication and diagnostic integer counters can wrap. Missing records or
logs remain unknown; equality is not automatic joining or final completion.

## Actual-source host validation

Run `python3 tests/run-tests.py --result <result.json>` from this directory.
The runner extracts the exact pinned upstream SDK header and requires the SDK
copy to match. It checks each copied capture/transport/context/wrapper file
against `5b0f5f3` and records full source hashes. It compiles snapshots of the
**actual observer.cpp** with mocked services, static-memory reads, TID, sleep
and log behavior, and starts a fresh process for each init scenario. There are
no reset hooks in production globals. Raw snapshots/logs remain ignored under
`out-history/`.

The final strict [host result](host-result.json) passes **660 assertions across
41 scenarios per mode**, in plain, ASan/UBSan and TSan. The same matrix was
independently reproduced by root. Its scenarios cover exact opt-in/run parsing;
null core, wrong ABI, negative/short size and every required null callback;
null/wrong firmware; each static slot/prologue mismatch; worker creation and
fatal-before-return failure; first/second refusal or empty original; fatal
worker during each claim; init reentry; and claim-time helpers while gated.

Helper/worker cases cover wrong-caller and closed-state rejection before
TID/task reads, failed/zero TID, exact requested ranges and errno, separate
execution entry, duplicate after, permanent quiet/poll/capacity closure, late
return after close, EINTR then fatal worker exit, logger suppression and bounded
attempts, nested helper/log reentry, unpublished holes followed by later READY
records, and real unmap before value-only after. Eight reserved producer threads
publish through actual helpers while the worker reads before producer
acknowledgements and joins. Reservation order is deliberately scheduled;
payload/return publication is concurrent. These are exercised host paths, not
a whole-process race or timing proof.

Five isolated broken-source controls must produce the exact named failures in
every mode, without sanitizer/crash diagnostics:

| Mutation | Required failures |
|---|---:|
| Clear first original after second refusal | 1 |
| Require Ready for an already captured normal return | 2 |
| Mark an unpublished entry hole copied | 2 |
| Retry already copied entry logs | 1 |
| Read TID before exact caller filtering | 1 |

An initial exploratory run established these same failure sets and is retained
separately; the strict run enforces them. No unexpected host build/execution
failure occurred. The wrapper functions are dummy symbols in this host matrix:
it does not execute native forwarding, generated trampoline bytes, firmware or
ARM64 wrapper instructions. Separate frozen synthetic ARM64 integration evidence
is reused by hash and must not be relabeled as this adapter's installed-path
validation. Hardware timing, asynchronous trampoline unwinding, live caller
provenance, native task/dispatcher ownership and actual log delivery remain
unproven. No core, physical deck, emulator state or release catalog is changed
by this experiment.

## ARM64 build and next checkpoint

From the repository root, the recorded build was:

```sh
python3 experiments/mods-shortcut-delivery/task-sdk-observer/build.py \
  --sdk ../az-mods-sdk \
  --result experiments/mods-shortcut-delivery/task-sdk-observer/build-result.json
```

The existing pinned SDK image runs with networking disabled and receives only
read-only source/SDK headers plus a private output directory. ARM64 compilation
and SDK validation pass for the frozen source. [Build metadata](build-result.json),
[sanitized compiler output](build-log.txt), [compiled-code review](BUILD-REVIEW.md)
and [independent evidence review](REVIEW.md) retain the exact identities and
limits. The binary and raw build/run snapshots remain private. No package,
release-catalog entry or emulator installation was created.

The subsequent [owned-scalar planner](status-layout/README.md) binds eight
named local atomics, 56 bytes in total, to this exact binary. Its 81 checks,
including 54 rejection controls, pass with independently identical result
JSON. Fabricated mappings exercise actual ELF offsets and 4/16/64 KiB pages;
these are offline checks, not process observations. Pointers, transport storage
and record slots are excluded. Sequential byte reads do not create acquire
loads, a coherent state snapshot or a transport-capacity guarantee.

The [fresh runtime plan](RUNTIME-PLAN.md) separates initial capture readiness
from the core's natural startup-guard expiry. Private mock checks exercise
startup, failure recovery and scalar sampling, but the final input adapter is
still disabled. Every mixer frame carries a knob counter, including BROWSE;
the previous RAW-based aligner cannot be reused for this observer. A reviewed
current counter source, one-step host ownership and readiness obtained inside
each action's preflight remain prerequisites. A saved progress pair or an
input-review file's existence is insufficient.

Check fresh loaded-file/process/mapping evidence and actual hook ownership
before any future input; init's text alone is not current capture readiness.
Keep native task storage out of later memory sampling, and do not interpret
missing execution or return output as cancellation or failure. Earlier live
route records and all unrelated runtime gates remain separate. The candidate
remains uninstalled.

[Preparation evidence](PREPARATION-EVIDENCE.md) and its
[sanitized result](preparation-result.json) retain the new mock checks, the
initial missing-`stat` preflight failure and the corrected read-only driver
diagnostic. One valid emulator MISO frame supplies no native task observation
or later input baseline. The independent saved-receipt audit passes 36 checks.

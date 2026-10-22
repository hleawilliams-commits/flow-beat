# Synthetic task observer integration

This experiment combines actual ARM64 assembly, the unchanged
[entry-local capture contract](../task-capture-contract/capture.h), and the new
[value transport](../task-value-transport/transport.h) in a synthetic process.
It passes **239 assertions across 16 scenarios**, four executed negative
controls and two compiled CFI rejection controls. It adds no native hook,
module, firmware execution, emulator installation or physical-device access.

The [result](test-result.json) binds every source/header hash and retained run.
The [independent review](REVIEW.md) records the corrected repeat, its permitted
contention variation and the exact ELF comparison.
The [sanitized output](test-results.txt) includes all expected negative-control
failures. The source fixture and all controls execute in the already available
`azmod-build:18.04` ARM64 image pinned by image ID, using `--pull=never` and
`--network none`. GCC 7.5.0 builds with optimization, warnings as errors,
exceptions and unwind tables. No sanitizer instrumentation is used in this
ARM64 suite; the transport's separate sanitizer evidence remains separate.

## What is integrated

`submission-entry.S` is byte-identical to the earlier
[call/return wrapper](../task-call-return-abi/entry.S), SHA-256
`16c7df29fef3c80fcd5f6259d4073abf92fd57471bc203c39f3853a0709d06c8`.
The runner requires that exact hash in both locations. It does not execute the
old 319-assertion suite again. The capture header is also required to match its
frozen hash; the actual transport header is compiled through its existing
relative include. Compile-time checks compare every context/payload field
offset with the earlier wrapper's 64-byte context. Helpers use `memcpy` to move
between the two named but layout-compatible local C++ structures.

The before helper receives box, task and original LR as separate inputs. It
calls `Transport::begin`, then places only the value context into the private
wrapper frame. The normal-return helper receives that context and full x0 and
calls `Transport::finish`; it has no task argument or saved native pointer.
The capture contract's five requests cover eight vptr bytes and 24 payload
bytes; the dispatcher, tag, padding and tail are excluded. The fixture's
`TC_READ_BYTES` seam counts requested ranges and clears its temporary
thread-local comparison base before the helper exits. This proves the requested
ranges in the instrumented build, **not the machine-load extent of the default
`memcpy` or its library implementation**. Native storage validity and
immutability throughout each copy remain preconditions.

`executor-entry.S` is a separately tested one-input adapter for a future
executor boundary. It receives task in x0, supplies original LR in helper x1,
saves/restores x0–x18, q0–q7 and NZCV/FPCR/FPSR, restores the original SP/LR,
then tail-forwards through an acquire-loaded original pointer. x16 is the
AAPCS64 transfer scratch. Its helper must be `noexcept`, preserve errno and
hold no cleanup obligation requiring a later callback. The fixture deliberately
clobbers errno inside that helper before restoring it, then destroys the
volatile registers and FP/control state in an assembly helper to test the
wrapper's restoration. The native stub's return is forwarded without assigning
it any executor meaning.

The new executor object has 46 instructions, one helper BL, one final BR, a
320-byte frame and LR at CFA−168. Its selected CFI rules and instruction shape
are checked in [disassembly](executor-disassembly.txt) and
[CFI metadata](executor-cfi.txt). A live helper backtrace reaches the wrapper and
synthetic caller. These checks cover this object and the tested synchronous
unwind path; they are not a general CFI-state proof.

The submission wrapper retains its earlier limitations: a 416-byte frame,
known two-register input/full-x0 result, no native stack-argument contract,
changed native SP/LR, and x16 transfer scratch. Its original pointers are
published non-null with release stores before synthetic calls. This is not a
proof of the SDK's original-pointer publication, island/trampoline unwind
registration, concurrent detachment or module teardown. See the separate
[hook lifetime review](../task-hook-lifetime/).

## Caller provenance is a fixture seam

`fixture.S` uses ordinary BL instructions and properly described stack frames.
The labels immediately after those BLs are the actual incoming LR values.
The helpers assert/retain those values in synthetic test records. Only the
recognized submission/executor callsite label is mapped, inside the fixture
helper, to the corresponding fixed caller constant consumed by `tc_begin`.
Unknown callsites map to rejection; the register stress call is also rejected
before any synthetic task read. Neither assembly wrapper rewrites native LR
to impersonate a firmware call.

This mapping tests the connection between ABI capture, exact-LR rejection and
value copying. It **does not establish firmware caller provenance**, native
object type, task lifetime, queue admission, execution completion or an event
identity. The synthetic task has the expected vptr bits and selected scalar
layout but is owned entirely by this fixture.

## Exercised behavior

- Submission entry is independently readable while native submission is still
  active and no return record exists. The native stub invokes the executor
  adapter before returning; its entry becomes readable before the submission
  return. Submission and execution have different observer invocation IDs and
  only a weak same-values candidate relation.
- One owned task page is copied at both entries and then actually unmapped by
  the original submission stub before the after helper runs. Its return record
  uses the pre-copy only. Full-width, zero and mismatching TaskNo return bits
  preserve their distinct weak relations.
- Wrong caller, null task, wrong vptr, disabled capture and exhausted capacity
  preserve exactly-once native forwarding. Disabled, wrong-caller and full
  attempts read no task bytes. Wrong-type capture reads only the vptr.
- Sixteen reservations remain immutable. Closing during native execution
  rejects entry attempts that observe Closed, while a begin that already saw
  Open may still reserve and publish. An already reserved normal return may
  also publish. Close is not quiescence or reclamation, and restart is refused.
- Nested native calls and reentry from both helpers preserve each invocation's
  private values. One executor exception at the deepest native call unwinds
  three submission invocations and five expected destructors, with zero after
  callbacks for those three invocations. Their entries remain readable and
  missing returns stay unknown. A subsequent normal call succeeds.
- Eight native submissions run across four threads. One-CAS contention may
  drop captures; checks require captured plus contention to equal eight,
  preserve native forwarding/results, and verify every claimed entry/return
  pair and unique observer invocation. Requested reads equal five per captured
  entry. These final counts are inspected after all test threads join; they do
  not make running counters a coherent snapshot or prove native engine races
  absent. The retained final run captured all eight.

There are 45 submission helper/native calls, 42 normal-return callbacks and
eight executor helper calls. Seven executor calls use the C++ target counter;
the eighth is the separate assembly register-capture target. Rejected
observation never suppresses the synthetic native call. Fixture instrumentation
and its synthetic addresses are test-only, not a deployable native observer.

## Controls and retained development

The runner requires exact failure names and counts, normal exit status 1, the
same fixed assertion/call totals and absence of sanitizer/crash diagnostics:

| Deliberate mutation | Required failures |
|---|---:|
| Pass native x0 in place of original executor LR | 12 |
| Omit executor x0/x1 restoration | 3 |
| Truncate the observed submission return to w0 | 6 plus the run's captured concurrent records (14 here) |
| Fail to restore errno in the executor helper | 2 |

Missing executor LR CFI and a wrong CFA size compile successfully but are
rejected by the metadata checker; neither malformed-CFI object is executed.
The original submission file is mutated only in private test copies for the
return-truncation control. No earlier experiment is edited.

The first candidate passed 237 assertions. Two checks were then added for
actual mid-call entry visibility/return absence, producing 239 assertions.
A separate exploratory control run established the discriminating failure
sets before the runner froze exact expectations; its result is retained
privately and is not substituted for the final strict run.

Independent root source review subsequently found a fixture race: the
`Guard` destructor incremented one plain `unsigned destroyed` counter from
all four concurrent native-submit threads. Earlier author and root runs
passed their assertions, but that undefined C++ behavior invalidates their
concurrent fixture evidence. Those runs are retained and explicitly
**superseded**, including the prior author final run
`3593f94a-d6be-4d26-9908-b0e1b4a73972` and root repeat
`ede96c92-cc05-4a9e-a63a-36b94a42dfb9`. This was a source-review finding,
not a sanitizer report or an observed assertion failure.

The correction changes only that fixture bookkeeping counter to
`std::atomic<unsigned>`. Neither wrapper, the capture header nor the transport
source changed. The current strict result is from the corrected source;
its metadata records the superseded runs and review correction. Pre-fix
source, runner, README and public reports were additionally snapshotted in
ignored `out-history/review-counter-race-before-fix/`. Raw logs, objects,
binaries and immutable run inputs remain in ignored `out-history/` directories.

Run the new suite with:

```sh
python3 run-tests.py
```

No native hook is installed and no asynchronous island/trampoline unwind,
native allocation lifetime, production copy timing, final reader ownership,
quiescence/reclamation or physical timing is established. Missing records and
matching values do not imply cancellation, admission or completed handling.

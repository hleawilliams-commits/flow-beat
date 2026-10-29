# Independent review: offline broker RESTORE

No blocking defect was found in the final source within its declared immutable
backend, sole-mutator and externally synchronized lifetime contract. This review
covers the new C broker/header, the focused fixture, runner, control mutations and
manual inventory. It grants no installation, live restoration or teardown approval.

Reviewed source pins:

| File | SHA-256 |
| --- | --- |
| `broker.c` | `f331713ac361f265b37c9e781f85400691195915076ca2604aeb2311fd12cbb7` |
| `broker.h` | `04efb6b1eb22e56798837ae8ff72514a5a513da2d616215d77d4c8f6a85033c8` |
| `test-broker.c` | `30060a7700816b00f45d23794f3fd2c5d046d628fff77390e20fab08395c60d2` |
| `run-tests.py` | `d4355548f4003e844756a3c248b3e4f1afe8e3d1eaba1b2599a760d3a0360df2` |
| `controls.json` | `d8796a5d7f5c6c677531aa281b39450c575b53cc5598e0f10a13cbe6675ff1e7` |
| `test-inventory.json` | `e75d20edfd35153de8728e584d1e2cdbc471f160abf1cac30d08db032bf41885` |

## Source findings and correction

Before the first test matrix, independent source review found that source/mode
validation preceded the retained same-ID comparison. A malformed mode or source
conflicting with the last RESTORE would therefore return INVALID without closing
a separately reopened grant. The documented conflict contract required CONFLICT
and closed admission. The author moved new-request validation after the retained
identity check under the mutex. The final two strict controls reproduce each old
ordering separately and require the intended conflict/closure assertion to fail.
This was a pre-test review correction, not an explanation inferred from later
passing repeats. No runtime observation was involved.

The final input copy precedes output clearing, supporting the tested nested
input/output alias. Identity includes every semantic source field, selected mode
and all 152 target bytes; padding is excluded. A fresh request requires closed
admission, no UNKNOWN, the next restore ID, full source equality, capacity, and a
fresh exact snapshot before one callback. Callback attempts consume restore IDs
for COMMITTED, REJECTED and UNKNOWN outcomes. FRAME/ROTATE do not reset that
separate high-watermark. Local refusal preserves the valid receipt.

Retained exact duplicates are historical queries. They precede stale-source and
capacity checks and perform no backend access or admission change. A duplicate
after explicit new adoption does not close that grant; a conflicting duplicate
does. A later exact duplicate does not reopen it. Only the last receipt is cached;
an overwritten old ID is refused, not reconstructed or silently resubmitted.

Flags23 and31 remain distinct even when selected bytes match. Producer adoption
requires31; FRAME and ROTATE explicitly predict31. RESTORE predicts23 or31 from
the explicit mode and retires the old grant on commit. Mode0 template validation
is performed by the actual extracted kernel handler, not guessed by this broker.
With UNKNOWN, only exact complete expected post-state reconciles a commit and
leaves admission closed. With no UNKNOWN, reconcile returns its existing receipt
and STATE error before changing admission. Snapshot equality relies on truthful
callbacks and exclusive custody; it does not independently establish causation.

## Fixture and independent execution

The fixture compiles the actual broker implementation with seven exact methods
from the frozen kernel RESTORE source. Its adapter explicitly translates FRAME,
ROTATE and RESTORE to their respective native64 wire structures; unknown operation
values are rejected. The full kernel is not executed. Usercopy, locking and RNG
use the pinned owned-memory/pthread shims. A separate strict translation-unit
compile checks the production broker without fixture unused-code suppressions.

Manual source review confirms 207 counted assertions:
28 mode/baseline, 44 identity/drift, 3 reopened-cache, 87 uncertain-outcome,
17 rejection/malformed-response, 5 capacity, 7 concurrency, and 16 selectable
single-oracle checks. Setup assertions, thread creation/join errors, timeouts,
compile diagnostics and sanitizer reports are failures, not accepted controls.
All 16 mutations have unique exact source anchors and select their own named
functional oracle. Each accepted control requires exactly one check, the one
expected failure label, and exit1; a compile error or unrelated failure cannot
satisfy it.

The held-callback schedules launch duplicate/conflicting contenders while the
first callback holds the broker mutex. The contender's start flag is set before
entry, so it does not prove that the contender has reached the mutex. Both threads
are joined before final receipt/counter checks. The caller-mutation case starts
mutation only after the callback barrier proves the broker copied the request;
it does not authorize concurrent mutation during the initial copy. Callback
counters are read after joins, and assertion bookkeeping is on the main test
thread. Synthetic pending commands are copied values, not live outstanding
syscalls or evidence that destroying an uncertain live backend is safe.

After the primary pass, one fresh independent run completed all 207 candidate
checks in plain, ASan/UBSan and TSan, all 16 strict controls in each mode, and three
clean standalone compiles. No parent passing suites were rerun. The complete
[test-result-independent.json](test-result-independent.json) is byte-identical to
[test-result.json](test-result.json), SHA-256
`8afc621df2f38ccd74e0d0ee4c7a5f542fe30c01015a42174e6903ff5fc4c217`.
No fields or raw-log hashes were excluded or normalized.

A separate retained-byte audit imported no runner or extraction helper. Across
both snapshots it independently reconstructed 14 kernel function instances,
shared declarations and shims, all 34 broker variants, and checked all 102 exact
test outputs plus 102 empty fixture compile logs and six empty standalone compile
logs. It also checked current/captured source pins, frozen inputs, prepared kernel
files and applied-patch hashes. [comparison.json](comparison.json) records those
checks and the private audit script hash. Raw source exports, scripts, binaries
and logs remain ignored under `private/`.

## Remaining boundaries

These results do not provide a live fd/wire adapter, durable request history,
original-baseline authority, stopped-native proof, native consumption, host-file
restoration, writer exclusion, guarded restart or a recovery permit. Enlarged
semantic structs require separately compiled consumers; they are not the ioctl
ABI. Callers can reauthor old bytes as a new request. A replacement broker cannot
recover a lost uncertain command from STATE alone. Callback blocking, callback
truth, destruction after external quiescence, same-fd custody and final process
lifetime remain external obligations. All runtime gates remain closed.

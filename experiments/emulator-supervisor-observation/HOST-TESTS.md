# Host observation evidence — November 4, 2026

Two fresh private matrices passed their first invocations. Each compiled the
production observation library and all five test variants, then ran eight
positive selectors and four strict controls. The first matrix also passed
locked/offline production compilation in the actual pinned az-preview crate.
The independent repeat deliberately omitted that already-passed crate check.
No previous suite was executed.

The [sanitized receipt](result.json) and [source review](REVIEW.md) delimit this
checkpoint. The independent byte audit passed on its first invocation. There
were no failed build/test attempts, fixture emergency cleanups or timeouts in
these matrices. Source, candidate, compiler versions and selected outcomes agree;
seven of 25 common raw logs differ in private paths, process/thread identifiers
or execution timing. A separate normalized comparison preserves all semantic
values and confirms those are the only differences. The first run
has 26 logs because it also contains the actual-crate check. Raw logs are not
claimed identical.

## What executes

A private sentinel /bin/sh child writes READY and remains alive until the test
fixture explicitly writes its G command. It then exits naturally with code 23.
The fixture calls actual Child::wait before each final semantic assertion. A
child is retained before fallible readiness work. Normal tests leave stdin on
the actual Child; Running and EOF also check its preservation. Only the
natural-exit test detaches
the command handle so it can send G while the observation borrow remains live.

The production observation code never commands this exit. An emergency fixture
kill/wait prints FIXTURE_ABORT and is rejected by the runner. A timeout kills
only the newly created private process group, retains its output, and fails the
run; it cannot count as a negative-control pass. The five-second fixture limits
are host test bounds, not production or physical latency evidence.

| Selector | Evidence |
| --- | --- |
| Running | Actual try_wait returns None; exact child, stdin and caller ownership remain. |
| Reader EOF | Actual io::copy reads seven bytes from an unnamed socket then EOF; one-shot receipt and Hold retain the live child. |
| Reader fault/unwind | Scripted raw error and caught panic remain distinct; no invented byte count or second copy. |
| Reporter disconnect | Actual mpsc sender drop produces Disconnected; first loss and Hold persist across later observations. |
| Natural exit | Actual child code 23 is retained; later polling refuses without invoking its callback. |
| Poll error | Scripted custom error payload remains downcastable after a later actual Running poll; it is not an exit receipt. |
| Poll unwind | Scripted panic immediately leaves armed/Hold evidence; the next poll refuses without replay. |
| Rebinding | The same journal refuses both the original and another actual child; both remain available for explicit fixture cleanup. |

The four private mutations omit loss Hold, misclassify a returned poll error as
exit, allow an unreturned poll to replay, or allow journal rebinding. Every
mutation compiled and failed exactly its named final post-cleanup assertion,
with exit 101, the expected source location/message, one failed selector and no
fixture abort. Compiler failure, timeout or an earlier unexpected panic does not
satisfy these controls. Their exact anchors are in [controls.json](controls.json).

## Provenance

The preparer exports all 13 regular Git blobs of az-preview at
06fc981707e69982c0554bb508e0c646c005a979 and verifies their hashes. It copies the
existing pinned dependency lock without updating dependencies. The resulting
16-file candidate changes only boot.rs by adding an inactive Unix module
declaration, and adds the lock, observation source and test source. The legacy
supervisor loop remains byte-for-byte unchanged after removal of that declaration.
No main selection or prior OwnedStartup composition is included.

Each run captures eight experiment inputs plus the retained lock before
compilation. Rust/Cargo 1.98.1 Homebrew, LLVM 22.1.8, aarch64-apple-darwin are
recorded in the receipt. Across the two runs there were one actual-crate check,
two standalone production-library compilations, ten test-variant compilations
and 24 selected test executions. These were plain safe-Rust host checks, with
no sanitizer or target-runtime claim.

A separate audit reconstructed the exact mutation sources, verified all 16
prepared-file hashes and 13 upstream hashes per run, checked every captured
input, reviewed the compiled inventories, and checked all 51 retained log
hashes and exact negative-control oracle locations. It also sampled the built
library/executable hashes after execution. This is retained-byte corroboration,
not durable or adversarial attestation.

Reproduction from the repository root, when the pinned emulator source and
cached host dependencies are available:

    python3 experiments/emulator-supervisor-observation/run-tests.py --emu-source ../xdj-az-emu

For the focused independent repeat, add --host-only. Successful unchanged cases
do not need further replay. Partial/failed preparations are not passing results;
both verified preparation and passed execution receipts are required.

## Remaining boundary

This module is available to compilation but no active supervisor uses it.
Hold changes no admission policy. Controller dispatch, composition with the
retained startup/exit owners, process stop authorization, guest service framing,
external writer exclusion, uncertain teardown and durable recovery remain open.
The reader can block indefinitely; scripted faults do not prove real OS failure
behavior. Counter exhaustion is source-reviewed only.

No running emulator, native endpoint or physical deck was accessed. No core,
settings, package, catalog, installation, release or earlier rollback changed.
The observer, stems staging, USB completion/cancellation, native lifetime/race,
audio-quality, recovery and physical-timing gates remain closed.

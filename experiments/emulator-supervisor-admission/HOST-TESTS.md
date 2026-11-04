# Focused supervisor/admission composition evidence

Date: 2026-11-04. Scope: offline host fixtures and an inactive source composition.
Both fresh matrices passed on their first execution. No production emulator
process, physical device, guest service, native backend, firmware or music was
used. Existing source suites were compiled but not executed.

## Executed checks

The first matrix ran one locked/offline production-configuration cargo check,
five test-binary compilations and eleven exact single-selector executions.
The second used fresh preparation/build/fixture directories, omitted the
already-passed production check, and independently checked the concurrent
fixtures and controls. Each compiled inventory had 267 tests; only seven new
selectors ran positively and four strict controls ran negatively. The 260
inherited suite tests stayed unexecuted; each exact execution filtered 266
other tests.

| New selector | Observed property |
| --- | --- |
| known_loss_closes_real_admission_before_poll | A supplied EOF closes the exact real port before polling; a clone refuses enqueue while the retained actual child remains live with stdin. |
| returned_poll_error_closes_real_port_without_child_stop | A scripted poll error preserves its raw code, closes port admission and does not become an exit or child-stop receipt. |
| poll_unwind_closes_real_port_before_escape | A scripted poll panic leaves armed/unreturned polling evidence and closes admission before an inner catch regains control. |
| in_flight_and_queued_work_remain_accounted_after_loss | One dispatched request receives its late ACK/settings result; one queued envelope is discarded and future admission is terminal. |
| reserved_request_is_not_forwarded_after_observed_exit | The cfg(test) barrier holds an actual reserved envelope before the existing terminal check; observing actual fixture-child exit prevents forwarding. |
| settings_callback_does_not_block_terminal_admission_latch | Admission latches while a real private settings callback is held, independently of its later completion. |
| scope_return_and_unwind_keep_startup_child_and_exit_owner | Normal/unwind scope exit retains the same owners; a separate pre-entry poisoned-child case refuses without polling/callback and latches the known real port. |

Each semantic oracle follows explicit worker finish and an actual wait for the
retained sentinel child. Private sentinel stdin requests exit 23; it does not
model an authorized native stop. Emergency kill/wait exists only for fixture
failure and emits FIXTURE_ABORT, which the runner rejects. Neither passing
matrix used that path. The mutable-child accessor is cfg(test)-only fixture inspection, fault injection
and cleanup; it does not clear the poison marker or authorize operational recovery.

The in-flight case deliberately waits for actual settings completion before
explicit finish, so it demonstrates a permitted late write rather than racing
producer closure. The reserved case uses a distinct pre-dispatch barrier.
The blocked-settings control releases its barrier after a five-second missing
result, then joins/waits before the final assertion; its barrier deadline is
fifteen seconds and the outer process deadline is forty. These are fixture
bounds, not hardware or product-latency guarantees.

## Strict controls

| Mutation | Exact final-oracle outcome in both matrices |
| --- | --- |
| Omit the known-loss pre-poll latch | Exit 101 at supervisor_admission_tests.rs:236:5 |
| Omit the returned-Hold latch | Exit 101 at supervisor_admission_tests.rs:263:5 |
| Invert the poll-unwind latch guard | Exit 101 at supervisor_admission_tests.rs:287:5 |
| Substitute blocking request_stop for the atomic latch | Exit 101 at supervisor_admission_tests.rs:413:5 |

All four variants compiled and reached their named final assertion after
fixture cleanup. Compile failure, timeout, barrier abort or a different panic
cannot count as a successful control. The runner verifies each explicit unique
assertion anchor, message and exact line/column from that variant's source.
A control that admits work can produce additional scheduled effects; identical
raw failure output is not required for the same proven ordering violation.

## Captured provenance

Preparation verifies 40 frozen inputs, preserves the exact 28-file parent
candidate and composes 32 files. Eleven experiment inputs are captured for
each run. Only three inherited files change: main module declarations, appended
StartupHandle<Child> scope, and the transport latch plus cfg(test) reservation
seam. Four files are added: admission source/tests and frozen observation
source/tests. The production check compiles without the reservation seam or
test-only mutable-child accessor.

Every variant's source hashes and copied executable hash are retained before
execution. The copied executable is rechecked after each selector, so later
Cargo cache writes cannot replace the evidence. The first matrix retains 24
logs and the repeat 23. Raw logs, exact fixtures, parent trees, compiler output,
test binaries and process identifiers remain private. Public hashes and
outcomes are in result.json; agreement means identical selected outcomes and
assertion locations, not identical raw paths, PIDs, durations or binary bytes.

Rust/Cargo 1.98.1 (Homebrew), host aarch64-apple-darwin, LLVM 22.1.8.
Compiler work is locked/offline with a minimal environment and private target.
Exceptional command cleanup kills only the newly created private process
group and retains failures. A failure inside the inherited preparer before it
returns can precede this successor's receipt; absent/pending evidence is never
a pass. No such failure occurred in these two invocations.

## Independent byte audit and retained audit failure

The corrected independent auditor verifies all 40 parent captures, 28 original
files, 32 composed source files and 11 experiment inputs per matrix. It also
checks all ten copied executables, 47 logs, 22 selected execution outcomes,
exact four source mutations and final oracle locations. Independent source
reconstruction confirms main changes only by module declarations and the
reservation edits are all cfg(test)-only. Each matrix has thirteen private
fixture directories and twenty-six retained regular settings files.

The first auditor incorrectly treated the complete main preparation tree as
only its 32 candidate source files. It encountered two additional files from
the frozen template-free Git apply context: .git/HEAD and .git/config. That
audit failed with 'retained main candidate differs'; its script and failed
receipt remain preserved. A separate corrected auditor requires those exact
two metadata files and their bytes in addition to every exact source file.
It excludes no arbitrary path and reruns no build or test.

Corrected audit script SHA-256:
d14b139e2d6e75318829f5522ebd348a3bdd91b20142561f9398b747a9f3bdfb.
Corrected audit receipt SHA-256:
20f053be8dddff8d01db10cc3902f3c5b5d5611cba7ffa43dd6ec93a525033fd.
The prior failed script/receipt hashes are retained in result.json.

Eleven of the 23 common raw logs differ. The audit verifies matching input,
source, toolchain and semantic receipt fields with explicit exclusions for
raw-log/binary hashes and the intentionally omitted repeat production check.
It makes no raw or normalized-log equality claim. Post-execution byte hashes
corroborate retained artifacts; they are not durable or adversarial attestation.

## Remaining limits

Source review repaired unresolved latch replay, known-owner access refusal
leaving admission open, interrupted command cleanup and overly rigid control
oracle parsing before the first execution. No failed execution was erased or
counted as a pass. Only the poisoned-child entry-refusal arm is exercised here;
other typed refusal arms, counter bounds and impossible/nonterminal latch
returns remain source-reviewed. No sanitizer or whole-runtime race claim
follows from these host checks.

The observation callback remains caller-driven and its events are not
authenticated by this adapter. Main and the legacy supervisor executable
branches are unchanged. Actual native cancellation, guest framing, persistent
ownership, external writer exclusion, teardown/restoration and recovery remain
unproven. Existing stems, USB, observation, audio-quality and physical-timing
gates remain CLOSED. Do not rerun these passing checks unchanged.

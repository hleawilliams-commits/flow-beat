# Focused host evidence — November 4

Two fresh offline matrices passed on their first attempts. Each executed eight
new selectors and four strict compiling controls. One locked/offline production
check compiled the inactive actual az-preview crate; the second matrix omitted
that check. All 275 test names were compiled/listed, but the 267 inherited
selectors were filtered and never executed.

The host was aarch64-apple-darwin with Rust/Cargo 1.98.1 (Homebrew), LLVM 22.1.8.
These are ordinary Rust host checks, not sanitizer or emulator-runtime results.
No active endpoint, device, native hook, core or package was touched.

| New selector | Evidence |
| --- | --- |
| retained_and_spawn_refusal_preserve_reader | Exact scripted spawn error; no read/drop before final owner destruction |
| gate_publication_precedes_first_read | Actual JoinHandle retained before private gate release and first Read |
| completed_journal_precedes_unit_notification | Actual anonymous-socket bytes/EOF; immutable journal visible before notification |
| reader_error_preserves_exact_payload | Exact custom io::Error; repeated report refuses while preserving first receipt/count |
| receiver_loss_preserves_completed_read | Real private receiver drop and actual send failure; completed read retained separately |
| reader_panic_is_joined_without_replay | Exact panic payload, poisoned attempted/unreturned journal, actual join, no replay |
| blocked_socket_allows_nonblocking_snapshot | Inspection completes while real socket read is held; explicit EOF release then join |
| joined_owner_retains_reader_until_final_drop | Result and resource remain after join; final destruction is separately observed |

The gate/publication barriers and short inspection deadline discriminate fixture
ordering. They are not product latency bounds. All final semantic oracles occur
after explicit worker cleanup/join; the spawn-refusal case has no worker. The
two anonymous socket fixtures use real local pairs with bounded fixture read
timeouts. No Child, mixer port or cross-process bridge is simulated as complete.

| Compiling control | Exact final oracle |
| --- | --- |
| Gate release moved before actual handle publication | reader_reporter_tests.rs:228:5 |
| Full journal publication moved after actual send | reader_reporter_tests.rs:262:5 |
| Actual send failure returned as NotificationSent | reader_reporter_tests.rs:294:5 |
| Snapshot try_lock replaced with blocking lock | reader_reporter_tests.rs:332:5 |

Each control exited 101 at its exact reviewed final assertion after cleanup.
Timeout, missing selection, unrelated panic or compiler failure is not evidence
that the control succeeded.

## Required independent audit

Review identified an acceptance gap in the focused runner: an extra earlier
fixture panic could accompany its expected final assertion. The separate public
[auditor](audit-results.py) closes that gap before accepting combined evidence.
Every control log contains exactly one panic, at its expected final oracle.
The intentional reader-panic positive contains exactly one terminal-reader panic
at reader_reporter_tests.rs:62:9. Every other positive contains none. Four
in-memory parser controls reject duplicate, unparsed, missing and wrong-location
panic records. No test or build was repeated to add this evidence check.

The first audit passed, verifying 48 frozen inputs, 32 original files, 34
composed files and eight captured execution inputs per matrix, plus the two
exact private Git metadata files. It independently reconstructs the only
inherited edit (the main module declaration), each mutant, the source-bound
oracle locations and the separately pinned inherited test inventory. It checks
all ten retained test executables, all 49 logs and all 24 selected executions.
The auditor's own source hash is bound separately in the sanitized receipt.

Semantic receipt projections match. Ten of 24 common raw logs differ, including
compiler output and panic thread identifiers; neither raw nor normalized log
equality is claimed. Exact exclusions are recorded. Both private preparation
attempts passed; there were no failed matrices, timeouts or unexpected fixture
panics. This is retained-byte corroboration, not adversarial attestation.

Matrix receipt hashes:

- First: d9ff9eacdecf7886612cd55fcbfce1b5548946304f945a7246c0319018aa0672
- Focused repeat: 71a2274d57f52f6ae52071e5532a4e19d42f623a9ee7772ef68ef959d69672fd
- Required audit: 07e02952491b00906c24c19bbd710bc1fe7f8f1d75d8e931dae08f3189929755
- Auditor source: c9e134bcb530068411cad5521b289f4c5704e6079cfb4bc679acdb658b2c3fb3

See [result.json](result.json) for complete sanitized bindings. Raw logs,
executables, source captures and the unexecuted preliminary private auditor
draft remain ignored. The prebuild test-module path correction and subsequent
review are recorded in REVIEW.md; no failing build was hidden.

The reporter remains inactive. Two-process ownership, actual controller/status
transport, continuous child observation, native stop authority, recovery,
observer/staging runtime, USB completion/cancellation and physical timing remain
open. Do not replay this unchanged passing matrix.

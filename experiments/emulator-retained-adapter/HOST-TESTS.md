# Focused retained-adapter host evidence

Two fresh matrices pass: primary and one independent invocation. Each has 14
positive selectors, with one final compound functional check per selector, and
nine strict compiling controls in each plain, ASan/UBSan and TSan mode. There
were no failed matrix attempts or audit attempts. No passing parent suite ran.

[Primary result](host-result.json) and [independent result](host-result-independent.json)
are byte-identical; the [retained-file comparison](comparison.json) records the
source/log audit. Both result files have SHA256:
1d2dc532da0b479a7158ae3ac0108baa060ee19e412bf74577da664d74f5d5f5.

Compiler: Apple clang 16.0.0 (clang-1600.0.26.6), arm64-apple-darwin25.2.0.
Each invocation records 42 clean compiles, 30 clean links and 69 executions.
Production translation units use -Wall -Wextra -Werror; unused declarations are
suppressed only in the harness that includes extracted kernel methods. Every
control must exit 1 with exactly its selected functional failure and expected
summary. Compile/link errors, setup assertions, sanitizer diagnostics, timeouts,
extra output or other failures do not count as a detected control.

## New cases

| Selector | Focused observation |
|---|---|
| allocated | Expected plan copied by value, no syscall, early validation refused without changing evidence, backend admission closed. |
| invalid | Invalid arguments/plans return null with no duplication, STATE or close. |
| duplicate-arm | Owner/attempt phase visible during the synchronous duplicate shim, then distinct fd retained before validation. |
| duplicate-errors | Exact negative, missing-errno, abnormal and same-original returns retained; no close, STATE or retry. |
| fd-zero | fd0 held through original-number reuse; successful duplicate/STATE ignore stale errno semantically while retaining it. |
| state-arm | Held descriptor and validation intent visible during synchronous STATE; initialized output is zero. |
| partial-state | Actual extracted STATE plus partial usercopy returns EFAULT; first 104 bytes and remaining 104 zero bytes survive exactly. |
| mismatch | Ten valid-shape plan disagreements cover both instance words, run, epoch, sequence, flags and raw frame positions including byte 151. |
| malformed | Corrupted version, reserved field, epoch, flags or CRC length remains raw diagnostic evidence without Ready. |
| positive-state | Full returned bytes with abnormal positive syscall result remain retained but unvalidated. |
| repeat-failure | Failed validation and duplicate cannot be retried or overwrite first evidence. |
| repeat-ready | Refused repeats preserve Ready, exact raw acquisition journal and single syscall counts. |
| disposal | Unheld/held branches, slot consumed before close, exact raw report, one close/no retry and original preservation. |
| broker-wire | Ready successor feeds actual frozen broker/dispatcher and one exact modeled FRAME; acquisition evidence remains unchanged. |

The invalid-plan oracle observes null/no-I/O behavior, not allocator calls.
Static review confirms input rejection precedes calloc; there is no allocation
counter or allocation-failure injection. Synchronous same-thread inspection is
the only in-flight inspection exercised. Factory admission is tested before
Ready; copied callbacks run after Ready via the actual broker. No hidden core
access or fabricated owner is used to claim broader reentry coverage.

The disposal uncertainty fixture removes the modeled duplicate, then returns
(-1,EINTR). It tests report/call-count behavior, not resolution of a real close.
All observations are copied before fallback synthetic fd cleanup. No runtime
teardown authority follows from cleaning this quiescent test graph.

The broker-wire case counts adapter-mediated operations only: one duplicate,
one validation STATE, a second STATE at broker creation, a third at adoption,
then one FRAME without hidden revalidation. Direct fixture CLAIM/STATE setup
and observations are outside those counters. The request, command, modeled
state and canonical receipt preserve all 152 raw bytes. A host-side C fixture
does not execute Rust transport, Linux libc/VFS or native UI input.

## Discriminating controls

All nine mutate only the new retained.c translation unit in fresh private
variant directories; frozen parent files and generated core stay unchanged.

| Control | Exact oracle selector |
|---|---|
| duplicate-arm-is-late | duplicate-arm |
| state-arm-is-late | state-arm |
| discard-partial-wire | partial-state |
| ignore-final-state-byte | mismatch |
| validate-ready-again | repeat-ready |
| export-before-ready | allocated |
| use-original-number | fd-zero |
| fd-zero-is-absent | fd-zero |
| omit-held-close | disposal |

[Controls](controls.json) retain exact old/new source and expected failure label.
[Inventory](test-inventory.json) binds selectors and check counts. The repeat
control really makes a second STATE call; the final-byte control only masks the
last comparison byte. The disposal control frees owner storage normally but
omits its close, so the functional oracle fails before fallback fd cleanup.

## Source provenance and audit

Each fresh private run captures seven new inputs and 18 pinned parent inputs.
It prepares the same three-patch module from the pinned local source checkout,
extracts seven exact driver methods, and produces 17 generated inputs. No module
or guest is built, loaded or started. The native callback include is the exact
6,378-byte prefix of frozen adapter.c before int mba_attach(; its SHA256 is
385ad0b7b604c5525c10b33de4683b4a4bbde25519d114a0c8c52a8b12410420.
No legacy public lifecycle implementation is linked.

The private retained-file audit independently reconstructs extracted methods
and declarations, the native prefix, copied headers/translation units and all
nine variants. It checks every recorded compile, link and functional log,
exact frozen/generated/prepared-module file inventories and both public result copies. Its SHA256 is
91f9c3ef17892620fa69aad07d1a24c84bd319d39cc6fe0fe20dd864c84bcbab.
It reads retained files only, without rerunning tests.

Before the matrices, source review corrected descriptor-exposure wording,
an incorrect harness receipt-field name and disposal-test coverage. Runner and
audit now reject optimized Python unconditionally, and initial input-capture
errors receive a retained attempt record. A separate refusal check confirmed
both stop before creating a run directory under Python -O. These were pre-run
review corrections, not failed matrix attempts. Parent historical failures
remain recorded in their own experiments.

Raw prepared sources, binaries, runtime logs and audit script remain ignored
under private/. Public evidence contains authored code, hashes, counts and
sanitized results only. This checkpoint supplies no live acquisition/CLAIM
owner, native baseline/stop authority, concurrent or durable retention, checked
teardown, runtime installation, audio-quality or physical-timing proof.

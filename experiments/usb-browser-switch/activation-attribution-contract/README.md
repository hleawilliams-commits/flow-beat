# Offline core fault-attribution contract

This separate fixture executes three exact functions from the pinned SDK against synthetic owned records. The first complete matrix passes **111 checks across 17 fresh-process selectors per plain and ASan/UBSan mode**, plus five strict safe controls in each mode. It turns the preceding [source-derived attribution counterexample](../activation-unwind-policy/README.md) into a bounded executable result.

In the modeled case where the synthetic generated-code PC, LR and frame RAs are outside every module envelope, lookup returns no owner. No generated-range registry or repair is implemented. No signal, hook, firmware, emulator or physical device is used, and every live/product gate stays **CLOSED**.

## Exact source and substitutions

The runner reuses the prior private source capture without rerunning its collector. SDK revision: 3e0c5cb010454228f4ee63226b60d060494eeaae; full community.c SHA-256: 4b2b72e2b623dcb1ecb0ef108a473815adab45a516a6aa13adb6cce512b80866. It requires that full hash and all three exact extent hashes before compilation.

| Function | Pinned lines / bytes | SHA-256 |
| --- | --- | --- |
| cm_range_cb | Starts 1503; 599 bytes | a13b13bb80cda80f3f7fdae33161788ff826b07d15937f2654b72536d780d814 |
| cm_owner_of | Starts 1533; 163 bytes | 22f8016ac6a6e04f4f56bc7532f41368f7306db3a71eb9598de69a76cd68e6da |
| cm_blame_fault | Starts 1561; 962 bytes | 9e31f235353a169e46f366ec33488076829b9383bc0fd0c462b465a0ddef70b4 |

These function bodies are concatenated unchanged into a private include. The public [fixture](test_attribution.c) supplies explicit synthetic cm_mod and program-header field models. They are value fixtures, not representations proven compatible with the real loader ABI or native object layout.

Frame addresses are integer keys in a bounded cell table. The modeled crash_peek8 records ordered reads and returns only table values; it never dereferences these addresses. The unblamed-crash callback records a count. Macro substitutions redirect open/write/fsync/close in the extracted body to bounded fixture ledgers. No actual crash persistence, startup quarantine or recovery code is executed. The fixture's stdout is its JSON test report.

Each selector gets a fresh process. The actual function-local static volatile written is left unchanged and has no reset seam. Cases that deliberately call twice test its suppression after an initial unblamed result, open failure, failed/short write or sync/close failure. This serial execution does not establish synchronization, reentrant signal safety or handler correctness.

## What the cases establish

| Cases | Selected behavior |
| --- | --- |
| anonymous-miss | Synthetic PC/LR and both frame RAs outside every mod envelope produce no owner, ordered reads, one modeled unblamed notification and no modeled blame-file open. |
| pc-precedence, lr-precedence, frame-hit | PC wins over a different LR owner; LR wins over a different frame owner; the first owned frame wins over a later one. No frame reads occur after a PC/LR hit. |
| range-envelope | The callback constructs one half-open minimum/maximum envelope over all PT_LOAD entries, including a non-executable segment and the gap between segments. Wrong image/no LOAD leave values unchanged; zero hi disables lookup; overlapping envelopes select the first index. |
| first/second frame-read failure | A failed next-FP read prevents the RA read. A failed RA read prevents advancing to a later otherwise-owned frame. |
| frame-limit-hit / miss | The sixteenth frame can select an owner; a matching seventeenth frame is not read. |
| invalid-low / unaligned frame | The address guard prevents modeled reads even when cells exist at those integer keys. |
| nonincreasing-frame | A matching RA is selected before the next-FP monotonicity break. |
| first-call-guard | An initial unblamed attempt consumes the once guard; a later mod-PC call returns no owner and produces no additional effects. |
| open / write / short-write / sync-close failure | Ownership selection can return an id despite modeled persistence failure. An opened fd still gets sync and close after a failed/short write, and a second call does not retry. |

The range case also gives a synthetic mod a different state field: the exact lookup does not consult that field. These tests preserve existing envelope behavior and report its limits; they do not redefine loaded-state eligibility or prove that a selected owner caused a fault.

The anonymous-miss case represents the ownership lookup inputs described by the prior source review. It does **not** execute an installer or establish a successful claim, generated instructions or live fault. Those are separate source/transfer findings.

## Discriminating controls

Every control changes only one reviewed source expression/block in a private extracted include, compiles under the same strict flags and terminates through the normal synthetic fixture. The runner requires the exact selector, exact check count, exact failed-check list, exit 1 and empty stderr. An unrelated crash, sanitizer report, compiler failure or extra/missing oracle failure cannot count as a valid control.

| Private mutation | Selector | Exact failed checks |
| --- | --- | --- |
| Include the upper envelope endpoint | range-envelope | excluded-boundaries |
| Try LR before PC | pc-precedence | owner, write-payload |
| Ignore failure of the second frame read | frame-second-read-failure | owner, peek-trace, unblamed-count, no-file-io |
| Permit a seventeenth frame | frame-limit-miss | owner, peek-trace, unblamed-count, no-file-io |
| Bypass the once guard | first-call-guard | second-owner, second-no-side-effects |

No control changes an assertion or fabricates a reported failure. The baseline's original bodies, source capture and earlier successful suites remain unchanged.

## Build and retained evidence

[Results](result.json) and [verification](verification.json) describe the first complete matrix. Apple Clang 16.0.0 targets arm64-apple-darwin25.2.0, with C11, optimization level 1, debug/frame-pointer retention and strict warnings. The instrumented mode adds AddressSanitizer and UndefinedBehaviorSanitizer with immediate failure. Leak detection is disabled. This is a serial host fixture, not an ARM64 Linux SDK build, loader integration or real handler test.

Four inputs, 57 commands, 114 stdout/stderr streams, 70 artifacts (including debug companions) and all 303 run files are retained privately. Independent source/control review precedes execution. An independent byte-only audit verifies exact input/extraction/mutation bytes, command receipts and flags, reports, complete artifact inventory and the sanitized projection without importing or rerunning the runner. No old suite or source capture was repeated.

The [runner](run-tests.py) supports selected modes, sections, selectors and control names for a genuinely changed or failed follow-up. Do not rerun this passing matrix unchanged. Raw commands, source copies, generated includes, binaries/debug files and logs stay under ignored private directories; public results contain identities, counts and hashes.

## Limits and next scope

This fixture verifies selected source decisions through modeled dependencies. It does not prove that crash_peek8 can safely read arbitrary native frames, that blame is durably written, that a real signal is safely handled, or that volatile written is concurrency-safe. It supplies no TSan, asynchronous-unwind, callback-quiescence or general lifetime result. In particular, adding anonymous FDEs would still not change this manual frame/envelope ownership lookup.

The next separate core review must define exact generated-range ownership before any implementation is composed: distinguish per-island-slot instruction coverage from a shared pool and literal data, retain actual trampoline extents/validated owners, and account for every install success/refusal/cleanup path. Specify bounded capacity/overlap refusal, visibility to the actual signal reader before redirection and permanent lifetime after publication. A failed prepublication attempt must not leave a stale attributable range after unmap. Existing PC/LR/frame precedence and quarantine protections must remain intact; ownership is not causation. Do not substitute a post-claim copy or logical capture close for that publication/lifetime contract.

Live image/entry provenance, real SDK admission/current hook ownership, accepted control-flow scope, writer exclusion, application/supervisor/QEMU topology and stopped recovery remain missing for passive attachment. Local Library row/provider/readiness, actual arguments, operation ownership and alternate destructive/queued paths remain activation gates. No native pointee/post-return reads, return interception, TLS pending state or cross-entry pairing are added. Stems staging, USB activation, main/live dispatch and all unresolved product gates remain closed; no installation or release occurred.

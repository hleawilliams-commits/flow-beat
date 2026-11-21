# Offline ordinary-provider fixture

This separately versioned C11 fixture executes fixed authored synchronous callbacks through the retained ordinary driver. It remains an offline experiment. Every live gate is **CLOSED**.

The first GCC 7.5 Linux AArch64 matrix passes **270 summed assertions (30 distinct names) across 14 ordinary selectors and 8 control baselines per mode**, plus 8 exact singleton controls per mode: **60 fresh processes** across plain and ASan/UBSan configurations. Leak detection is enabled. This is bounded ordinary callback and same-thread nested-loser evidence, not general reentrancy or native provider safety.

## Implementation and custody

The [retained plan](../activation-ordinary-provider-plan/README.md), [complete source ledger](source-ledger.json), [transformation map](source-transform.json), [test contract](test-contract.json), [source review](source-review.json) and [review lock](review-lock.json) bind the prebuild inputs. The six retained reader fragments, sole terminal admission, receipt state machine and five-function setup surface remain intact. Setup finishes before entry; both commit functions retain their Virgin eligibility checks.

Only the admitted caller reads request/provider fields and captures its fixed typed table and context. Only reached callbacks are required: unused NULL callbacks remain valid. Tables, bindings and request/model storage remain stable through synchronous return. Context changes are limited to authored bounded counters, callback journals, a nested-call guard and public observations.

Callbacks receive copied owner/write arguments, with canonical 33-byte IDs and full signed 64-bit accepted-open tokens, including zero. The implementation preserves callback-return/error pairs together, then records private raw availability, authoritative receipts and finally immutable release/acquire completion. A callback's `prepared_return` journal is an authored pre-return value; it does not independently observe the outer raw capture or receipt stage. Public snapshots expose no incomplete payload.

The fresh [method/dependency review](codegen-review.json) binds all four standalone/setup plain/sanitizer objects before runtime. Selected plain code shows admission before provider loads, five indirect callback sites, returned registers stored before raw availability, and payload copy before release publication. The large by-value C arguments lower to caller-created stack copies on this ABI. LL/SC admission can retry; libc, stack-canary and sanitizer dependencies remain. Standalone configuration is compile-only. The candidate reuses only its newly compiled setup object; controls reuse only the same packet's macro-independent harness object.

## Focused cases and controls

The 14 normal selectors cover generated ownership, fd zero and a token above 32 bits; no-owner work; open refusal, short write/sync failure and write error; invalid write-domain and missing reached callback; nested ordinary losers at unblamed/open/write, including a genuine NULL nested request; and terminal repeats after completed/incomplete returns. Post-return caller mutation is actually performed and does not refresh retained completion.

Each designated callback makes at most one guarded synchronous nested call. That call loses the existing admission and returns suppressed before binding/provider work. This does not establish callback quiescence, asynchronous reentry, signal safety or general recursion behavior.

| Control | Exact singleton failure |
| --- | --- |
| Early provider access | `suppressed-entry-does-not-call-provider` |
| Wrong nested-loser return | `nested-entry-returns-suppressed` |
| Omitted unblamed call | `unblamed-callback-is-invoked-once` |
| Alternate valid context | `callback-receives-bound-context` |
| Decoy copied write value | `write-callback-receives-recorded-payload` |
| Replaced close return | `close-receipt-matches-callback-return` |
| Repeated short write | `short-write-dispatch-is-single-attempt` |
| Narrowed open token | `accepted-open-token-reaches-followup-callbacks` |

The controls use the same candidate harness and assertions. Early-access spies are log-only; the close-return mismatch is one composite oracle; the duplicate-write oracle retains raw journals and applies the same exact-duplicate normalization to baseline and control. The dormant callback fields are included in duplicate equality.

## Retained first evidence

[Sanitized results](result.json) and [independent verification](verification.json) retain 30 inputs, 161 commands, 322 streams, 236 build artifacts, 60 reports and 979 packet files. Independent source/control review and a separately frozen corrected byte audit pass. **486,378 structural audit assertions are not runtime cases.** The auditor was independently reviewed, copied before invocation and unchanged afterward; no predecessor matrix, successful retention or audit was replayed.

All implementation corrections occurred before first build, including error sentinels, complete duplicate comparison, stable mutable setup storage, runner/source-pin closure and schema counts. Saved runner/contract drafts remain private; the earlier harness edits have review history but no preserved original draft. No compiler or runtime failure occurred. The first auditor duplicated unified-diff header/hunk newlines and refused otherwise unchanged source spans. Its frozen source and failure packet remain; a separately reviewed auditor uses the original keepends/direct-join serialization and retains exact equality and byte-coverage checks. Its first invocation passes. A separate codegen-review JSON authoring assertion failed before writing output; only an explicitly retrospective note and the corrected writer are retained, without a claimed original failure receipt. Ten sanitizer-object metadata warnings match the already retained exact FINI_ARRAY structural exception; every other diagnostic remains refused. The historical failure packets remain unchanged.

The private packet retains command outputs, objects, executables and raw reports; public files contain source, authored-value summaries and hashes. No firmware, music, credentials or device logs are published.

## Limits and next boundary

There are no private stepping/inspection/refusal/diagnostic symbols in the witness configuration, second once guard, frozen volatile consumer call, concurrent publication, native frame/crash-file provider, DSO/installer invocation, signal/nonlocal exit, firmware/emulator/device action, production integration, live observer or release. Authored error numbers are explicit values, not captured native errno. Callback journals and ordinary return evidence do not prove persistence, signal safety, native availability or fault causation. Native visibility, arbitrary loader/interposition, external writers, original/replacement lifetime, quiescence and recovery remain open. Quarantine and protections are unchanged.

Next make a separately scoped source-only native-provider admissibility review. Reuse the frozen ordinary-provider fixture, exact readers and retained fault-reader/SDK source corpus to map each proposed open/write/sync/close/unblamed callback to its actual native call context, descriptor/token range, return/error capture and lifetime requirements. Separate ordinary synchronous callback evidence from handler/signal/nonlocal-exit paths, libc/canary behavior, persistence outcomes and quiescence. Identify the smallest defensible future boundary and concrete missing evidence before any adapter implementation; do not infer native availability or safety from authored callbacks or nested losers. Keep one terminal admission, immutable completed ownership, setup-only APIs, quarantine and all existing protections. This review is source-only: no new callback matrix, native frame/file provider, DSO/installer, signal/nonlocal exit, concurrent publication, emulator/device, production integration or live observer composition. Do not replay this passing matrix/audit or previous successful captures unchanged; every live gate remains CLOSED.

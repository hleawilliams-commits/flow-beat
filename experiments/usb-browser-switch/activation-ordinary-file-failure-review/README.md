# Ordinary-file failure-custody review

The frozen ordinary-file witness establishes ordinary provider/receipt correspondence on owned disposable files. Its passing runs do not exercise native close failure or missing returns. This source-only review maps those limits and selects a smaller future target: **known held-descriptor cleanup after an explicitly consumed validation veto**. Every live gate remains **CLOSED**.

The [boundary review](failure-custody-review.json) pins the exact engine, provider, native leaves, harness, readers and retained results. It distinguishes nine boundaries, six historical controls and three proposed controls. [Source review](source-review.json), [retention](result.json) and [verification](verification.json) concern this new source-only review. No prior passing capture, matrix or audit is replayed.

## What the first witness actually established

The retained GCC 7.5 Linux AArch64 C11 matrix ran 40 fresh exec children and separate supervisors: eight ordinary selectors, six control baselines and six singleton controls per plain and ASan/UBSan mode. Each candidate mode summed 126 assertions with 22 distinct names. The two repeat-case executions add two suppressed calls, giving 42 ordinary engine calls.

Actual native negative outcomes were exclusive-create OPEN `-1/EEXIST` and read-only WRITE `-1/EBADF`. Read-only fsync returned zero in both modes. Across the retained children, 32 successful provider opens all returned fd 3, all 24 actual provider closes returned zero, and all eight external held-cleanup closes returned zero. These are provider counts; parent/observer descriptors are separate.

The supplied-close case refused before native close, leaving a known held descriptor for separate cleanup. The invalid-write case actually wrote 32 bytes, supplied 33, returned incomplete with no public payload, and then closed the still-held descriptor outside the terminal attempt. Neither is a native close failure or a missing-return observation.

The six existing controls separately exposed corrupted supplied errno, a full-width corrupt token, decoy owned bytes, omitted native sync, omitted native close and an extra successful owned write. Their exact singleton failures do not establish untested native failure paths. The corrupt token never reached follow-up native I/O; the extra write never retried a failed write.

## Boundary and custody map

| Boundary | Evidence obtained | Remaining limit |
| --- | --- | --- |
| Setup and inheritance | Selected successful parent capability creation, child validation and authored model setup | Early setup/handoff/exec failures and per-call inheritance diagnostics were not exercised |
| Successful open and validation | Actual int fd held before successful fd/name identity queries; full-token checks before narrowing | Failed queries, identity mismatch and HeldUnvalidated cleanup were not exercised |
| Returned native failure | EEXIST and EBADF retained separately from callback values; write failure still followed by sync/close | No actual close failure, short write, EINTR or retained native sync error |
| Supplied refusal/incomplete | Explicit origins, ordinary returns and public completion kept separate | A prepared return does not acknowledge engine recording |
| Missing return | Source marks close custody unknown before native entry | No return means downstream freeze/export may never execute; no live missing-return record exists |
| Report export | Normal exit, exact 4,760-byte report and separate parent read establish accepted envelopes | Child cannot acknowledge its own report write; abnormal/report failures remain untested |
| Observer | Separate nofollow observer fd, identity checks and bounded post-exit bytes | No interference, failed-observer, durability or recovery proof |
| Held cleanup | One separate close from VerifiedHeld, with successful raw result and unchanged public snapshot | Boolean protocol success alone is not close success; unknown custody never authorizes retry |
| Parent cleanup | Selected identity-checked fixed unlink, empty-directory removal and parent closes | Partial setup/wait/report/observer/cleanup failures may leave outstanding owned names |

Two source findings matter for the next step. First, post-open validation failure preserves the saved descriptor as HeldUnvalidated and supplies the engine's invalid-domain `{-2,7104}`. The engine then remains terminal incomplete. But the parent normally requires `creation_identity_available`, which the refused validation never sets. A known held-fd cleanup path therefore does not by itself establish safe path observation or cleanup.

Second, export is downstream of the synchronous engine call and held cleanup. The parent accepts only normal child exit zero before reading the report. A nonzero ordinary exit is retained but its binary report is not accepted; a signaled status is rejected before being copied into the supervisor's status field. No crash-state salvage, missing-return export or abnormal-exit recovery follows from the passing evidence.

The native leaf's entered/returned fields and the provider's UnknownEntered/UnknownAfterCloseReturn states express source ordering. All actual closes in this matrix returned zero. The unused CloseEntered enum is not an observed state. No later test may manufacture close failure by closing, probing, reusing or otherwise operating on a possibly released token.

## Smallest justified future extension

The selected next candidate is an ordinary owned-file **consumed-validation refusal**, separately planned before implementation. A fresh child would perform a genuine successful open and successful fd/name identity queries, retain their raw pairs unchanged, then apply an explicitly authored policy veto before promotion to VerifiedHeld. The native queries would remain successful facts; the veto would not be described as a native query failure.

The saved fd would remain HeldUnvalidated. Supplying the existing `{-2,7104}` would exercise actual incomplete return with acquire-visible ActiveIncomplete and no public payload. The provider report would be frozen before one eligible external cleanup close. Raw cleanup outcome, custody and engine state would remain separate.

The parent needs a separately retained, immutable raw-identity receipt for this path, based on the genuine successful fd/name queries and checked independently after ordinary termination. It must not set normal creation acceptance or VerifiedHeld merely to bypass the refusal. Only the designated consumed-validation diagnostic may be expected; all unrelated setup, query, capacity, argument and infrastructure failures must remain rejected.

Three proposed singleton controls specify the necessary distinctions:

- Corrupt only the reported actual-versus-consumed query evidence, while an independent unmodified identity receipt continues to govern safe external observation.
- Supply `-1` instead of `-2`, exposing the difference between recorded ordinary OPEN failure with completion and terminal incomplete return.
- Omit eligible held cleanup and honestly retain no attempted close. Child exit may contain the still-held resource, but cannot be labeled an acknowledged cleanup.

Each control needs one dedicated composite oracle and an unchanged passing baseline in the later plan. No new matrix or process count is fixed or executed here. A value-only unknown-close policy witness is another possible scope, using inert identities and no syscall dispatcher; it would not establish actual close-error behavior and is not selected as part of this known-held extension.

The engine/header, six reader/type fragments, sole admission, immutable completion, five setup-only APIs and opaque unblamed branch remain frozen. Native frame/crash-file paths, CM_BLAME, CM_CRASHES, quarantine/startup policy, callback nesting, concurrency, signals/nonlocal exits, DSO/installer, firmware/device, production and live composition remain excluded. LL/SC retry, libc/canary/sanitizer dependencies, arbitrary custody, interposition, native visibility, quiescence and recovery remain unresolved. Ownership is not fault causation.

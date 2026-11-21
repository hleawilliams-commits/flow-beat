# Ordinary-provider boundary plan

This source-only plan defines a separately versioned offline successor to the [ordinary-entry witness](../activation-ordinary-entry-fixture/README.md). It adds an authored callback boundary and one bounded same-thread nested loser witness to the future scope. **It implements and executes neither. Every live gate remains CLOSED.**

[provider-plan.json](provider-plan.json) fixes seven transformation groups, five case groups (fourteen selectors), eight control baselines and eight exact singleton controls. Two future modes would require sixty fresh processes. Those are planned counts, not executed coverage.

## The boundary

Keep the single terminal admission and ordinary `ac_run` driver. A future request supplies PC/LR/FP plus a fixed typed table and opaque context instead of embedded I/O return values. The winner captures table/context only after its existing CAS. A suppressed call reads no request/provider/context fields and does no provider work. There is no second once guard.

The table has open, write, sync, close and opaque void unblamed functions. I/O returns `ac_io_value` by value. Its error integer is authored data, not ambient errno. Open receives a copied owner value; write receives a copied value struct with the full int64 accepted-open token, length and 33-byte payload. Callbacks receive no pointers to private owner/event/raw storage. Zero and tokens above 32 bits remain valid under the retained open domain.

Check only the table/function needed at the reached operation. No-owner may provide only unblamed; returned open -1 may omit later functions. A missing reached callback is terminal incomplete, distinct from a callback returning -1. An opaque context may be NULL if its authored callback supports it; the planned witnesses use live nonnull storage. Nonnull is never a lifetime proof.

All module/generated/cell setup finishes before entry through the same five setup-only functions. Preserve both commit eligibility checks and all six exact retained reader fragments. Keep request, table, callback code and selected model stable through the outer return. Callback context may append a bounded transcript and record its one-use nesting guard/results; it may not change bindings or publish models.

## What can be observed

The event is entered before dispatch. An actual I/O callback return is copied immediately into a local value and then the private raw latch; no error getter or other callback intervenes. Only the later record phase validates its domain, captures valid failure errors and advances. Only complete ordinary progression release-publishes the result. The public acquire observer reveals Active with no payload during callbacks.

Callback transcripts record invocation arguments and a `prepared_return` entry written before their C return. That entry is not itself evidence the outer engine captured or recorded the return. Exact enter/raw/record ordering remains a source and future method-review claim. Completed public receipts provide the final outcome; incomplete paths expose no private partial payload. No stepping, inspection, refusal or diagnostic API is added.

Returned write failures or short writes still lead to sync and close. Returned out-of-domain values halt before authoritative recording. The accepted OPEN token is consumed from its already recorded receipt without narrowing or another open. Error, short-write and completion remain separate facts.

## Planned nesting and controls

A designated unblamed/open/write callback sets a local one-use scheduling flag, observes Active/no payload, calls ordinary entry once with a distinct stable valid request/table/context (or genuine NULL in a dedicated case), and records the suppressed return. It observes Active again and returns its ordinary outer value. The flag schedules the authored harness only; the existing terminal CAS alone admits the engine. This tests one synchronous same-thread nested loser, not general reentrancy or quiescence.

The new cases cover provider arguments and full-width tokens, no-owner and returned open refusal, returned versus missing/invalid outcomes, three nesting locations plus NULL, and suppressed repeats after completed/incomplete outer returns. The eight safe controls introduce premature provider invocation, a wrong nested-loser return enum, omitted unblamed dispatch, a live alternate context, a decoy write value, a substituted close return, duplicate short-write dispatch and token narrowing. Each has an exact singleton failure set in the plan; unaffected checks must remain independent.

## Retention and limits

The new helper only copies pinned local source/evidence and line-byte anchors into an exclusive private packet. It performs no Git source read, subprocess, compilation, callback, loader, installer or runtime operation. Source review precedes first retention; an independently frozen byte auditor follows. Successful predecessor retentions, matrices and audits are not replayed.

All prior tested bytes and failure packets remain unchanged. LL/SC admission retry, libc/canary/sanitizer dependencies, per-slot rather than coherent publication, arbitrary DSO/slot custody, loader/interposition, nonparticipating writers, native visibility, signals/nonlocal exits and recovery remain limitations. Native frame/file providers, concurrent publication, DSO/installer invocation, firmware/device actions, production integration and live composition stay deferred. Quarantine and official protections remain unchanged; ownership is not fault causation.

# Ordinary-close value-classifier plan

This is a source-only plan for a separate offline value classifier. **Every live gate remains CLOSED.** No classifier implementation, compiler, native I/O experiment, callback, runtime matrix or process-termination experiment runs here.

The [plan](close-value-plan.json) reuses 23 pinned local inputs and 28 line-byte anchors from the frozen close-disposition review and selected evidence. It defines six future source groups, eight receipt stages, 45 ordinary selectors and five dedicated control baselines. Five singleton controls per mode bring the proposed matrix to **110 fresh processes across plain and ASan/UBSan modes; zero executed**. The retained prior Linux AArch64 image and toolchain identify a possible later host environment, not a new execution.

The new retention and independent byte-audit results concern this plan only. Prior passing captures, audits and runtime matrices remain frozen and are not replayed.

## Value boundary

All inputs are authored bounded values. Integer tags and 0/1 markers permit malformed-domain cases without creating invalid C Boolean representations. There are no descriptors, addresses, callbacks, function pointers, native adapters or resource-operation permissions.

| Stage | Native result authority | Historical one-request eligibility |
| --- | --- | --- |
| HeldFresh | None | True |
| HeldOmitted | None; honest non-dispatch | False: the request was consumed |
| ArmedUnknown | None; conservative custody precedes leaf entry | False |
| EnteredNoCapture | None; entered does not prove libc dispatch | False |
| PartialCapture | Partial raw/error storage is nonauthoritative | False |
| CapturedBeforePromotion | Complete zero/error pair; custody still unknown | False |
| DispositionStoredBeforeHelperReturn | Complete pair plus zero-to-closed/error-to-unknown disposition | False |
| HelperReturned | Separate authored caller-observed return acknowledgement | False |

A complete native result is distinct from provider custody and the ordinary helper return. Only HeldFresh has an inert historical eligibility value. Omission retains the held obligation but cannot obtain a second cleanup request. A returned error never restores held eligibility.

The exact admitted partial and complete raw/error tuples are enumerated. Error numbers 4 and 5 are authored EINTR/EIO-labelled sentinels; they establish no native errno behavior. Unsupported or contradictory complete values produce canonical InvalidEvidence. Unknown-before-leaf and partial-result cases remain valid stopped values, not malformed complete receipts or observed interruptions.

The future local classifier validates the input relations, then uses one capture predicate. Its complete-result branch maps stage 7 to HelperReturnedZero/Error, stage 6 to DispositionZero/Error, and other stages to CapturedZero/Error. This makes the zero-default control observable without a second stage guard repairing it. Derived eligibility is an output, never a redundant input or output repair predicate.

## Parent projection

The parent entry receives a header and bounded inert slot index only. Private fixed authored slots hold child values. It validates header scalar domains and slot range, then applies **one nine-term acceptance conjunction** before a private accessor records one logical payload consumption.

The gate combines report presence, exact wait, normal exit, exit zero, shape/size/read, binding, provenance, conversions and setup. Each term is independently false in a designated ordinary selector. These authored predicates do not reconstruct a real interrupted execution.

Policy verdict is copied separately and excluded from admission: a truthful failed policy-control report is not automatically malformed evidence. Parent observation, unlink and removal acknowledgements are also independent. A mixed absent/error acknowledgement case and an unchecked policy verdict leave an accepted child error classification unchanged.

No child aggregate is passed, copied, hashed, validated or classified before the gate. The private accessor has only bounded storage checks and no second acceptance veto. An unaccepted malformed child remains unread by that route and yields Unavailable. An accepted malformed child is consumed once and yields InvalidChildEvidence.

Invalid header or slot returns canonical empty child storage and unavailable parent facts. After valid header/range checks, parent facts are copied; a false gate preserves those facts with no child result. The exact output tuples are pinned in the plan. JSON null represents absent zeroed child value storage, not a C pointer field.

The stale-held selector uses matching historical VerifiedHeld labels while the later child custody is closed. The old label cannot override the later receipt. These authored associations do not authenticate arbitrary histories.

The proposed accessor counter measures only ordinary logical consumption inside the classifier route. A later test-only comparison may inspect immutable slot data after projection, outside that count. No speculative-load exclusion, general memory-read absence, interrupt-safe snapshot or concurrent publication is claimed.

## Five exact controls

| Mutation | Sole owning composite |
| --- | --- |
| Admit raw zero despite absent complete capture | close-result-requires-captured-return |
| Map the stage-7 error tuple to HeldFresh with eligibility true | returned-error-never-reopens-custody |
| Omit the single report-acceptance gate for one false provenance term | unaccepted-report-cannot-supply-child-outcome |
| Promote independent parent unlink acknowledgement into child zero-close acknowledgement | parent-cleanup-does-not-ack-child-close |
| Relabel honest omission as entered-with-missing-return | omission-is-not-missing-return |

Each baseline and corresponding mutant uses the same immutable authored slot. The composite owns every changed semantic consequence; shared checks preserve inputs, domains, independent facts and infrastructure expectations. No generic full-output or output-coherence assertion duplicates a control's composite. Candidate ordinary cases independently check complete expected projections.

The future candidate uses 50 fixed slots: 45 ordinary selectors and five dedicated baselines. The included invalid-slot selector passes the table length as its index rather than adding a slot. Each mode proposes 50 candidate executions plus five singleton mutant executions. Counters begin at zero in fresh processes and are never reset. Only new value-model sources would be compiled; the frozen engine and native witness are reference inputs only.

Before any later build, freeze and independently review the complete new source ledger, enum/marker layout, immutable case table, exact commands, logical accessor and control partition. The current plan authorizes no implementation or execution.

The sole admission, immutable completion, five setup-only APIs, opaque unblamed and all frozen source/failure packets remain unchanged. LL/SC/libc/canary/sanitizer limits, arbitrary custody, nonparticipating writers, loader/interposition, native visibility, signals/nonlocal exits, quiescence and recovery remain unresolved. No native close error, missing return or failed report is manufactured. No CM_BLAME/CM_CRASHES/startup/quarantine, DSO/installer, nesting/concurrency, firmware/emulator/device, production/live or release work occurs. Quarantine and official protections remain preserved. Ownership is not fault causation.

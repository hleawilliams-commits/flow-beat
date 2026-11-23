# Ordinary-close disposition and report acceptance

This source-only review maps what the frozen consumed-validation witness can establish about close custody and downstream reporting. **Every live gate remains CLOSED.** No adapter, compiler, native I/O experiment, callback/runtime matrix or process-termination experiment runs in this slice.

The [review](close-disposition-review.json) pins 28 existing local inputs and 28 line-byte anchors. It separates ten boundaries, five disposition classes and ten findings. The [retention helper](retain-source.py) only copies and hashes selected local bytes. Independent source review and a separately frozen byte audit are recorded alongside the review; their checks are structural evidence, not new runtime cases.

The first consumed-validation matrix remains frozen: 14 historical fresh exec children, 32 summed candidate assertions with eight distinct names per mode, and three singleton controls per mode. All 14 provider opens returned fd 3, all 42 provider queries returned zero and all 12 actual external closes returned zero. Four selected existing reports are retained here: one ordinary result, the normalization control and both omission controls. No predecessor procedure is replayed.

## Resource disposition

| Evidence at the local boundary | Permitted interpretation |
| --- | --- |
| Known held; no close entered or admitted | Preserve the local obligation. Only the existing ordinary custodian may qualify for its one cleanup request. A historical parent report supplies no live descriptor permission. |
| Conservative unknown state, or leaf entered without complete native result capture | Keep disposition unknown. No close retry, probe, reuse or operation through the saved token. |
| Complete native zero result | Acknowledge that captured native result and retire the token from use. Later provider state, helper return, report export and parent acceptance remain separate facts. |
| Complete native error result | Preserve the error and unknown disposition. Never convert it to still-held eligibility or retry, including an authored EINTR case in a future value model. |
| Report absent or unaccepted | No accepted downstream child outcome. Do not consume an unaccepted payload or infer which earlier stage stopped. Independently known parent facts remain separate. |

The provider stores UnknownEntered before calling the native leaf. The leaf stores entered before libc close. These are conservative source boundaries, not proof that the call reached libc or the kernel. Raw/error fields precede the returned marker; default raw zero and partial stores cannot acknowledge close. The marker means captured native return, not that the leaf, provider or cleanup helper has itself returned.

All actual close outcomes in the retained matrix are zero. Returned-error and interrupted-stage rows above are source reasoning, not observed failures. The unused CloseEntered enum is not an observed state.

## Reporting does not transfer custody

The independent raw-identity receipt retains successful open/fd/name query evidence before the explicit consumed veto. HeldUnvalidated and absent accepted creation survive; supplying {-2,7104} returns AC_INCOMPLETE with public ActiveIncomplete and no payload. The normalization control instead completes a recorded OPEN failure while the actual held resource obligation remains.

Provider freeze happens before external cleanup. The frozen HeldUnvalidated copy cannot acknowledge that later close. Cleanup records a single request and may return Boolean true without dispatch; its Boolean describes protocol progression. The two omission controls leave attempted, entered and returned false and preserve held custody. They are **non-dispatch controls, not missing-return evidence**.

The child writes its enclosing report only after synchronous engine return, freeze, external cleanup and public observation. It cannot acknowledge its own write inside that envelope. Missing or unaccepted downstream output can result from many earlier or later boundaries; it cannot identify interruption, dispatch, native return, export failure or fault causation.

The parent requires the expected wait result and normal exit zero before examining the report. It checks exact size/read, version, selector, count bounds and descriptor bindings. The report_valid flag then means structural envelope availability only: raw identity, consumed decision, observation, conversions and semantic oracles follow. Main can assess a structurally available report after later supervision failed. A truthful negative-control report and a malformed envelope are different evidence categories.

Parent observation uses its own checked directory and observer descriptors plus the independent raw identity, under RAW_IDENTITY_AFTER_CONSUMED_VETO. It never operates on the child's saved numeric descriptor. Empty read, observer close, fixed-name unlink and empty-directory removal acknowledge parent operations only. Their success in omission controls demonstrates why they cannot acknowledge child close, durability or recovery.

## Smallest future boundary

The selected candidate is a separately planned **inert value classifier**. It would keep local historical disposition, captured native outcome, provider/helper return and parent accepted-report projection separate. Parent observation/cleanup acknowledgements remain an independent result. It would receive authored bounded values, not descriptors, addresses, function pointers or resource capabilities, and return no executable permission. No dispatcher, native adapter or actual report parser is proposed.

A later source-only plan must fix valid and contradictory combinations, phase semantics, explicit acceptance criteria, finite selectors/baselines and exact control assertions. Unknown enum/domain inputs must be refused. Unaccepted child payloads must not influence their projection. Structural availability, provenance eligibility and expected witness verdict must remain distinct.

Five proposed singleton controls each own all changed consequences in one composite:

| Value-only mutation | Sole proposed composite |
| --- | --- |
| Infer zero-close acknowledgement without complete return capture | close-result-requires-captured-return |
| Turn captured error into held/cleanup eligibility | returned-error-never-reopens-custody |
| Bypass acceptance for an unaccepted payload claiming closed zero | unaccepted-report-cannot-supply-child-outcome |
| Promote parent unlink acknowledgement into child-close acknowledgement | parent-cleanup-does-not-ack-child-close |
| Relabel honest omission as entered-with-missing-return | omission-is-not-missing-return |

Shared truthful-input checks still reject unrelated malformed evidence. No control may create a real close failure or call through an unknown/possibly closed token. Authored stopped values would not establish actual interruption, errno behavior, signal handling or process termination.

The frozen engine/header, six reader/type fragments, sole terminal admission, immutable completion, five setup-only APIs and opaque unblamed branch remain unchanged. LL/SC retry, libc/canary/sanitizer dependencies, arbitrary custody, loader/interposition, nonparticipating writers, native visibility, signals/nonlocal exits, quiescence and recovery remain gates. No native frame/crash-file, CM_BLAME/CM_CRASHES/startup/quarantine, DSO/installer, nesting/concurrency, firmware/emulator/device, production/live or release action occurs. Quarantine and official protections remain preserved. Ownership is not fault causation.

Next make only the separate source-only value-classifier plan. This review does not authorize its implementation.

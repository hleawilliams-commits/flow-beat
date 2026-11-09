# Community inline claim and owned receipt composition plan

This source-only successor to the [installer receipt fixture](../activation-installer-receipt-fixture/README.md) at ab56c8f33df54cf0ad2bc05b159c5534dc03fdf3 specifies the next offline C experiment. It connects the retained SDK inline-claim predicates and init result handling to actual owned emission receipts. **No composition implementation or production patch exists here. All live gates remain CLOSED.**

The concrete changes required are a single admission/close/accounting domain, a true-entry caller value, an immutable session captured before waiting, full SDK ID storage, and irreversible accounting of normally returned published claims. Merely placing the current public receipt wrapper inside community.c would retain two mutex/epoch/counter authorities and an extra replacement-envelope predicate.

[composition-plan.json](composition-plan.json) records the ten transformation groups, ten future case groups and six safe controls. [retain-source.py](retain-source.py) only retains pinned existing inputs and line-byte anchors. The [source packet](result.json) retains 17 inputs and 23 line-byte anchors in 19 files, with no child commands or new SDK Git reads; [verification](verification.json) records the independent audit and source-review status; the future cases and controls have **not run**. No old collector, transformer, passing matrix, compiler, make, firmware or device operation is required for this plan.

## Retained boundaries and newly identified mismatches

All SDK references use commit 3e0c5cb010454228f4ee63226b60d060494eeaae. Existing local captures supply community.c, azmod-mod.h, inline_hooks.c and azprot.h; there are no new SDK Git reads. Existing receipt sources, its already generated installer/protection files, ledger and result/verification are reused without regenerating them.

| Exact source | Consequence for composition |
| --- | --- |
| community.c:1226–1248 | Inline admission checks ID, caller min/max envelope, current loading init, hooks_ok and target-start envelope before taking its mutex. |
| community.c:1095,1251–1271 | The SDK mutex is recursive; the existing installer is enclosed, but nhooks increments after unlock. |
| community.c:1847–1851 | Init opens/closes plain loading flags around the callback without this mutex. |
| receipt.c:386–453 | The frozen fixture owns a different nonrecursive cm, one epoch and module_hooks counter, and additionally checks the replacement against its authored claimant envelope. |
| community.c:81,463–465; azmod-mod.h:447; receipt.h:21 | The SDK allows 32 ID characters plus NUL; the frozen receipt's 32-byte array allows only 31. |
| azmod-mod.h:199–204; receipt.h:27–31 | SDK success is 0, raw installer success is 1, and the richer receipt has separate OK/warning/status values. These domains cannot be substituted. |

The separately named successor must widen **all owned ID fields to 33 bytes** and exercise a 32-character ID without truncation. Keep the old fixture frozen. Remove its extra replacement-in-claimant-envelope admission predicate; exact fixed replacement custody supplies the new experiment's separate lifetime condition. Inline claim itself does not require that replacement address to lie inside the claimant envelope.

cm_claim_hook calls cm_called_by directly, not cm_self. It does not inherit cm_self's frame-thread refusal, require a thread identity, or require LOADED state. Do not import claim_vslot's predicates. Caller range is the retained half-open min/max PT_LOAD envelope, including gaps and non-executable segments. The target predicate checks the start address within the executable min/max envelope; known target16 custody and checked overlap arithmetic are explicitly stronger additions.

## One ingress, one authority

Preserve the exact bodies of cm_id_ok, cm_find and cm_called_by in a separate offline translation unit. cm_find reads the plain record count and IDs, so all calls and record initialization/count updates occur under the shared recursive cm discipline. Authored ID arguments are valid readable NUL-terminated buffers. This plan does not turn strlen/strcmp into safe arbitrary-memory probes.

Keep the exact SDK function-pointer signature: int(const char*, uintptr_t, void*, void**). A separately named, noninline public entry captures CM_RA once at that entry, before any dispatch helper. It also acquire-loads one internal active-session handle. A future fixture-only authored-value entry supplies explicit caller-RA/session inputs to the same worker; it is separately named and never presented as authenticated native provenance. No TLS override or silent CM_RA redefinition is planned. The actual-builtin configuration starts compile-only unless a later explicit test validates it.

Use four fixed session slots for this bounded experiment. One atomic unsigned value encodes 0 for closed or index+1 for a particular immutable process-lifetime session. The bounded index avoids independent index/epoch loads and arithmetic generation exhaustion; a slot is never reopened or reused. Initialize the immutable session under cm → writer, then release-store index+1 on open; close release-stores0 under those same locks. The entry acquire-loads once, and an authored handle is range-checked before it can index or dereference a slot. This specifies publication order, without claiming signal or Linux code-generation proof. One-use means **one open init epoch per module index**, not one hook: several fixed bindings may be admitted in that session.

Under recursive cm → common writer, the worker resolves the ID, applies the retained predicates, and requires the captured nonzero handle still to equal current, name that same index, and agree with the locked SDK loading/init state. Never refresh a waiting request to a later session. Implement the new session veto as one combined condition: captured == 0 || captured != current || captured != index+1. After it passes, construct the private permit from the locked resolved current session; the private installer does not repeat the entry-token veto. Read mutable open/loading/count state only while locked; the entry reads just the atomic handle and immutable facts when needed.

This distinguishes two schedules. A claim captured in A cannot proceed after A closes and B opens. A claim that captured 0 before A opened cannot later adopt A even though the old loading predicates now pass. The latter is the precise discriminator for the safe control that omits this entire combined new veto; simply skipping the new check after A closed would still be rejected by the retained loading predicate and would be an ineffective control.

## Lock and lifecycle transformation

| Operation | Lock extent |
| --- | --- |
| SDK inline claim | Recursive cm → writer → the existing actual protection helper; retain cm/writer through receipt completion and SDK accounting. |
| Direct Core claim | Writer → protection; private typed Core owner; no mutable cm reads or writer → cm edge. |
| Record/module publication and session open/close | cm → writer for bounded state mutation only; callbacks outside both. |
| Private receipt install | Already-held writer; it must not reacquire either public receipt mutex. |
| Generated-only reader | Existing bounded acquire lookup, with no loader or admission calls. |

The successor factors the copied receipt installer into an internal writer-held entry. It removes the fixture's second cm, independent epoch and module_hooks authority. The only public Community operation is the transformed SDK ingress; one SDK nhooks field and per-attempt accounted evidence describe the same publication. Core and Community share binding/target/storage/capacity reservation. Retain constructor-to-late transition under writer and restrict this successor to the late-island path.

Add an explicit one-start controller before authored callbacks; an in-progress or repeated invocation refuses without waiting for its own callback. Initialize each fixed record under locks before exposing it to cm_find. Open the session after descriptor acceptance, release both locks, invoke the authored init callback, then reacquire cm → writer to close and freeze the count. An already admitted claim completes before close obtains the locks. A waiting, not-yet-admitted claim must revalidate. An init that starts and joins its own claim worker can therefore run without a lock held by its caller across init.

A same-thread reentry may already hold recursive cm, but it must never enter while holding writer or protection. Fixture scheduler callbacks may hold workers for a declared schedule but cannot reenter admission, logging or mutation. Recursive cm is not a general reentrant installer contract. Pthread failures and unsupported nonlocal exits remain terminal/incomplete evidence, not a lock-recovery scheme.

## Authored loader and capability inputs

The future experiment uses explicit values for handle presence, successful link-map query, link-map presence, intended object identity, base validity and base value, plus bounded PT_LOAD descriptions. A checked scratch builder rejects missing provenance, overflow and empty/inconsistent envelopes. A successful valid zero base is accepted; zero alone supplies no provenance. Keep min/max envelope semantics. These are authored loader inputs: no actual dlopen, dlinfo, ELF enumeration, constructors, approved-hash lookup, filesystem or manifest parser will run in this slice.

Publish copied immutable per-index module facts before the authored descriptor-entry/validation stage, following the retained range-before-entry ordering. Keep them and a retained handle marker if descriptor validation or init later fails. Fixed one-way publication flags preserve original record index order and holes. Storage exclusion reads these committed envelopes under writer; Core does not consult mutable cm fields. This is not module-first fault attribution integration. Later module masking, literal ownership and all constructor-before-range limitations remain.

Authored manifest hooks and firmware-any inputs feed the selected hooks && !any relationship at community.c:1768–1774. The hooks-and-any combination is refused at that selected loader boundary. Otherwise the claim's capability predicate is precisely hooks_ok. This does not validate JSON parsing, firmware matching or user approval. A false-hooks control uses any=false and otherwise valid inputs so no unrelated loader veto masks the intended admission check.

## Permanent custody and result accounting

Preserve the real fixed process-image target/replacement definitions and strong aligned initialized plain void* originals. Resolve the supplied target, hook and orig to that exact descriptor before inspecting an original object. Wrong/temporary/inaccessible orig identities refuse without dereference. This is a bounded ownership precondition, not a general guarantee for arbitrary SDK pointers or DSO unload.

Reserve common target16, binding/metadata/journal/owner and accounting capacity before resource calls. Check both new and existing target endpoints for overflow. A fixed four-binding bound can establish nhooks capacity before unlock; no postpublication count failure is permitted. Keep the one-attempt policy, burned slots and failed/uncertain cleanup custody even when no record becomes visible. Optional hook diagnostics are not authoritative admission or completion.

A normally returned receipt with actual redirect_published accounts exactly once while cm and writer remain held, before epoch close can snapshot nhooks. Preserve the source's SDK success 0 for published receipts including relock warnings, while retaining the raw installer result 1 and richer warning details. A genuine prepublication decline maps to SDK −1 and count 0, including raw −3. RC_OK's enum value 0 is not installer success 1. Unexpected publication/result inconsistency retains terminal incomplete/unknown evidence and never authorizes cleanup, reset or retry.

If a later claim declines or init returns an error, clear only the selected callback/state fields as the retained loader fragment does. Keep earlier originals, immutable generated metadata, copied owner, code/mapping custody, receipts and nhooks. The source's failed-init “after patching code” description must still use the retained count. Closing admission is not draining callbacks or reclaiming code.

Copy bounded diagnostic data before releasing locks. An authored log sink may receive the copy afterward; it cannot determine success from mutable state. The experiment does not execute file logging, persistent blame, Reload, disable/remove or quarantine changes.

## Preserve the actual emission cut

The copied successor retains actual allocation/relocation out/ncopy, mapping and protection receipts, separate island/target tokens, and exact instruction-versus-literal attribution. Its already reviewed cut remains release metadata → actual plain original store → actual target redirect. All new identity/session/capacity/custody decisions finish before target unlock.

The private final permit binds the immutable admitted session and descriptor, actual storage and pre-reserved record during uninterrupted writer custody. It must not add a second epoch authority that silently masks the single admission decision or a new callback/refusal after unlock. Multiple immutable module envelopes replace the old single-module exclusion. Any change to a selected emitter/protection/cut body requires its own explicit ledger entry; it is not hidden behind “same installer.”

The plan reuses the already generated late-island files as review inputs. A future implementation must retain a complete diff and byte ledger for selected SDK fragments, the renamed receipt adapter and any regenerated installer changes. No generator is run here. Preserve the earlier SHN_COMMON refusal, explicit initializer repair, result-domain correction and completed matrix exactly.

## Future evidence and safe controls

Ten case groups in the machine-readable plan focus on policy-to-receipt composition: predicate boundaries,32-character IDs, stale/pre-open sessions, admitted close, partial failed init, warnings, fixed custody, Core conflicts, authored loader facts and lifecycle/reentry. They are specifications, not passing tests.

Six controls change only safe authored behavior: omit the single pre-open-session veto, omit actual SDK count increment, mis-map a warning's SDK result, erase only failed-init count, bypass hooks_ok, or truncate only the copied32-character owner. Exact named failure lists are specified in the plan and must be frozen with the executable test contract before compilation. A source change that makes a control redundant is a contract defect to fix before running; a crash, timeout, freed route, null original or data race is not an acceptable discriminator.

Selected positive schedules must observe real mutex contention and actual receipt/accounting state, release and join all workers, and avoid invoking non-RX routes. These tests should not rerun the older scalar forwarding/exception/CFI suite. One owned emission is used only when the new composition assertion requires it; an ordinary post-join call would not prove native publication visibility.

The next implementation is a separately versioned offline C claim/receipt composition with this reviewed transformation ledger and authored inputs. No production integration or live composition is implemented or performed by this plan. Arbitrary DSO/slot custody, nonparticipating writers, native caller/loader facts, interposition, signals/nonlocal exits, anonymous unwind, recovery and general lifetime remain open. Preserve x16 tail-transfer and x17/island-unwind limits; action x2 stays opaque, service Source comes only from registers, and no native pointee/post-return reads or cross-entry pairing is introduced. All Local Library activation, frozen staging, physical timing and recovery gates remain closed. Ownership is not fault causation.

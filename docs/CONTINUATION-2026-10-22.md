# October 22 continuation

**November 4 publication correction:** [live metadata, catalog and download verification](evidence/mods-0.1.3-publication-reconciliation/README.md) establishes that MODS Shortcut 0.1.3 was already published on October 18, and the downloaded package exactly matches the preserved archive. Earlier statements here that it remained unpublished were incorrect inferences from the development checkout's stale catalog. This supersedes publication status only: the later false-quarantine counterexample, unresolved delivery/ownership and physical-timing limits remain. No installation, new release or catalog change was performed.

The recurring handoff still described `e79d4b6` and an entirely restored October 17 emulator. Repository history and a new localhost-only inspection show later work. Continue from the existing development branch; do not reset to the heartbeat's old commit or overwrite current emulator files with its old backup.

## Reconciled source and emulator state

Commit `1c3b1c79f9fb7a85d684b450e58f95c7d8f1b95b` saved the [stems composition](../experiments/stems-staging-publication-composition/), [USB intent policy](../experiments/usb-browser-switch/activation-contract/) and [Waveform observer](../experiments/mods-shortcut-delivery/direct-knob-observer/). Later development reached `47ecaf86a5e1d7f54275bfa67db5edd8277c3a4f`. Those are existing contributions, not new test runs in this continuation. Read-only remote verification on October 22 found development at `47ecaf86a5e1d7f54275bfa67db5edd8277c3a4f` and main at `1ff775af5af3c643929b4f2111ac13974e70cd00`; the older heartbeat's main reference is also stale. This continuation updates only the development branch. Untracked `deck-lights/` work is unrelated and must be preserved.

The current emulator contains MODS Shortcut 0.1.3, Grouped Panels 0.3.1, Crate Remote 0.3.0 and other later community mods. Its two original core hashes still match the earlier baseline. [MODS 0.1.3 evidence](evidence/mods-shortcut-0.1.3/README.md) records spare input lanes and emulator-only fault injection; the [source README](../mods-shortcut/README.md) and archived package describe that candidate. The checkout's `azmods-repo.json` still lists MODS 0.1.2. No new release or live-catalog verification was performed in this continuation, and no physical installation is inferred.

The October 17 knob observer's installation script used an ID substring as its readiness check. It therefore recorded installation after staging/restart even though the hook was refused and `init()` returned -20. Later emulator restarts encountered the same retained files. None of those refusals provides native knob observations. The corrected verifier executes 18 host assertions against extracted pinned policy functions and confirms that `azm_patch_ok(func, 1)` rejects the third instruction before island allocation or any code write. Eleven additional checks execute the extracted virtual-slot segment validator with the exact ELF segment facts and discriminating fixture controls. An independent rerun reproduced all 29 assertions and identical metadata. The successful assembly fixture and host queue tests do not imply hook eligibility.

## Cleanup completed

A fresh current backup replaced the stale restoration assumption. The approved selective cleanup removed ten exact observer-owned paths and its tokens from two lists. It preserved 146 remaining regular files, the newer loaded-mod set, unrelated bytes/modes/inventory, both original cores, caches, prior absences and the shared logging flag. The opt-in is absent and the probe is unmapped after a clean empty restart; the normal startup guard expired naturally. [Sanitized evidence](../experiments/mods-shortcut-delivery/direct-knob-observer/runtime-evidence.md) retains the original refusal, harness error and earlier approval-credit interruption. The emulator is released. No physical deck access occurred.

## Encoder dispatcher follow-up

The [dispatcher boundary review](../experiments/mods-shortcut-delivery/dispatcher-boundary/) identifies three different function entries that pass the unchanged pinned instruction policy: `0x1adea38`, `0x1adeee0` and queued executor `0x1add3b8`. The verifier checks 59 exact instruction anchors and executes 66 assertions against extracted installer/overlap policy, including TBZ, short-return and overlap negative controls. An independent run produced identical results. That review established conditional install eligibility. The subsequent observer below accepted one claim at `0x1adeee0`; the other entries remain unclaimed.

That review selected passive entry `0x1adeee0`, reached through Waveform's secondary vtable. It has **five input integer registers** before dispatch, adjusts the view address and can enqueue a copied task. The later observer implements scalar capture of the five inputs, original LR, thread identity and sequence and passes its separate ABI tests, described below. This generic dispatcher also serves other views; entry records alone cannot prove Waveform provenance, target execution, completion, counter ordering or retained view lifetime. The refused Waveform entry and its SDK-ineligible vtable slot remain untouched. The newer alternative route below supersedes this entry as the next observation candidate.

## Dispatcher observer: accepted claim, absent input boundary

The [new passive observer](../experiments/mods-shortcut-delivery/dispatcher-observer/) passes **16,686 assertions per plain, ASan/UBSan and TSan mode**, independently reproduced with identical source/results. Its actual ARM64 wrapper passes 55 assertions; strict assembly controls produce exactly 8, 7 and 2 expected failures. Eleven exact binding checks and SDK compilation pass. Binary SHA-256 is `c791d1f289cc0bad1b912774476e192c1b1614d3f9fa9da42899f679f49edc5e`. Main-thread entries use a guarded SPSC queue; a separate one-shot mailbox permits one off-thread sample without asserting global event order. Independent review caught and corrected signed `gettid()` failure normalization. First-use TLS costs, allocation and hardware timing are not measured.

The [native thread-predicate review](../experiments/mods-shortcut-delivery/dispatcher-observer/thread-predicate/) verifies 58 exact anchors: dispatch compares `pthread_self()` against an owner object's stored thread identity. The observer's `gettid() == getpid()` test is not that native predicate. No live owner identity or predicate result was read.

A fresh localhost backup verified the current 146-file baseline. The SDK accepted the exclusive hook, exact current loaded status and positive ready/one-hook logs passed, and the process remained unchanged through a visually confirmed empty four-deck Waveform screen and one knob detent each way. **No entry or diagnostic record appeared.** The host-injected counter changed 51 → 52 → 51; it is not native counter telemetry. Later reads of the observer's own status found state/live equal to 1 and heartbeat 5,876 → 5,918, with empty queues and zero recorded drops/errors. These sequential reads do not prove historical gating at the input instant, native delivery, view identity, argument semantics or ordering.

[Runtime evidence](../experiments/mods-shortcut-delivery/dispatcher-observer/runtime-evidence.md) preserves the limits and a cleanup harness failure: before stopping or removing anything, cleanup rejected USB probe mode 0777 rather than the requested modes. Read-only diagnostics confirmed `vfat` with `fmask=0000`, matching baseline USB library modes and exact uploaded bytes. A correction limited to the three new probe files allowed selective cleanup while retaining all baseline metadata checks. All 146 baseline files/bytes/tar modes/inventory, settings, existing loaded mods, both original cores, caches, shared logging flag and prior absences were verified. The probe and opt-in are absent after a clean empty restart; the guard expired naturally and the emulator is released. This was a new experiment with its own fresh backup, not a repeat of the earlier refused-probe cleanup. No physical access, community release or core replacement occurred.

The later [source/binary audit](../experiments/mods-shortcut-delivery/dispatcher-observer/source-audit-2026-10-22.md) found no static observer defect that explains the missing records. Completed post-gate captures should leave sequence, mailbox or diagnostic traces even if logging fails, but the silent state/live gates remain a historical uncertainty. The inline status sampler and map listing were not retained as separate artifacts, so its address calculation and read widths have not been independently reproduced. Future sampling must match ELF PT_LOAD mappings to symbol virtual addresses; the observer's RX and RW file-offset displacements differ. Preserve the historical receipt with this limitation.

The new [input-stage fixture](../experiments/mods-shortcut-delivery/input-stage/) executes the pinned SDK's actual filter dispatch and strike functions: 60 assertions pass per sanitizer mode, independently reproduced with identical results, and omitting the per-filter RAW copy reset causes exactly one expected failure. RAW callbacks see checked input before core edits. A normal callback can be skipped by an earlier EATEN or have its observed counter changed by a later editor. EATEN stops the editing chain but does not itself drop the mixer frame. A pair of RAW/normal observations therefore does not establish the final native counter. This is source/host evidence, with mock timing and no runtime input; it does not explain the earlier negative result.

The [native input-route review](../experiments/mods-shortcut-delivery/input-route/) identifies a distinct `HuiAdaptedViewDispatcher` / `ViewTransitionManager` observer at **`0x1b01cf8`**. Its task executor calls child handlers directly through `+0x198`, bypassing both generic entries from the earlier review. The exact ELF verifies 88 instruction anchors, 13 data words, three RTTI anchors and six discriminating controls; the actual pinned hook/overlap policy passes 22 assertions. An independent execution produced identical results. The entry has five scalar input registers and a policy-eligible prologue. That review was static; the subsequent observer below now has a bounded live result. Its live registration recipient and active children remain unproven. The executor's integer equality guard is not proof of a cancellation epoch or retained lifetime.

## Route observer: positive off-thread sample

The [separately bound route observer](../experiments/mods-shortcut-delivery/route-observer/) preserves the five-input ABI and adds independent native attempted/state/live diagnostics plus a passive RAW mixer lane. Its actual-source suite passes **31,427 assertions across 31 scenarios per plain, ASan/UBSan and TSan mode**, independently reproduced with identical results. Two strict controls discriminate misplaced gate accounting and RAW frame mutation. Binding passes 34 checks and 18 rejection controls; ARM64 build/SDK validation passes. The unchanged 55-assertion prior ABI fixture is reused by exact source identity, not reported as a new execution.

One localhost emulator installation accepted its exclusive claim at `0x1b01cf8`. Before input, the fresh RAW baseline was 128 while the saved host-driver counter was 133. A reviewed helper preserved that file and aligned the host-only counter to 128; it issued no guest input. Two BROWSE pulses reached a visually confirmed empty four-deck Waveform screen, followed by two clockwise and two counterclockwise detents. The process stayed unchanged. RAW byte 34 changed 129, 130, 129, 128. Native attempted count reached four; the off-thread one-shot mailbox captured one record with `w3=1`, `w4=1`, full-width `x2` and original LR `0x22c6e08`, then intentionally skipped three further off-thread entries. No main-ring record exists. The sample's TID differs from PID; that is not the native owner-thread predicate. The other three arguments, native returns and child execution remain unobserved.

[Runtime evidence](../experiments/mods-shortcut-delivery/route-observer/runtime-evidence.md) retains nine RAW records and five noncoherent own-module status samples. Native/RAW gate and queue-loss counters were zero in those samples; heartbeat progressed. The new retained sampler checks the exact loaded binary, all ELF mappings, auxiliary-vector page size, read widths and same process identity before/after each sample. Its public offline planner passes 110 checks with three controls, independently reproduced; private AST/mock reviews separately exercise sampler and host recovery failures. Actual maps and scalar bytes remain private. These checks do not make sequential values a coherent snapshot, prove cross-lane causality or explain the earlier negative observer.

All **147 fresh baseline regular files**, bytes/modes/inventory, current settings and loaded set, both original cores, caches, shared logging flag and prior absences were selectively restored. Probe/opt-in absence, a clean empty restart and natural guard expiry passed. The original host counter file (133, exact bytes/mode) was restored without input. The emulator is released. The probe binary was unchanged through host/binding/runtime validation; no core replacement, community release or physical access occurred.

The [observed-caller review](../experiments/mods-shortcut-delivery/route-observer/caller-review/) verifies that LR `0x22c6e08` is consistent with the encoder notification tail route: 71 instruction anchors, eight rejection controls and 14 extracted-policy assertions pass. It does not prove a live source vtable or select between two direct callers. The earlier caller is CBZ-refused; notifier ADRP is policy-allowed but not fully claim-audited. That sampling gap motivated the separate bounded capture below, without event-pointer reads or a new native entry. Keep addresses opaque; do not infer active child ownership, acceptance/completion or cancellation from this entry. Do not repeat the unchanged one-shot sequence to gather the arguments it deliberately skips. Native returns, event ownership, counter ordering, focus handback and view cancellation remain gates before a core-owned menu. Preserve native input, original policy and exclusive hook ownership.

## Bounded route capture: four retained native records

The separate [burst observer](../experiments/mods-shortcut-delivery/route-observer-burst/) uses 16 immutable off-thread slots per process. One source-level strong CAS grants each reservation; losers exit with separate capacity/contention accounting, payload publication uses READY release/acquire, and the worker skips unfinished holes without resetting consumed slots. Slot number is reservation identity, not global/native order. CONSUMED means copied, not logged. The previous observer's 30 tracked files remain unchanged.

Its actual-source matrix passes **32,339 assertions across 37 scenarios per plain, ASan/UBSan and TSan mode**, with six strict negative controls and an independent identical run. Binding passes 47 checks and 30 rejection controls, independently reproduced; ARM64 build/SDK validation passes. Actual-binary offline layout reviews each pass 28 checks for the 27 scalar allowlist. Prior assembly-fixture evidence is reused by exact source identity, not rerun or presented as new execution.

One fresh emulator installation accepted the same exclusive native entry. A host-only alignment preserved the original 133 counter and set the local driver to the observed RAW baseline 128. After two BROWSE pulses to a visually confirmed empty Waveform screen, two clockwise and two counterclockwise detents yielded four off-thread records: signed-32 `w3` interpretations +1,+1,−1,−1, `w4` values 1,2,1,0, and original LR `0x22c6e08` throughout. RAW byte 34 changed 129,130,129,128. The copied x0/x1/full-x2/TID bits are equal across all four; TID differs from PID. These are bounded scalar relations, not live object types, native owner-thread identity or general counter semantics. Native attempts/claims reached four with no main-ring record, nine RAW records and zero sampled loss/gate counters. The five status samples contain 135 individually validated scalar reads; they are explicitly noncoherent and do not read native objects or capture slots.

[Runtime evidence](../experiments/mods-shortcut-delivery/route-observer-burst/runtime-evidence.md) records selective restoration of all **147 fresh baseline regular files**, bytes/modes/inventory, current settings/loaded mods, cores, caches, prior absences and shared logging flag. Probe/opt-in absence, clean empty restart and natural guard expiry passed. Original host-counter bytes/mode were restored without guest input. The emulator is released; no physical access, core replacement or release occurred.

This closes the one-shot sampling gap only for the new bounded sequence. No native return, child execution, queue admission/completion/cancellation or retained event lifetime was observed. Next audit a passive boundary that can distinguish admission from execution and correlate copied values through an owned event lifetime, with ABI and eligibility proved first. Do not repeat this successful sequence unchanged, reuse opaque pointers, weaken SDK policy or open a direct menu from these observations. The stems staging gate remains closed.

## Separate overlay-watchdog order candidate

The [menu watchdog-order patch](../experiments/menu-watchdog-order/) fixes a deterministic one-frame mismatch in the pinned bus dispatcher. Knob handling currently sees the previous `wd_stall`, while click/BACK handling sees the refreshed value: on 200→201 stall it freezes the knob after normal button suppression has expired; on 201→0 recovery it releases the knob while normal button suppression resumes. Moving the existing refresh ahead of both paths makes them use the same frame's stall count. Threshold, saturation, CRC/length eligibility, raw SDK observation, native byte 35 and existing SOURCE handback/pin behavior are preserved.

The actual full read-path fixture passes **978 assertions in each plain, ASan/UBSan and TSan mode**, independently reproduced with identical metadata and extraction hashes. Unchanged source fails exactly 21 named observations of the two transitions in every mode. A full isolated ARM64 build passes; patch SHA-256 is `8f547d57800cf067be3853d3ffe322c443aa1c40ebf9abff14e5152a89fea65f`, binary SHA-256 is `696a6c4b5b26cdc06e24da828de80058ecaaea28c94ad4430f1277685a28b0af`. It has never been installed.

This overlay-heartbeat watchdog is separate from community filter timing/strike quarantine. The serial fixture has inert tone/drum/x-pad callbacks and provides no concurrent ownership snapshot, native handback, runtime or physical-timing proof. Earlier sampling can precede a later overlay update. Keep the patch separate from community packages and test actual-source interactions before composing it into another core candidate. No emulator state was changed in this follow-up.

## USB final value route

The [activation ownership review](../experiments/usb-browser-switch/activation-ownership/) resolves the static final-value question: DeviceLibrary supplies an eight-byte MediaID carrier to its guarded action, constructs a 12-byte `gui::source_select::Source`, and passes it by value to `BrowseSourceSelect::setSource(Source)` at `0x20a1550`. The constructor can normalize an invalid requested media-source discriminator to zero, and calling the final service directly would bypass the guarded DeviceLibrary checks. This is an argument/dispatch finding, not an activation API.

The verifier checks 87 exact instruction anchors, RTTI/slot relationships, ELF identity and unwind extents, plus 20 assertions against the extracted pinned instruction policy. The guarded action `0x1be7e38` and named service entry have eligible prologues; live receiver identity, ownership, island allocation and complete wrapper ABI remain unverified. The timer-listener slot remains SDK-ineligible because it lies in executable `PT_LOAD`; its original function is a four-byte RET.

Both queued timer and source tasks copy raw owning-object pointers. The direct and queued listener paths can read those objects again after callbacks. Native lifetime rules may exist outside the inspected scope, but no retain/serialization contract for custom navigation has been proved. Next audit ABI/ownership for a passive correlation of the guarded action and value-service entry, preserving all native checks and leaving absent boundaries UNKNOWN. Do not call the service or retain an observed pointer for later use. No emulator or physical access occurred.

## Remaining development gates

The following offline reviews were added after the bounded four-record run. They required no new emulator session and do not change its restored/released state.

### Native task ownership and admission

The [capture/executor review](../experiments/mods-shortcut-delivery/route-task-ownership/BOUNDARIES.md) verifies 133 exact instruction anchors, eight data words, eight CFI rows, 22 rejection controls and 61 assertions against extracted pinned hook/overlap policy. Root independently reproduced identical JSON. The executor `0x1b029c0` and submission entries `0x237a440` / `0x237eac0` pass the unchanged static prologue policy; no claim, wrapper ABI test or runtime observation was attempted.

The task copies a raw adjusted dispatcher pointer, full ID and two integer values. Its task-specific destructors do not release those captures; their counter is specialization-global. The dispatcher constructor and executor use the same cached sentinel, with a destructor zeroing store. That is not a task cancellation epoch, allocation validity check or retained lifetime. The executor tries two children then the dispatcher through `+0x198`, tests child returns only in the low byte and tail-dispatches its final call. No unified accepted/completed result is established.

The companion [task-box audit](../experiments/mods-shortcut-delivery/route-task-ownership/TASKBOX.md) verifies 199 instruction anchors, 12 data words, four RTTI names, six diagnostic names, the `pthread_self` relocation and 11 controls. It traces constructor-installed TimerTaskBox submission to an embedded AsyncTaskBox queue; successful insertion returns a full-width TaskNo. Later execution and freeing are separate, and destruction can free pending tasks without executing them. A configured auxiliary cancellation mechanism can skip execution by TaskNo. Neither the earlier route records nor this static audit establishes live admission, execution, cancellation or owner-thread identity. Queue storage/memory-pool ownership is distinct from retained dispatcher/child lifetime. Consumption may occur before posting returns, and optional notification can introduce other calls; never read the task after native return. Direct-call checks do not prove absence of transitive or indirect behavior.

The next offline step is now implemented below. Bounded capture, actual-source integration and teardown remain requirements before any new emulator installation. Keep absent execution UNKNOWN and never treat a post return as handler completion. No direct menu or navigation service is implemented by this audit.

### Entry-local values and standalone call/return ABI

The [task capture contract](../experiments/mods-shortcut-delivery/task-capture-contract/) copies only TaskNo, opaque ID and the two integer bit patterns before forwarding. Its five disjoint reads cover 24 payload bytes plus the eight-byte vptr; the dispatcher field, tag, padding and tail remain unread. A separate 64-byte context belongs to each observer invocation. Return recording accepts only this context and full result bits, including after the synthetic task allocation has actually been unmapped. Caller LR/type/alignment checks are rejection filters, not a native memory validator. Entry borrowing depends on valid, immutable task storage for the complete copy, and the context stays private until complete.

Six serial host scenarios pass **67 assertions per plain, ASan/UBSan and TSan mode**, independently reproduced with identical results. Deliberate return truncation fails exactly one check; dispatcher overread fails exactly two, in every mode. Duplicate tuples, zero and wrap-like TaskNos, missing boundaries and scripted nested invocations retain weak value-only relations. Caller-provided run/invocation uniqueness is a precondition; no allocator, automatic event join, bounded queue, logging-loss or teardown protocol exists. TSan does not turn this serial fixture into a concurrency proof.

The separate [ARM64 call/return wrapper](../experiments/mods-shortcut-delivery/task-call-return-abi/) executes actual assembly against synthetic callees in a pinned local ARM64 container with networking disabled. **319 assertions pass**, including full return/register preservation, 64 calls across four threads, helper reentry, four nested native calls, and one native exception that unwinds three nested invocations and four destructors while bypassing the after helper. Five executed negative controls discriminate original LR, observed/caller return width, errno restoration and context initialization; two compiled CFI mutations are rejected by metadata checks without execution. The wrapper's 416-byte frame and before/after 64-byte value context are independently scoped from the capture helper. The native callee sees a new wrapper return LR and `x16` branch scratch; this is not transparent instrumentation for arbitrary functions or stack arguments.

An independent ARM64 run reproduced all assertions, control results and the wrapper object byte-for-byte. The linked synthetic test ELFs differ only in six compiler-temporary filename characters in `.strtab`; both hashes and the exact comparison are retained in its review. This is one thrown exception through three native invocations, not three separate throws. No sanitizer instrumentation was used for this ARM64 execution.

At that checkpoint neither artifact had been combined or installed. The follow-up below now supplies bounded publication, an executor-specific adapter, combined synthetic tests and a pinned installation/lifetime audit. It still adds no native observer or installation. No native submission return, executor entry, admission/completion/cancellation or retained dispatcher lifetime was observed. The earlier emulator restoration remains unchanged. MODS 0.1.3 remains unpublished and the stems staging gate stays closed.

### Bounded task publication and combined synthetic ABI

The [value transport](../experiments/mods-shortcut-delivery/task-value-transport/) wraps the unchanged capture header in fixed, never-reused slots. One source-level reservation CAS assigns an observer invocation ID; a separate release/acquire READY protects each entry and normal return. Failed native capture consumes a reservation with a rejection entry but grants no return context. Disabled, wrong-caller, full and contended attempts read no task bytes. `finish` uses only the pre-copy and full result, including after synthetic task unmapping. Matching native values remain candidates, never identities or completed events.

All **220 assertions across nine scenarios per plain, ASan/UBSan and TSan mode** pass, independently reproduced with byte-identical results. Four serial strict controls distinguish early publication, duplicate return rewriting and result truncation with exact 1/1/2/2 failures per mode. Concurrent payload/return readers acquire records before producers finish rather than relying on joins. Diagnostic counters are noncoherent and can lag publication. A begin already past Open can publish after close, and an existing return can publish after close; permanent storage and fresh run identity are caller obligations. No reuse, reclaim or automatic event join is implemented.

The [hook-lifetime audit](../experiments/mods-shortcut-delivery/task-hook-lifetime/) uses pinned core source and exact ELF metadata. **56 assertions across ten serial scenarios in plain and ASan/UBSan** pass with an independent identical result and two strict controls. The late installer seals its one-instruction trampoline before storing the supplied original slot and redirecting entry. That original store is relaxed; its ordering before a release instruction store is not a C++ acquire/release proof for another core's instruction fetch. Use permanent per-hook originals directly, configure capture before claiming and keep capture closed until both claims succeed. A failed second claim leaves the first forwarding path live. The SDK/loader retains opened libraries and successful hooks until restart; logical capture close is not draining or unhooking.

Exact continuation FDEs are compatible with the inspected covered instructions once control returns to the native body. Anonymous island/trampoline PCs have no demonstrated unwind registration, and the installed path uses X17 scratch in addition to each wrapper's X16 transfer scratch. These static/host facts do not prove installed-path execution or asynchronous unwind behavior. Both matching vtable slots lie in executable PT_LOAD and remain ineligible; no policy is changed.

The [combined ARM64 fixture](../experiments/mods-shortcut-delivery/task-observer-integration/) executes the actual capture/transport headers, unchanged submission assembly and a new one-input executor tail adapter against synthetic callees. It passes **239 assertions across 16 scenarios**, four exact executed controls and two compiled CFI rejection controls. It checks independently visible entry-before-return, execution before post return, actual synthetic-task unmapping before after, exactly-once native forwarding on rejected/full/disabled capture, reentry, eight submissions across four threads, and one executor exception unwinding three submission invocations and five destructors with no after callbacks. The executor adapter restores SP/LR before its tail transfer; native return bits have no executor meaning.

Root review found a plain destructor counter written by concurrent test calls. It is now atomic; the earlier passing author and independent integration runs are preserved as superseded evidence, not accepted concurrency validation. The corrected matrix passed independently with identical candidate totals and strict failure sets. The wrong-LR control captured seven concurrent records versus eight in the author's run, a permitted contention drop with native forwarding preserved. The executor object is identical; full test ELFs differ only in three compiler-temporary names in `.strtab`. No sanitizer instrumentation is used for ARM64 execution; transport sanitizer results remain separately scoped. Fixture helpers map exact synthetic BL return labels to the capture constants, so this tests the connection without asserting firmware caller provenance. Requested copy ranges are instrumented, not a proof of default memcpy machine-load extents.

The next step is a separately scoped passive SDK adapter with permanent original slots and transport storage, explicit configuration/partial-claim gates, nonthrowing helpers, exact caller/firmware binding and an owned value-only reader. Test that actual adapter's success and failure paths before a new emulator claim; the synthetic original-pointer publication does not prove the installed path. Preserve native forwarding and existing hook ownership, leave absent/matching boundaries ambiguous, and never read native task storage after submission returns. Live admission/execution/cancellation, task borrowing and dispatcher/child retention remain unresolved. No emulator, physical device, core, package or catalog changed in this follow-up.

### Unpublished MODS 0.1.3 delivery diagnosis

The [new actual-source counterexample](../experiments/mods-shortcut-lane-observation/) compiles the pinned complete shortcut with actual SDK registration, dispatch and strike bodies. A serial script places UI watches between measured callbacks of a single frame. The mod emits three “switched off by the core” diagnostics and moves the active lane while every core registration remains loaded with zero strikes and `off=0`. All callbacks resume and local off labels clear without restart. The prior emulator off-flag injection demonstrated one cause of missed delivery, not a reliable quarantine classifier.

Four scenarios pass 82 assertions per plain, ASan/UBSan and TSan mode, independently reproduced with identical results and 25 source/extraction checks. An additional false authoritative-quarantine hypothesis fails exactly one of 83 assertions per mode. Whole-frame delivery and actual eight-strike SDK quarantine are discriminating controls. This serial fixture uses mock time/services, has no pending request or pulse, and measures neither scheduling frequency nor physical timing; it is no concurrency or log-transport proof. The original 0.1.3 source/archive is retained. Current documentation withdraws inevitable-overrun and multiplied-lifetime claims. Keep the candidate unpublished until delivery semantics, watchdog intent and safe lane handover are resolved. Do not attribute the historical accepted-then-cancelled tap to this counterexample.

### Gates retained

- **Direct menu:** the exact Waveform handler entry is blocked by the existing hook policy, and the SDK's virtual-slot service rejects its slot because the firmware segment is executable. The earlier generic dispatcher observer captured no entries; the separate burst observer now captured four off-thread scalar records for four attempted entries. Its live receiver/child, general counter semantics, native returns and completion remain unproven. Preserve original dispatch, page protections and exclusive ownership. Establish live provenance, ordering, arguments/returns and event ownership before opening a core-owned menu or consuming input. No policy weakening or native-input takeover.
- **USB buttons:** V4's first-selection request path is understood, but readiness and owned activation remain open. The pointer-free intent broker passed 731 assertions per sanitizer mode. The static final-value route is now identified above; live Local Library/MediaID correlation and view/timer/service lifetime still need proof. No activation adapter or USB buttons exist.
- **Stems:** the composition passes 336 ordinary and 1,020 staging assertions per mode and a full ARM64 build. Private suppression guards and negative controls are implemented, but it has never been installed. The historical ordinary-restart ASan failure remains unexplained, so the staging runtime gate stays closed. No authoritative pause, accepted same-track LOAD signal or selected explicit-token activation exists. Do not repeat unchanged passing loops or infer intent from metadata/masks.
- **Phrase grid:** retain the isolated consumer/Drum Roll results; delayed native-grid recovery and physical Deck 2 attribution remain unproven. Test explicit interactions when a candidate core is composed.

Keep core patches separate from community packages, preserve rollback and all official protections, and keep raw backups, logs, scripts, firmware and music private. Use only the emulator for further runtime work; Henry performs physical installation himself.

### Passive SDK adapter: offline integration complete, runtime still pending

The new [task SDK observer](../experiments/mods-shortcut-delivery/task-sdk-observer/) uses the unchanged, previously tested submission/executor assembly and capture/transport headers in a separately identified, unpublished module. Both original function slots and all capture storage live for the process lifetime. A worker starts before any claim; capture stays closed until both claims succeed. A partial claim failure preserves any live forwarding target. A worker failure during initialization prevents readiness, and fatal worker exit after readiness closes observation without reclaiming storage. Late normal returns remain value-only.

The actual-source host matrix passes **660 assertions across 41 fresh-process scenarios per plain, ASan/UBSan and TSan mode**. Five strict controls fail exactly 1/2/2/1/1 named checks per mode. Independent execution produces identical result JSON (`53c8770200bdccfece5dc9b1c08ef3b88c9dfaa42b3f59e015669599cfb7a227`). Tests mock the SDK, static memory, TID, sleep and logging, and enter helpers directly; they do not execute native wrappers or firmware. Concurrent readers consume records before producer acknowledgement/join; earlier unpublished holes are revisited after close. The suite also unmaps synthetic task storage before the return helper. It is neither a whole-engine race proof nor native retention evidence.

Exact adapter binding passes **96 checks and 52 strict rejection controls**, with 44 instruction anchors, five data words, two RTTI anchors and independent identical JSON. Wrapper/dependency source hashes reuse the frozen earlier ABI/policy results without rerunning their unchanged suites. The pinned, network-disabled ARM64 build and SDK validation pass; binary SHA-256 is `71d7fc088f4def67d1307e4faeef301020aed04bfa3da7ae4bc6a422ece3e255`. Build output and immutable source/run snapshots remain private. No package was made or published.

Worker output is bounded to one void log-service attempt per entry/normal-return record, plus bounded diagnostics and initialization/closure events. Those calls ordinarily perform synchronous worker-side filesystem I/O; they do not acknowledge delivery. Missing output remains UNKNOWN. `tick` only increments a module-owned heartbeat. Five quiet or 120 active worker iterations can close observation; these are scheduled iterations, not wall-clock guarantees or quarantine evidence. Closed capture may still receive already admitted entries and late returns. It is never rearmed; the worker normally keeps scanning until process exit. Own-module worker-cycle and exit-reason diagnostics distinguish a retained reader from a failed one, without inferring native producer quiescence.

Next prepare and validate exact-binary own-module scalar sampling, confirm exclusive hook ownership/current core identity, and review a fresh selective emulator backup and cleanup sequence. A future bounded passive run must establish fresh loaded-binary/readiness evidence before input and distinguish submission values/normal result from executor entry. No return observation proves handler completion, and missing execution does not prove cancellation. Do not read native task storage after return, retain opaque pointers, reclaim observer storage, retry blocked Waveform claims or add navigation/input consumption. The emulator was not contacted in this follow-up; its last restored/released state is unchanged. Stems staging stays closed and MODS 0.1.3 remains unpublished.

### Scalar sampling and controller baseline: preparation checkpoint

The [new preparation evidence](../experiments/mods-shortcut-delivery/task-sdk-observer/PREPARATION-EVIDENCE.md) and [sanitized result](../experiments/mods-shortcut-delivery/task-sdk-observer/preparation-result.json) retain an exact-binary eight-scalar planner (56 bytes; 81 checks including 54 rejection controls) and emulator-driver identity/frame policy (99 checks including 51 controls). Each public result was independently reproduced byte-for-byte. Private actual-function mocks add 253 startup/recovery/sampling checks across 48 scenarios. These checks neither sample live observer memory nor prove full selective recovery.

One localhost-only diagnostic was completed. Its first preflight stopped before mixer acquisition because the guest lacked `stat`; partial metadata and that failure remain private and do not constitute validated identity. A revised collector uses a strictly parsed, non-extracted tar character header. Its final 1,440 checks across 82 mock scenarios include zero-acquisition failures and no-retry post-acquisition failures. An earlier fixture-normalization error is retained separately. The changed collector then acquired one 128-byte emulator MISO frame, verified CRC, and matched before/after loaded-module, disk, device, sysfs and parameter identity. A separate local saved-receipt audit passes 36 checks. Same-empty-process and original-core guards passed; separate raw postcheck core/empty responses were not retained. The observations are sequential and not bound to the acquisition's open descriptor. One acquisition command is not proof of exactly one kernel read.

No observer was installed, no input was sent, and no service, core, host counter, package or catalog changed. No rollback/restart was needed and no physical access occurred. The copied counter is not reusable for a future action. The observer source/build and prior results remain unchanged; native task observations and event ownership remain absent. Next complete and test fresh action-bound progress/counter acquisition, single-step host-counter ownership and independent host restoration, then use the fresh selective runtime plan for a bounded passive observer run. Do not use the old RAW aligner, old progress receipts, old restore points or an input-review file's existence as readiness. The startup guard still expires naturally; no capture rearm or watchdog change is authorized. Stems staging stays closed, MODS0.1.3 stays unpublished and all other unresolved gates remain.

### Action preparation: component checks complete, coordinator gate retained

The [new action checkpoint](../experiments/mods-shortcut-delivery/task-sdk-observer/ACTION-PREPARATION.md)
adds a pure152-byte frame/settings policy (245 checks,222 rejection controls,
independently identical) and host-only counter ownership/recovery (959 checks
across179 scenarios, three strict controls, independently identical). Recovery
accepts only the latest owned state or uncertain rename endpoints, preserves
snapshot-only runs, and rejects external/historical-value drift. A fresh
status/frame/frame/status preflight binds one-use permits to current run,
process, mixer settings and bounded timestamps; private mock evidence and
scope are retained in the checkpoint. No input or native observations follow.

Installation review also reproduced approval-table eviction at capacity and
added explicit space and exact per-target claim checks. The old EEEE substring
is replaced with a current-PID, exact-shape, fresh empty-marker check. The old
combined-BROWSE/two-detent adapter and review-file enablement are removed.
Installation/input now fail before connection until the coordinator composes
fresh permit consumption, atomic host target, and a retained at-most-once
neutral release after uncertain press delivery. Diagnostics/restoration stay
independent of readiness. Next implement and fault-test that composition,
then use a fresh selective baseline for one bounded passive run; do not repeat
unchanged component suites or earlier cleanup. This follow-up used no emulator
or physical connection, changed no runtime/core/package/catalog, and preserves
all staging, activation, publication and hardware gates.


### Input coordinator complete offline; fresh compatibility refused

The [coordinator checkpoint](../experiments/mods-shortcut-delivery/task-sdk-observer/COORDINATOR-EVIDENCE.md)
now composes the frozen one-use preflight and host ownership helpers with exact
retained payloads, target-before-first-call bookkeeping, one-shot transport and
an at-most-once neutral release after uncertain first delivery. A cooperative
local lock spans the whole action and runner transitions. Its actual-source
mock suite passes 2,697 checks across 70 scenarios (67 candidate plus three
strict controls), independently identical. Pre-install compatibility passes 231
checks/58 scenarios plus two controls; startup wait passes 74/58 rejections/two
controls; local prerequisites pass 132/118 rejections/three controls; runner
wiring passes 95/27 rejections. These scopes and retained fixture failures are
explicit in the checkpoint. No native delivery follows from these results.

A fresh localhost read-only attempt verified 147 baseline files, ten loaded
mods, both original cores and the same empty process. It refused the saved
host mixer state before the first full-frame acquisition: 54 entries versus 28
reviewed offsets. Of 26 extras, 22 equal pinned idle; four differ, including
packed analog low bits and the FX TIME counter according to local preview
source. No state was normalized or discarded and the frozen policy is intact.
A final comparison verified the baseline files/modes/inventory, loaded mods,
cores, caches, absences, process and host counter/mixer unchanged. There was no
input, observer installation, restart, guest restoration or physical access.
The snapshot-only host receipt was closed without a counter write; the emulator
is released. No enabling review manifest exists, and this run cannot be reused.

Next design a separately versioned complete-overlay preservation policy for
all supported packed fields and independent counters, with unknown controls
rejected and actual-source interaction tests. Only then use a new run identity,
fresh backup and external source review for another bounded compatibility
attempt. Do not repeat this refusal unchanged or widen offsets blindly. A
process crash can prevent Python finally, and restart/file restoration does
not clear or prove restoration of a sticky kernel controller overlay. No
automatic replay/reset is added. Native task provenance/returns/ownership,
MODS 0.1.3 publication, stems activation/staging, USB activation and hardware
validation remain unresolved.


### Complete-overlay V2 preservation checked offline

The separate [V2 policy and source audit](../experiments/mods-shortcut-delivery/task-sdk-observer/overlay-policy-v2/)
account for all 54 supported saved offsets: 40 analog high bytes, 11 packed-low
bytes, FX selector and the two independent FX TIME bytes. Unknown keys, partial
FX TIME pairs, all four held FX button bits and reserved analog bits are refused.
The policy never strips or normalizes a saved setting. The source audit passes
147 checks/29 rejection controls; pure-policy checks pass 2,914, including 2,603
refusals and four strict mutations. Both results independently match exactly.

Three separately named private adapters now retain the exact second validated
152-byte observation and construct the first/release pair from that copy. Their
[new integration checkpoint](../experiments/mods-shortcut-delivery/task-sdk-observer/overlay-policy-v2/INTEGRATION.md)
records 1,864 actual-adapter mock checks, 60 refusals and four strict mutations,
plus 1,317 focused checks with the actual frozen host-counter helper and real
temporary files. Independent results match exactly. The latter checks complete
alignment/BROWSE/CW/CCW and an uncertain-first-call release/stop path, each with
exact host restoration. External observations, clock, transport and the sticky
overlay/CRC model remain mocked; coordinator scope/locking is stubbed and not
newly proven. These are offline interaction results, not native observations.

Original helpers, the old policy, runner and closed refusal remain unchanged.
No V2 runner selection, enabling manifest, new runtime identity, emulator
connection, input, restart, core/package/catalog change or physical access
occurred. The emulator's last released state was not revalidated or modified.
Raw scripts, frames, settings and immutable input/result snapshots stay private.

Next resolve competing full-overlay writers, including queued preview frames,
and review exact V2 runner/recovery wiring before selecting a new run identity
and taking a fresh selective backup. The cooperative lock does not exclude
those writers. CRC restamping depends on the checked but writable prefix-length
parameter; sequential checks do not hold it fixed. Passing preservation tests
do not prove current overlay ownership, native delivery or crash recovery.
Do not reuse the closed run or repeat its refusal unchanged. Direct task
provenance/returns/ownership, MODS 0.1.3 publication, stems staging/activation,
USB activation and hardware validation remain unresolved.


### Writer replay and recovery counterexamples; V2 runner stays inert

The new [writer/recovery checkpoint](../experiments/mods-shortcut-delivery/task-sdk-observer/overlay-policy-v2/RUNTIME-GAPS.md)
executes exact extracted preview sender code with real queues and fake transport.
Its 17 assertions and three strict controls reproduce queued messages after
producer disconnect, cached replay after reconnect and retry after an uncertain
write. The accompanying 13-source audit passes 58 checks and 18 rejection
controls; independent results match exactly. Neither the cooperative coordinator
lock nor quiet process/socket samples enforce overlay/CRC ownership. Closing
preview can stop its QEMU; stopping the forwarder affects other outputs and
leaves direct writes possible. No process or service was changed.

A separate actual-source recovery regression reproduces held input surviving
the current runner's automatic restart, plus a neutral target counter remaining
different from the successfully restored host original. It extracts two runner
functions and the kernel write/read/open/close/CRC functions, executes the actual
host-counter helper, and mocks process/services/file-cleanup evidence. It does
not execute the whole restore, guest kernel or native application. The earlier
host-file proof remains valid within its scope; it is not an overlay handback
proof. Historical failures and earlier results remain retained.

Separate private V2 runner/planner candidates pass 151 checks/89 scenarios,
including 86 rejection scenarios and four strict controls, independently
identical. They validate 19 selected task/transport Python sources, frozen build
and proposed fresh scope. Every live dispatch, including recovery, is disabled.
No live scope, runtime ID or enabling manifest was created, and no old helper
was changed. No emulator/physical connection, input, install, restart, core,
package or catalog change occurred; the last released state was not revalidated.

Next implement a separately reviewed emulator arbitration route covering final
mutation, CRC and queued-generation handback, then an owned stopped recovery
that verifies the complete original neutral overlay and original host bytes
before guarded restart. Unknown overlay state must not fall through automatic
restart or completion flags. Future imported-module identity also needs binding.
Only then consider a new run and fresh selective backup. Do not repeat unchanged
passing suites or reuse the closed refusal. Native task ownership/returns,
MODS 0.1.3 publication, stems staging/activation, USB activation and physical
validation remain open.


### Emulator overlay ownership phase 1 — offline candidate

A separate [emulator-kernel patch](../experiments/emulator-overlay-ownership/)
now checks mixer ownership at the final mutation boundary. A default-off opt-in
permits a privileged writable open-file description to acquire an epoch; tagged
full-frame writes require that file, run ID and exact next sequence. Legacy
mixer writes/clears are fenced after acquisition, including requests copied
before it, and clear-all refuses atomically. CRC updates parse privately before
locked commit; frame and CRC reads use one locked snapshot. Explicit release
and final close preserve the overlay and permanent fence.

The actual-source host suite passes 146 assertions in each of plain, ASan/UBSan
and TSan modes, with seven exact failing mutation controls per mode and 17
source integration checks. Independent full runs match byte for byte. Strict
ARM64 compilation/modpost passes with no warnings, and all 33 undefined symbols
resolve in the retained kernel export table. The final module hash is
`bfc33fb721cf292b1472ac4c9cca6687a818624f5147f31f18b17a3c4503aaa2`.
An initial build revealed missing CRC parameter-type metadata. Its one-line
correction restores the original entry exactly once; all five executable
sections remain byte-identical, independently checked. Both build/test
checkpoints and the earlier fixture compile failure remain retained privately.

This is a partial arbitration candidate, **not an installed module or a usable
producer handback**. Owner loss can leave held input behind a permanent fence.
The next work is an explicit module-instance/producer-generation handback route
that cannot revive queued input, followed by owned stopped recovery verifying
the complete original neutral overlay and exact host bytes before restart.
Do not install this phase or wire the inert V2 runner on the strength of host
and build results alone. No new live identity or enabling manifest exists.

Only isolated local build/test work occurred; no emulator/device connection,
input, service action, restart, installed core/module/package/catalog change or
physical access occurred. The last released emulator state was neither changed
nor revalidated. All old runner/policy helpers and the closed refusal remain
unchanged. Native task provenance/returns/ownership, MODS 0.1.3 publication,
stems staging/activation, USB activation and physical validation remain open.


### Instance-bound generation rotation — offline successor, October 27

A separate [successor patch](../experiments/emulator-overlay-handback/) applies
**after** the frozen phase-one ownership patch. It generates a nonzero 128-bit
module instance before publishing devices on the opted-in path; wait failure
or a zero result aborts initialization. V2 tagged mutations bind that instance,
file, run, epoch and sequence. V1 command sizes are refused. Same-broker ROTATE
commits all 152 opaque bytes, increments the epoch and resets sequence to zero
under one lock. It retains file/run ownership and the permanent legacy fence.
No preview, forwarder or runtime adapter was changed.

Fourteen extracted functions pass 228 host assertions and ten exact failing
controls per plain, ASan/UBSan and TSan mode, with an independently byte-identical
result and 22 integration checks. The new schedules reject old copied commands
after rotation and expose two limits: an earlier low-sequence STATE does not
cancel a pending command, and a broker can still retag an old payload as new.
Kernel source review and strict ARM64 compilation/modpost pass without warnings;
all 35 actual undefined symbols match the pinned export table. Independent ELF
parsing confirms the two RNG additions and unchanged parameter metadata. The
private binary is `bf8293addeb6570498965d4958db849c5e79b7e40f8f600772ed825f878a3184`.

The producer review now specifies creation-time generation tags, quiescence of
queued and saved-state writes, serialized request/ack accounting, and verified
baseline adoption before rearming preview input. None is implemented. The kernel
cannot retract native events already sampled before rotation, authenticate an
upstream producer, or detect restored/cloned VM identity. Its random instance
provides probabilistic ordinary-load separation only.

Stopped recovery remains unimplemented. In addition to full original neutral
bytes and exact host state before guarded restart, it must preserve injected/idle
mode: writing idle-looking bytes sets INJECTED and cannot restore a baseline
that had that mode off. The documented narrow admission requires an already
injected baseline, or a separately reviewed tagged mode-restoration operation.
No automatic restart or old cached input is authorized by a passing byte check.

Next implement and test the broker/producer admission path against the actual
queue/reconnect sources, then the owned stopped-recovery transaction. Keep both
kernel candidates uninstalled and the V2 runner disabled until those gates pass.
No new live run, enabling manifest, emulator/device contact, service action,
physical access, core, package or catalog change occurred. The last released
emulator state was not revalidated; old helpers, frozen patches and unrelated
untracked work remain intact. All native menu, stems, USB and physical gates
remain open.


### Offline broker and producer gate — October 27

The separate [mixer-broker candidate](../experiments/emulator-mixer-broker/)
now implements an offline C broker and a standalone Rust producer gate for the
frozen V2 kernel primitive. The broker serializes its immutable backend context,
binds complete grants and contiguous request IDs, returns exact duplicate
receipts without backend calls, and blocks generation changes while a result
is unknown. An exact positive state can reconcile one pending operation under
the same-file/sole-mutator assumptions; lower state is not cancellation and
reconciliation never reopens the old grant. Rotation and fresh baseline adoption
remain explicit operations.

The C suite executes seven exact extracted kernel functions and passes 203
assertions plus six strict failing controls in each plain, ASan/UBSan and TSan
mode. Independent final results match byte for byte. The Rust gate passes 19
named tests plus eight strict failing controls, independently reproduced with
identical sources and functional results. Its failure-output hashes differ only
in runtime panic thread IDs after the existing path/duration normalization;
that distinction and both original results are retained. The C library also
compiles as an ARM64 relocatable object, independently checked as ELF64 AArch64,
with 14 recorded unresolved dependencies. It was neither linked nor executed.

The producer stamps immutable envelopes at creation, preserves a canonical
bounded queue, retains one unresolved request across connection loss, and
requires exact baseline ACK plus caller-observed release before rearming.
Executable counterexamples preserve two limits: callers can reauthor old raw
bytes under a fresh grant, and settings callbacks within one generation can
execute out of creation order. C requests remain mutable caller-owned values.
Neither library establishes backend/receipt authority, fresh physical gestures,
absence of older broker operations, durable receipt history or native delivery.

Next integrate the producer/broker adapter with the pinned actual preview's
creation, queue/cache, reconnect and saved-settings paths. Establish event/ACK
ordering and invalidation of old UI state without changing other buses or
forwarder outputs. No C/Rust wire/FFI adapter, broker daemon, preview patch or
owned stopped-recovery transaction exists yet. The recovery contract still
requires original neutral overlay, injected mode and exact host bytes verified
before guarded restart. Both kernel patches remain uninstalled and the V2
runtime runner stays disabled.

Only isolated source, host tests and compile work occurred. No emulator/device
contact, service action, physical access, package/core/catalog changes or new
live run occurred. Old helpers, patches, released emulator state and unrelated
untracked work were preserved. MODS 0.1.3 publication, native menu ownership,
stems activation/staging, USB activation and physical validation remain open.


### Inactive actual-preview integration — October 27

The separate [preview-gate patch](../experiments/emulator-preview-gate/) now
composes the pinned emulator App/control classifier with a copied producer and
an injected in-process backend/settings seam. The original emulator checkout,
frozen broker libraries, kernels and recovery helpers remain unchanged. Main
supplies `None`, preserving legacy behavior; `Some(closed route)` has no bus-2
fallback. There is no live adapter, enable flag or broker daemon.

App captures its generation before UI mutation and validates it at creation.
Full baseline adoption resets all mixer models, including Gray FX SELECT and
independent FX TIME bytes, and only mixer gestures. ERP/deck/SHIFT state survives.
A focused event-free release-only update cannot create mixer input. Typed owned
requests bypass legacy cache/replay and the old optimistic counter-file paths.
Exact positive ACK and settings callback are serialized; failure after a known
native commit cannot make it eligible for resend. Unknown work stays retained
and closed. `sent_mixer` is enqueue deduplication, not acknowledgment.

Production cfg compiles offline on the macOS ARM64 host. The actual crate passes
37 selected tests (35 new plus two retained App cases) and 12 strict compiling
negative controls, with independently reviewed evidence in the experiment.
Tests execute headless egui handlers and actual changed gate/route/classifier
methods with synthetic in-memory receipts, not main, socket worker or filesystem
settings. The full binary lists 95 tests; 58 unrelated/inherited cases are
filtered. Locked sources, full private run snapshots and sanitized results are
retained. Nine expected inactive-code warnings are not suppressed. No Linux
ARM64 emulator/core, sanitizer, native timing or audio claim is made.

The real backend and host writer remain unimplemented. Synchronous callbacks
can delay other bus sends and App creation/close. The external mpsc queue remains
unbounded across repeated generations, and UI-time MIN_HOLD does not guarantee
native press duration after delayed delivery. UNKNOWN has no route query API.
Baseline authority/neutrality and global producer quiescence remain external
obligations; malformed positive ACKs retain pending work even though their
uncertain diagnostic flag stays false. These limits are not a runtime permit.

Next resolve bounded transport/lifecycle and native dwell semantics, then the
ordered host writer and owned stopped-recovery transaction. Keep original full
neutral overlay, injected mode and host bytes/modes verified before guarded
restart. No emulator/device access, cleanup, service action, installation,
physical control, package or catalog change occurred. Preserve untracked work.
MODS publication/native menu ownership, stems gates, USB activation and physical
validation remain open.


### Inactive bounded preview transport — October 27

The new [transport successor](../experiments/emulator-preview-transport/)
removes owned mixer envelopes from the legacy mpsc channel. One dedicated
worker uses a bounded mailbox, counting queued and reserved/in-flight values.
Capacity check, canonical creation and insertion serialize together. Generation
closure drops unsent work; a resolved route cannot adopt a new baseline while
its mailbox reservation remains. Other bus classification has no owned callback
or route reference, so it can progress while the synthetic backend/settings
callback is held.

A terminal flag and separate coalesced wake token do not require a payload slot.
Drop requests stop only; an explicit consuming join proves local thread
completion and distinguishes normal stop from panic/poison. Work reserved before
stop can still reach the backend. Uncooperative callbacks can prevent join;
no cancellation, native quiescence or held-overlay release follows from a stop
receipt. Exclusive use of one route by one port family is a trusted constructor
contract, not type-enforced ownership. Creation, inspection and explicit
close/stop may block; only UI key/baseline observations use nonblocking attempts.

Review found a draft App bug that closed the staged generation on transient
confirmation Busy. The corrected App keeps it pending and waits for another
clean update. A test-only local mutex forces the actual WouldBlock branch;
the exact staged receipt survives and the eventual confirmation update still
cannot create input. Endpoint Busy is also deferred, without a separate executed
endpoint-contention App schedule. The earlier combined draft compiled but was
superseded before testing; its source/log are retained privately.

The final patch is
`d630fd6c7b15b8d0f10f0020a9079833b42f839b8d7c1e84bffdbe833e1f485c`.
All 17 prepared files are pinned; 13 parent files including producer, route,
config and lockfile remain byte-identical. Production host compilation succeeds
with 32 visible inactive-code warnings. The actual crate passes 37 selected
cases (15 App, five classifier, 17 transport), with 78 of 115 listed cases
filtered. All 11 compiling controls fail exactly the intended source assertion,
including line/column, name, count and exit code; timeout or unrelated failure
cannot pass. Tests use headless egui, local channels and injected callbacks;
main, the window, socket loop and filesystem settings are not executed. No Rust
sanitizer, Linux ARM64 runtime, audio or physical timing claim is made. An
independent fresh source export and full selected run match every functional,
source, compiler and exact-assertion field; only raw path/time/thread log hashes
are excluded from comparison. See the experiment
[comparison](../experiments/emulator-preview-transport/comparison.json).

A separate [dwell counterexample](../experiments/emulator-preview-transport/DWELL-BOUNDARY.md)
extracts six unchanged functions from the frozen V2 kernel candidate. Two
successful full-overlay commits before a SPI read leave only the latest value;
inserting a read between commits distinguishes both. Its arbitrary byte32 and
byte6 markers are explicitly not the Browse mapping or native handler evidence.
20 assertions pass per plain and ASan/UBSan mode, with three exact controls per
mode, independently byte-identical. Kernel ACK is not consumption, and UI-time
MIN_HOLD does not establish native dwell after queue backlog.

Next compose an authenticated backend/receipt adapter, ordered host persistence
and owned stopped recovery, with explicit callback/deadline and native input
consumption policies. App on_exit requests stop but still continues legacy
Quit/boot::stop before any owned worker join; a future live lifecycle must fix
that ordering and preserve unresolved work before shutdown. Main remains None
and constructs no owned worker; there is no enable flag, live backend adapter
or new installation.
Original neutral overlay, injected mode and host bytes/modes still require
verified restoration before guarded restart. No emulator/device contact,
service action, physical control, core/package/catalog change or old cleanup
occurred. MODS/native menu, stems activation/staging, USB activation and physical
validation remain open. Preserve unrelated untracked work.


## October 27 follow-up: retain the joined session and exact ACK evidence

The separate [joined-session successor](../experiments/emulator-joined-session/)
changes only producer, route and transport after the frozen bounded-mailbox
candidate. All 17 prepared files are pinned; the other 14, including App, main,
config, controls and lockfile, remain byte-identical. Patch SHA-256:
`4267249c94882120ad9b8e2b37e3894704db95876d51b5c42a10a5f244596cad`.

A completed real join now returns a non-Clone, privately constructed session
retaining the same route/backend and copied accounting. The producer retains
its latest exact full ACK before invoking settings, distinguishing no callback,
closed-gate skip, armed/no recorded return, returned success and returned error.
Close and later noncommits do not erase that ACK. A separate exact baseline
checkpoint distinguishes stage, acknowledgment and local release/adoption.
Both checkpoints carry their own keys and can refer to different generations;
neither is a complete history or original recovery baseline.

Read-only poison salvage never clears poison or reopens operations. A poisoned
raw admission flag is not usable admission; an unresolved malformed-positive
request remains unresolved even with uncertain=false. A normal worker exit,
empty mailbox or copied diagnostic is not recovery permission. Dropping the
final owner can destroy backend resources without restoring the overlay.

Production locked/offline host compilation succeeds with 36 visible unused-code
warnings. The actual crate lists 126 tests and passes the 11 new cases; 115
inherited cases are compiled/listed but filtered. All seven strict source
controls compile and fail their exact intended assertion, including filename,
line/column, test name/count and exit 101. The ownership control uses Weak while
preserving a real join and safe fixture cleanup. Independent host results are
retained separately with comparison/review in the experiment. No Rust sanitizer,
Linux ARM64 runtime, audio, native-consumption or physical-timing claim follows.

Next implement a separately reviewed lifecycle owner holding both worker and
supervisor resources through join, with idempotent App-exit and startup/return
paths. Unix supervisor EOF and successful Windows job binding can stop QEMU
independently on process death; main-only cleanup also misses macOS Cmd-Q paths.
This patch does not change those policies or fix App's existing stop-without-join
ordering. Main remains None, with no backend factory or enable flag.

The [lifecycle/host-file audit](../experiments/emulator-joined-session/LIFECYCLE-LIMITS.md)
records independent preview/driver writes and boot deletion of mixer.state and
knob.ctr. The earlier recovery contract retains only the original counter file;
a real settings sink must additionally retain mixer.state originals/absence/mode/
identity, exclude writers and account for partial two-file progress. A callback
outcome cannot prove file contents, durability or restoration. No such sink or
stopped-recovery adapter exists here.

Native ownership, MODS publication, stems staging/activation, USB activation and
physical validation remain unfinished. Runtime dispatch and the stems staging
gate remain closed. No emulator/device endpoint, service action, physical control,
core/package/catalog change or old cleanup occurred. Preserve the original emulator checkout, frozen parents and
unrelated untracked work, including deck-lights/.


## October 27: inactive owner through exit and returning startup failures

Continue from [emulator-owned-exit](../experiments/emulator-owned-exit/), after
the frozen joined-session candidate. Patch
`b278c09b4db47e341ebc5542386dfa79d185586c905b8c4904c430f2aef51a3b`
changes App/main/transport and adds the owner; 14 other candidate files remain
identical. All 18 final files and the nine frozen inputs are pinned. Main still
selects no owned port/worker pair; the original emulator checkout is unchanged.

The coordinator rejects a mismatched pair while returning every input, claims
the worker once, and performs stop/real join outside its owner lock. A stop
error cannot skip the join. It retains the exact joined session and available
supervisor value, and neither owned App exit nor the returning fallback sends
Quit or calls boot::stop. A direct worker-thread finish is refused before taking
the handle. Poison during join retains completed accounting and its poison flag;
poison before taking the worker remains Running/unjoined and does not establish
terminal admission. Copied diagnostics grant no teardown/recovery authority.

Production host compilation, 12 new selected tests and seven strict compiling
controls pass; see the experiment for independent results, exact assertions and
source review. The other 126 listed tests were not executed again. Headless App
fixtures have no launched child. Synthetic generic guest counters prove only
local retention, while a pinned cached eframe/winit source audit explains the
unexecuted platform boundaries. No window, real backend, settings-file write,
guest process, emulator endpoint, physical device or native input ran.

The owned candidate remains unsuitable for live enablement: main's eventual
Drop can release the Unix supervisor pipe; Cmd-Q/process death can bypass a
returning wrapper; Windows Launched is a PID, not a job handle. Startup pairing
rejection also ends in ordinary Drop. Callback dependency cycles/deadlines,
authenticated backend/receipt authority, native consumption/dwell, unknown
reconciliation, two-file settings restoration/writer exclusion and owned stopped
recovery are still required. Do not install this candidate or infer a runtime
permit. Preserve the prior source/evidence and all staging/publication gates.


## October 29: tagged selected-mode restoration, offline only

The separate [RESTORE successor](../experiments/emulator-overlay-restore/)
applies after the frozen ownership and instance/generation patches. It adds a
208-byte input-only V2 request (`0x40d05a55`) with the existing bound header,
explicit injected value (0 or 1), zero reserved field and 152 raw frame bytes.
Mode 0 must match every pinned idle-template byte. The final mutation lock checks
instance/file/run/epoch/exact-next sequence and counter capacity, then commits
mode/frame, epoch increment and sequence zero together. Existing FRAME/ROTATE
encodings and semantics, owner/run/instance, permanent legacy fence and mixer
CRC policy remain intact. No new STATE flag or legacy fallback is introduced.

Patch SHA-256:
`fdf40df07b76e22f1684186e4d725ccc5d1cc56e24d5cb9a7f7813534a045352`.
Only the C module and ownership header change; frame header and Makefile remain
identical. Preparation exports pinned Git blobs and checks all three captured
patches and each intermediate inventory. The original emulator and frozen
parents are unchanged.

The focused host fixture passes 148 assertions per plain, ASan/UBSan and TSan
mode, 11 strict compiling controls per mode and 26 static source/ABI checks.
It executes exact extracted methods with owned-memory usercopy and pthread
shims, testing both modes, all-byte idle mismatch rejection, partial copies,
all ownership/generation gates, old copied mutators, concurrent winners,
legacy/CRC fencing, coherent historical STATE and delayed SPI snapshots.
The frozen parents' complete passing suites were not repeated. A fresh independent
run matches the entire result byte for byte, with no excluded fields. Strict
ARM64 compilation/modpost passes; all 36 actual undefined symbols match the
pinned export table, and the original CRC parameter metadata is retained.
Source review and separate build evidence are retained in the experiment.

Two draft-fixture failures remain recorded: a first compile rejected an unused
fixture variable, then a plain run failed both delayed-SPI-history assertions
because a reused fixture barrier was still signaled from the earlier STATE
read. Per-launch barrier reset under its mutex fixes that schedule. The
assertions and kernel candidate were unchanged; this is unrelated to the
unexplained historical stems ASan failure, whose gate stays closed.

The target is selected raw bytes and mode, not hidden dormant-buffer history.
A previously sampled SPI frame can still complete after RESTORE, and a producer
can still reauthor old bytes under a fresh tag. No native event cancellation,
neutrality, application-stop proof or recovery completion follows. An uncertain
RESTORE needs exact retained request/result reconciliation, not blind retries or
inferred sequence advancement. The old broker does not yet support this command.

Next compose a separately reviewed broker/recovery adapter with retained original
baseline authority, closed producer admission, verified native process/start
stop, exact two-file host restoration/writer exclusion and guarded restart.
Platform final-Drop/process-death custody, callback deadlines and native dwell
remain open. Main supplies no owned backend and all runtime dispatch stays CLOSED.
No installation, emulator/device endpoint, service action, physical access,
core/package/catalog change, old cleanup or snapshot restoration occurred.
Preserve unrelated untracked work including deck-lights/ and all prior gates.

## October 29: retained broker RESTORE accounting (offline)

A separate [broker successor](../experiments/emulator-broker-restore/) now
accounts for the kernel RESTORE primitive using a copied value request: an
independent monotonic restore ID, complete expected source state, explicit
injected/idle mode and all 152 target bytes. Closed admission, no unresolved
command and a fresh exact source snapshot precede the single backend attempt.
The exact request, kernel command and expected result are retained before
submission. Local refusals consume no ID; attempted REJECTED and UNKNOWN
operations do. FRAME/ROTATE never reset the restore-ID high-watermark.

Exact last duplicates return historical receipts without queries or resends.
Changed requests with that same ID close admission and preserve the valid
receipt, including malformed source/mode conflicts. A source review caught
the original malformed-input ordering before testing; dedicated strict
controls now reproduce it. No assertions were weakened. After another
operation replaces the last receipt, the older restore ID cannot be replayed.
Successful or positively reconciled RESTORE clears the old grant, advances
epoch/sequence state and stays closed. Idle flags23 are valid bookkeeping
state but never producer-adoptable; FRAME/ROTATE explicitly predict flags31.
A cached receipt after a separate fresh adoption changes no admission state
and is not current recovery evidence.

The focused fixture passes 207 assertions per plain, ASan/UBSan and TSan mode,
with 16 strict controls per mode and clean standalone C compilation. It
executes seven exact methods from the frozen kernel RESTORE candidate, with
explicit test-only semantic/wire translation. Tests cover both modes, lost
responses versus retained not-yet-delivered commands, full exact positive
reconciliation, stale/malformed identity, raw CRC/tail bytes, capacity limits,
input/output aliasing and selected synchronized concurrency schedules. The
old passing broker/kernel suites were not repeated. The first full focused
run passes; a fresh independent run matches the complete result byte for byte,
with no exclusions. Strict
ARM64 object compilation also passes: no link or execution, with the same
14 unresolved dependencies and only mb_restore added to the global functions.

This library is not wired to the Rust preview, native fd, runtime runner or
a stopped-recovery coordinator. Semantic structs changed and need separately
compiled consumers; they are not an ioctl ABI. There is no durable journal,
authoritative original baseline or native-stop proof. An old/lower STATE is
not cancellation; another same-capability mutator could invalidate causal
attribution, and a caller can still reauthor old bytes under a fresh identity.
Host-file restoration/writer exclusion, guarded restart, process-death/final
Drop custody, callback deadlines, native event ownership and timing remain
open. Main still supplies no owned backend. All runtime/staging/product gates
remain CLOSED. No emulator/device endpoint contact, install, service action,
physical access, package/core/catalog change, old cleanup or snapshot restore
occurred. Frozen parents and unrelated deck-lights/ work are preserved.


## October 29: native64 controller adapter (offline)

The separate [native64 adapter](../experiments/emulator-native64-adapter/)
now supplies explicit Linux wire translation for the frozen RESTORE broker.
It duplicates an already-claimed controller fd with CLOEXEC, validates one
complete owned STATE against every expected field and all 152 bytes, and binds
callbacks to that retained reference, instance and run. Reusing the caller's
original fd number cannot redirect those callbacks. Other duplicated or
inherited references can still mutate the session; live module provenance,
sole-mutator custody and original-fd stability during attach remain caller
requirements. This library does not acquire or CLAIM a controller.

FRAME, ROTATE and RESTORE each issue one typed ioctl. A zero return commits;
only the pinned input-only handler's exact precommit errno set is REJECTED.
Other errors and malformed result pairs are UNKNOWN, with no automatic retry,
query, close or reopen. STATE publishes all fields only after success, strict
shape/flags and identity validation. Failure cleanup preserves its primary
error separately from one close result. Explicit disposal consumes the handle
before one close attempt and requires external lifecycle/uncertainty resolution;
it is not restoration, cancellation or a recovery permission.

The first focused host matrix passes 137 checks per plain, ASan/UBSan and TSan
mode, plus 13 strict single-oracle controls and clean standalone compilation.
One fresh independent run matches the entire result byte for byte. The actual
adapter and frozen broker execute seven exact extracted kernel methods with
serial owned-memory fd/usercopy/syscall shims. Tests cover raw152/wire equality,
partial STATE failures, original-number reuse, unknown replies before/after
actual extracted commits, and exact positive broker reconciliation. The native
libc wrappers are source-reviewed and separately compiled, not host-executed.
Strict ARM64 Linux compilation and shared linking pass with real Linux headers;
all 18 strong imports match the copied musl provider, while five weak CRT hooks
retain their documented optional status. No artifact was loaded or executed.
The original broker/kernel suites were not repeated.

The adapter is not connected to the actual preview, FFI, runtime runner or
stopped-recovery coordinator. It provides no authoritative original baseline,
native-stop proof, durable journal, bounded callback, two-file host restoration,
competing-writer exclusion or guarded restart. Review of the actual settings
path confirms that legacy knob.ctr truncation and mixer.state temp/rename
writes are separate operations; the owned SettingsSink is still injected-only.
A later host-file transaction must retain both original identities/bytes and
per-file progress, including uncertain rename outcomes, without claiming pair
atomicity. Final Drop/process death and native event ownership/dwell remain
open. All runtime, staging and product gates stay CLOSED. No emulator/device
endpoint contact, install, service action, physical access, core/package/catalog
change, old cleanup or snapshot restoration occurred. Unrelated deck-lights/
work and all frozen parents are preserved.


## October 29: ACK-bound host file-pair component (offline)

The separate [settings-pair experiment](../experiments/emulator-settings-pair/)
now retains both original host file images and per-file progress for explicit
FrameAck and target-byte inputs. It accepts an explicit directory descriptor,
uses only knob.ctr and mixer.state, and requires one exact baseline generation,
next request ID and kernel sequence. It captures exact originals including
absence/mode/identity; exclusive temporary creation, content/mode, file sync,
armed rename, directory sync and readback each have distinct retained stages.
A partial second-file update preserves the first result. Error, panic, drift or
conflict blocks later writes without rollback, transaction retry or cleanup.
An independent evidence handle keeps the same directory and journal after
writer Drop; the last handle or process disappearing can still lose custody.

Final production host compilation and 24 focused tests/eight exact-assertion
controls pass, with independently matching functional results; all 138 inherited
tests were compiled/listed and filtered.
The actual Rust crate uses real filesystem operations in fresh private synthetic
directories, with no application main, backend or emulator/device access.
Two earlier full runs passed 22/23 but failed an invalid special-mode fixture;
the strengthened failure occurred before attach. The same Rust permission probe
preserves privilege bits under one launch context but masks `06000` under the root
launcher, even with its inherited environment. The mechanism remains unproven.
Final tests distinguish real sticky-mode attach rejection from copied-metadata
setuid/setgid rejection. No privileged-file workaround, production-policy change,
skipped assertion or runtime gate opening follows from the passing matrix.
The failures, bounded probes, exact pins and independent review are retained.

A source audit maps preview, az-drive and boot/test writer/deleter paths, their
different formatting/order/error handling, and the preview's 51 analog offsets
versus the historical 54 saved offsets. A future mapper must derive the intended
persisted fields from the same ACK and preserve supported unrelated state;
permissive legacy parsing is not adopted as proof. The new primitive accepts
opaque bytes and does not implement SettingsSink or restoration. External writers
and readers needing a consistent pair must be excluded; nofollow/identity checks
are not conditional inode replacement, and two renames are not pair-atomic.
Native adapter/FFI composition, authoritative original baseline and stopped
process identity, durable custody, actual both-file restoration and guarded
restart remain open. Boundary fault hooks do not simulate kernel partial-write
errors or power loss. All runtime, stems staging and release gates stay CLOSED.
No services, endpoint contact, installation, physical action, core/package/catalog
change, old cleanup or snapshot restoration occurred. Frozen parents, original
emulator checkout and unrelated deck-lights/ work remain preserved.


## October 29: strict ACK-to-settings mapper (offline)

The separate [settings-mapper experiment](../experiments/emulator-settings-mapper/)
now projects one complete FrameAck into the browse counter and all 51 complete
analog bytes. Explicit selector/TIME choices either refresh those fields from
the same frame or preserve valid existing values/absence. Selector low-nibble
momentary bits are never copied. TIME bytes remain an exact paired bit pattern.
A bounded strict JSON subset refuses duplicates, aliases, unsupported offsets,
malformed/ranged values, unsafe prior selector bits and incomplete TIME pairs,
even when a selected update would overwrite them. Exact original file custody
remains with the unchanged pair engine; mapped target formatting is canonical.

Production host compilation, 27 focused synthetic tests and 13 strict compiling
controls pass in both final runs. All 162 inherited tests were compiled/listed
and filtered. The independent audit reconstructs both runs' 28 variants and
compares every result field after only 29 declared raw-log-hash exclusions;
28 hashes differ, one agrees, and whole result JSON is not identical. The build
has 81 inactive-code warnings. An initial run stopped because the trailing-data
control's custom assert message lacked the runner's required assertion wording.
Its exact assertion had failed; only the fixture macro changed to the equivalent
assert_eq!(predicate, true). Production and runner bytes remained unchanged,
and the interrupted run is preserved. Explicit optimized-Python refusal now
protects frozen preparer assertions without changing parent sources.

This mapper uses real preview types and parser/control helpers in value fixtures,
but performs no host settings I/O and implements no SettingsSink. It does not
authenticate ACKs or prove callbacks ran in order, file persistence, a native
poll or physical timing. Main remains a literal absent owned pair. Next connect
the validated ACK callback to the same mapper/pair custody, retaining mapping
refusals and partial file outcomes without replaying a committed native FRAME.
External writer/reader exclusion, native adapter/FFI and receipt authority,
authoritative originals/stopped-process identity, both-file/overlay restoration,
durable custody, final Drop/death and guarded restart remain open. All existing
runtime, stems staging, native ownership, USB and release gates remain CLOSED.
No endpoint, service, installation, physical access, core/package/catalog change,
old cleanup or snapshot restoration occurred. Frozen parents and unrelated
deck-lights/ work are preserved.


## October 29: retained ACK settings callback (offline)

The separate [settings-sink experiment](../experiments/emulator-settings-sink/)
now implements the actual preview SettingsSink using the frozen strict mapper
and retained file-pair engine. Construction binds one explicit baseline, fixed
selector/TIME policy and the writer's own evidence handle. Each accepted callback
retains the full ACK and last verified source image before mapping, then the full
target plan before file operations. Errors, duplicates and interrupted phases
close later writes without replay, repair, rollback or implicit cleanup.

Production host compilation, 22 focused tests and 14 strict compiling controls
pass with an independent fresh reproduction. All 189 inherited tests remain
compiled/listed and filtered; 211 total names are pinned. The 24-file candidate
changes only an inactive Unix module declaration among parent files. The build
has 91 inactive-code warnings. The first preparation invocation failed before
candidate creation because a redirect substring matched both a quoted anchor
and its assignment; the exact-line repair and original inputs are preserved.
The runner verifies captured compilation bytes and requires the final panic
to identify the designated assertion, excluding earlier caught-panic text.

Actual route/producer/worker fixtures use synthetic backend receipts and real
isolated host files. They preserve mapping refusal after a known native commit,
partial replacements, independent evidence during blocked callbacks, panic gaps,
poison and joined worker custody. Bridge completion is distinct from producer
callback completion; snapshots copy separate journals and are not coherent or
fresh filesystem observations. A pair may be committed while bridge/producer
completion remains armed. No replay or recovery permission follows.

Two counterexamples remain explicit: a constructor/backend baseline mismatch
and rearming a newer route epoch after callback error can commit a native FRAME
before this bridge refuses its callback. Its permanent closure therefore does
not enforce permanent native admission closure. The next owner composition must
establish baseline correspondence and prevent failed-route rearming before
submission. This checkpoint creates no live backend or factory.

Enforceable external writer/reader exclusion, native acquisition/FFI and receipt
authority, authoritative originals/stopped-process identity, callback deadlines,
native dwell, both-file/overlay restoration, durable custody, final Drop/death
and guarded restart remain unresolved. Main and runtime dispatch stay inactive;
all stems staging, native ownership, USB and release gates remain CLOSED.
No endpoint, service, installation, physical access, core/package/catalog change,
old cleanup or snapshot restoration occurred. Frozen parents and unrelated
deck-lights/ work remain preserved.


## October 29: retained single-generation route construction (offline)

The separate [fixed-session experiment](../experiments/emulator-fixed-session/)
binds the actual settings bridge and immutable preview route to the same full
backend baseline. It retains the actual backend and original directory before
initialization, permits one construction attempt, publishes settings/route
custody before later boundaries, and returns an unarmed route. The cached receipt
can be issued once. A closed route cannot query a newer epoch through this
wrapper after a settings failure, known rejection or abandoned adoption.

Production host compilation, 25 focused actual-crate tests and 16 strict
compiling controls pass with an independent fresh reproduction. The 211 inherited
tests are compiled/listed and filtered; 236 total names and 26 candidate files
are pinned. Only an inactive Unix module declaration changes among parent files.
The build has 113 inactive-code warnings. The parent settings sink and its
counterexamples remain frozen; this composition closes their local baseline
correspondence and rearming gaps only for the constructed route.

The backend journal records full requests before forwarding and exact returned
outcomes before producer processing. Poison after a returned result retains it
for diagnostics but yields Unknown to the route. Constructor errors, unwind
boundaries, callback panic, late ACKs and actual worker/lifecycle joins retain
their separate evidence. Snapshot tests complete while backend or settings
calls are blocked without acquiring those operation locks. Evidence-only and
route-only ownership retain actual resources, while final release tests detect
built-in cycles. Final Drop/death can still lose custody and never permits
restore or safe teardown.

Captured receipts are historical correspondence, not freshness or authority.
This component creates no native backend, worker, supervisor, endpoint or main
wiring. A supplied backend can share outside resources, and separately
constructed sessions do not inherit this route's one-generation restriction.
External native/file writer and reader exclusion, acquisition/FFI, authoritative
originals and stopped identity, callback deadlines, native dwell, both-file and
overlay restoration, durable custody and guarded restart remain unresolved.

All runtime, stems staging, native ownership, USB and release gates remain
CLOSED. No endpoint, service, installation, physical access, core/package/catalog
change, old cleanup or snapshot restoration occurred. Frozen parents and
unrelated deck-lights/ work remain preserved.


## October 29: retained worker and supervisor startup composition

The separate [owned-startup successor](../experiments/emulator-owned-startup/)
retains injected backend, original directory and supplied supervisor before
constructing the fixed session, actual worker channel and frozen lifecycle.
It stores the exact returned port/worker pair before later hooks. A tested
publication unwind with usable startup journal/products leaves the external
handle able to perform an explicit actual join. Poison can refuse that access;
no implicit cleanup or restoration occurs.

Production host compilation, 16 new focused tests and 12 exact-assertion source
controls pass with one independent fresh reproduction. The 236 inherited tests
remain compiled/listed and filtered; 252 names and 28 candidate files are pinned.
The retained-file audit verifies 33 frozen inputs and eight runner inputs per
run, reconstructs all 26 variants, and checks 72 regular fixture files in 36
directories per run. Exactly 27 enumerated log-hash fields are excluded from
functional comparison; 26 differ. Every other field agrees; whole JSON differs.
The build has 132 inactive-code warnings and uses no sanitizers.

Busy finish is not cancellation: it returns InProgress, and startup may still
reach Ready before another explicit finish. ChannelArmed without a returned
pair remains StartupUnproven. The spawn-error helper invokes the frozen actual
error arm but does not induce OS spawn failure. Poison refuses operations while
retaining resources; an independently returned lifecycle can still perform its
own join. A real join completed during journal poison is retained diagnostically,
while the startup return remains Poisoned.

Handle-only custody and control-only custody are distinct. The control retains
the sink's duplicated directory capability after the original File Arc expires;
a private rename and subsequent ACK-bound write exercise that capability.
Final Drop, process death and unreturned channel interior custody remain open.

The [native-boundary review](../experiments/emulator-owned-startup/REVIEW.md)
also records that the native64 adapter requires an already-claimed Linux
descriptor and retains a private duplicate only after successful attachment.
Preview runs on the host; a direct host C/Rust cast cannot reach the guest
controller. Next review a guest-resident broker transport with complete
request/result identity, conservative unknown outcomes and explicit ownership;
ABI translation alone cannot establish acquisition or receipt authority.
Authoritative originals/stopped identity, external writer/reader exclusion,
callback deadlines, native dwell, full overlay/both-file restoration, durable
custody and guarded restart remain unresolved.

Main and all existing runtime/stems-staging/native-ownership/USB/release gates
remain CLOSED. No endpoint, service, guest start, installation, physical access,
core/package/catalog change, old cleanup or snapshot restoration occurred.
Frozen parents and unrelated deck-lights/ work remain preserved. Do not repeat
this passing matrix unchanged or treat it as native observation evidence.


## October 29: bounded broker wire seam (offline)

The separate [broker-wire experiment](../experiments/emulator-broker-wire/)
adds canonical C/Rust records between a borrowed already-created broker and a
host MixerBackend owning one caller-supplied stream. It invokes actual frozen
mb_adopt/mb_submit in its synthetic C fixtures. Exact last-exchange retrieval
has cached provenance and cannot become a fresh host grant or ACK. Full identity,
sequence and all 152 raw frame bytes are checked before acceptance.

Production host compilation and 20 focused Rust tests pass with seven strict
compiling controls. The 252 inherited tests remain filtered (272 total names).
The new C suite passes 5,248 checks per plain, ASan/UBSan and TSan mode, with five
strict controls per mode and separate clean production translation-unit builds.
One independent fresh matrix also passes. Retained-file audit reconstructs all
variants and verifies 30 candidate files, 40 frozen parent inputs and 15 runner
inputs per run. Exactly 63 enumerated log-hash fields are excluded from comparison;
17 differ, every other JSON field agrees, and whole JSON differs. The production
host build has 162 inactive-code warnings; Rust tests are nonsanitized.

The pipe integration exercises commit, definite rejection, unknown, lost reply
after synthetic commit, cached receipt and synthetic API error with positive
receipt. The error injection follows actual broker return; it does not induce
an OS mutex failure. Any nonzero API result stays uncertain. A repeated earlier
committed Envelope is Unknown without another send, never a fabricated rejection.
Pre-I/O intent and returned progress/errors survive tested panic/poison boundaries;
independent diagnostic snapshots remain available while I/O blocks.

This is an inactive value/transport seam, not a live guest adapter. The host
stream can block indefinitely, buffer data or refer to an unauthenticated peer.
Its label is correlation only. Evidence handles retain diagnostics, not stream
custody after backend Drop. The C wrapper borrows a broker and cannot exclude
aliases. Native acquisition, authoritative originals/stopped identity, owned
endpoint/supervisor binding, external exclusion, native consumption/dwell,
full overlay/both-file recovery and durable custody remain open. The static
[legacy transport review](../experiments/emulator-broker-wire/REVIEW.md) explains
why cache/reconnect/SSH fallback cannot supply matching native receipts.

Main and all runtime/stems-staging/native-ownership/USB/release gates stay CLOSED.
No endpoint, service, guest start, installation, physical access, core/package/
catalog change, old cleanup or snapshot restoration occurred. Frozen parents,
previous releases and unrelated deck-lights/ work remain preserved. Do not repeat
this passing matrix unchanged or treat it as native observation evidence.


## October 29: wire/native64 actual-source composition

The separate [wire/native64 integration](../experiments/emulator-wire-native64/)
now links unchanged C wire/dispatcher, broker and adapter translation units to
seven exact extracted driver methods. Fourteen new scenarios and six strict
controls per plain, ASan/UBSan and TSan mode pass in two fresh matrices. Complete
result JSON is byte-identical. The retained-file audit checks all source and log
inventories; its first declaration reconstruction retained a trailing comment,
was corrected without weakening equality, and then passed. That audit failure
is recorded separately; no tests or captured inputs changed.

The chain verifies full raw and canonical bytes, duplicate-fd identity through
original-number reuse, fresh adoption state, exact modeled EPERM/EFAULT,
UNKNOWN after actual modeled commit plus synthetic EIO, historical cache
retrieval and same-capability alias divergence. A cached response remains
historical and cannot exclude another mutator. These are serial host checks;
native libc/VFS, Rust transport, external ownership and real input do not run.

The [acquisition review](../experiments/emulator-wire-native64/ACQUISITION-REVIEW.md)
narrows the next implementation gate: retain the original before one-shot CLAIM,
retain exact pre/post state and returns, and separately revise failed attachment
to preserve usable duplicate custody. Current mb_destroy has no observable
success result, so checked teardown remains separate. CLAIM preserves content
and injected mode; matching STATE is neither baseline nor stop authority.
No automatic FRAME may turn idle acquisition into ready input.

No emulator/device endpoint, service, core, package, catalog, settings or prior
cleanup changed. Main and runtime wiring remain inactive. Stems staging,
native ownership, USB completion/cancellation, recovery, audio quality and
physical timing remain unresolved. Preserve all prior source and evidence.


## October 29: retained-before-validation adapter

The separate [retained adapter](../experiments/emulator-retained-adapter/) allocates/copies an owner before separate
one-shot duplication and STATE validation. Failed validation preserves the held
duplicate, raw syscall return and all 208 initialized/returned bytes. Diagnostic
bytes are not a usable semantic state; full version/shape/identity/counter/mode/
CRC/all 152-byte equality is required before backend admission. Stage repeats
make no I/O and preserve first evidence. The frozen snapshot/submit prefix is
mechanically included byte for byte; legacy public lifecycle is not linked.

Fourteen new focused selectors and nine strict compiling controls per plain,
ASan/UBSan and TSan mode pass in two fresh matrices. Whole result JSON is
byte-identical; the retained-file audit independently verifies source generation,
variants and every recorded log. Both matrices and the audit passed on their
first invocations. No unchanged parent suite ran. Review corrected descriptor
wording, a harness field typo, verifier optimization guards and disposal coverage
before either matrix.

The new disposal case checks explicit owner consumption, held/unheld close
branches, exact raw uncertain close reports and no retry under synthetic
quiescence. Its fallback fixture cleanup is not runtime teardown permission.
The original fd remains borrowed; inspection exposes actual descriptor numbers,
so aliases and external writer exclusion remain unresolved. Same-thread syscall
inspection does not prove blocked/concurrent inspection or durable custody.

Next retain an original fd before one-shot CLAIM, exact pre/poststate and raw
returns, then compose retained adapter/broker/dispatcher ownership through failed
construction. The frozen broker's void destroy still cannot certify graph
teardown. Matching state supplies neither authoritative originals nor native
stop; idle acquisition must never be converted automatically to injected input.

No endpoint, emulator start/restart, install, physical control, settings, core,
package, catalog or prior cleanup changed. Main and observer runtime dispatch
remain inactive; stems staging, USB completion/cancellation, lifetime/race,
recovery, audio quality, native timing and community-release gates remain open
work. Frozen parents, previous evidence and unrelated deck-lights/ are preserved.


## October 29: retained original descriptor and one-shot CLAIM

The separate [CLAIM owner](../experiments/emulator-claim-owner/) retains a caller-transferred original fd,
including fd0, before preflight STATE or CLAIM. It copies the plan and authors
the exact command before transfer; each syscall is armed once, with raw returns
and all initialized/returned STATE bytes kept before interpretation. Failure
does not implicitly close, release, retry or discard the owner.

The actual extracted handler demonstrates two distinct races. A legacy write
between preflight and CLAIM can change content: CLAIM commits, then exact
postflight refuses readiness while retaining Committed history. A same-file
alias can claim first: this attempt is EBUSY/Rejected even though that file
description owns the overlay. Thus Rejected never proves the original unowned;
Committed never proves current exclusive ownership. Scripted postcommit EIO
stays Unknown even when fixture STATE matches. Idle acquisition stays idle,
and prior fenced acquisition resets sequence without clearing the fence.

Fourteen new compound selectors and 12 strict compiling controls pass per
plain, ASan/UBSan and TSan mode in two fresh matrices. Entire result files match
byte for byte; the retained-file audit independently checks generated source,
variants, inventories and logs. Both matrices and audit passed first invocation.
No unchanged parent suite ran, and no Linux ARM64 build is claimed here.
The invalid-plan oracle has no allocator instrumentation; command overwrites,
abnormal returns and STATE corruptions are explicitly synthetic. Disposal tests
use only quiescent modeled fd cleanup, not runtime teardown authority.

Next compose original/CLAIM custody with the retained adapter, broker and
dispatcher, preserving every failed construction graph. No such composition
or live endpoint exists yet. Supplied expected bytes and matching snapshots
cannot establish authoritative originals or native stop. Alias exclusion,
deadlines, checked graph destruction, complete overlay/both-file recovery and
durable/process-death custody remain open.

No endpoint, emulator start/restart, physical control, installation, settings,
official core, package, catalog or prior cleanup changed. Frozen parents,
previous failures and unrelated deck-lights/ remain preserved. Stems staging,
USB completion/cancellation, observer runtime, lifetime/race, recovery,
audio-quality, physical-timing and community-release gates stay CLOSED.


## October 29: retained broker initialization and observable disposal

The separate [retained broker](../experiments/emulator-retained-broker/) publishes an owner before synchronization
setup or backend validation. One-shot initialization records each armed call
and raw result; failure keeps all previously initialized objects. One-shot
validation keeps returned semantic state and raw callback result before full
equality checks. Only Ready exports the embedded core. The exact frozen broker
helper/operation bodies and C dispatcher remain unchanged; old create/destroy
bodies are excluded from this successor.

The first explicit disposal permanently closes setup/export and invalidates all
earlier borrows. Failed mutex destruction retains all remaining resources;
failed attr destruction after mutex success keeps owner/attr and permits only
inspection or independently justified later disposal. First/latest reports and
raw results survive. Later explicit attempts skip released resources. This
observable result depends on external serialization, quiescence and uncertainty
accounting; it cannot establish them or authorize graph teardown/recovery.

Fourteen new fixture selectors plus a separate production pthread-wrapper smoke,
and 12 strict compiling controls, pass per plain, ASan/UBSan and TSan mode in
two fresh matrices. Whole results are byte-identical; retained-file audit checks
source generation, variants, inventories and all logs without rerunning tests.
Both matrices and audit passed first invocation. No parent suite was repeated,
and no Linux ARM64 build is claimed.

Successful synchronization paths execute pthread. Refusal scripts occur before
pthread calls and do not demonstrate real busy-thread destruction or contention.
Backend values remain synthetic. Same-thread inspection proves no concurrent
custody; disposal overflow is source-reviewed only. Fixture cleanup uses known
quiescence and gives no runtime retry permission.

Next compose original/CLAIM, retained adapter, broker and dispatcher while keeping
every acquired child on failed construction. That full graph and owned transport/
supervisor binding remain unimplemented. Backend context is still borrowed;
raw aliases, native originals/stop authority, deadlines, complete overlay/both-file
recovery and durable/process-death custody remain open.

No endpoint, emulator start/restart, physical control, install, core, settings,
package, catalog or prior cleanup changed. Frozen source, historical failures
and unrelated deck-lights/ remain preserved. Main/runtime wiring stays inactive;
stems staging, USB completion/cancellation, observer, lifetime/race, recovery,
audio-quality, physical-timing and community-release gates remain CLOSED.

## October 29: retained native construction graph

The separate [owned graph](../experiments/emulator-owned-graph/) now composes original-descriptor/CLAIM custody,
retained duplicate attachment, retained broker and the actual C dispatcher.
The caller retains graph storage before transfer. Each child is stored before
its effectful stage; construction failure leaves acquired dependencies and
armed/returned child results inspectable. Constructor Ready means the dispatcher
exists, not that ADOPT or a native input succeeded.

The first graph accepts injected prestates only and rejects idle plans before
taking a descriptor. No automatic FRAME changes idle mode. It exports no raw
child, broker, dispatcher or callable backend; diagnostic fd numbers remain
usable in-process and do not establish alias exclusion.

Explicit disposal requires external quiescence and uncertainty/ownership
accounting. It retires the dispatcher first, then the broker. A broker failure
keeps both descriptor dependencies. An uncertain duplicate close stops before
original close; later explicit disposal skips consumed children, never retries
old fd numbers and keeps the uncertainty-bearing graph shell. Only separate
finalization can discard that empty shell after independent accounting. Fully
known-success disposal returns its final report and consumes the graph.

Thirteen new graph selectors and 11 strict compiling controls pass per plain,
ASan/UBSan and TSan mode in two fresh matrices. Whole results are byte-identical.
Each matrix records 54 clean compiles, 36 clean links and 72 executions; the
retained-file audit verifies source generation, variants, inventories and logs
without replaying tests. Both matrices and the audit passed on first invocation.
No unchanged parent suite was repeated and no Linux ARM64 build is claimed.

The fixture executes exact extracted driver methods through frozen child sources
and operation bodies. Descriptor/syscall/usercopy/RNG/kernel-lock behavior is
modeled, pthread success calls are real and synchronization refusals are scripted
before calls. No allocator substitution is used; allocation-error branches and
counter exhaustion are source-reviewed only. Same-thread inspection does not
prove concurrent custody or real quiescence. Synthetic disposal/close outcomes
provide no native recovery permission.

This closes a local construction/dependency-lifetime gap under stated external
preconditions. Owned transport/supervisor binding, live libc/VFS, external writer
exclusion, authoritative originals/native stop, deadlines, complete overlay/
both-file recovery and durable/process-death custody remain open.

No emulator/device endpoint, restart, physical control, installation, core,
settings, package, catalog or previous cleanup changed. Frozen parents, prior
failures and unrelated deck-lights/ are preserved. Main and all existing runtime,
stems, USB, lifetime/race, recovery, audio-quality, physical-timing and release
gates remain CLOSED.

## October 29: retained graph ARM64 compile and export policy

The [owned graph ARM64 checkpoint](../experiments/emulator-owned-graph/ARM64-BUILD.md) compiles the eight production units,
including actual Linux syscall and pthread wrappers, and links two private shared
libraries from those same objects. Twenty frozen repository inputs produce the
21-file source/include closure with exact retained core cuts and the pinned restore
UAPI. Seven units remain byte-identical; the copied wire source has only four
independent adjacent-if line splits. The frozen wire parent remains unchanged.

The first actual target build failed at wire compilation with four
-Werror=misleading-indentation diagnostics. The isolated formatting patch
preserves non-whitespace bytes and token order, and strict warning flags remain.
A separate private runner preflight then falsely omitted a nested build-exit-code
receipt from its inventory. Its narrow top-level build-UUID filter repair and
Docker-never-invoked failure are retained separately. These records account for
two selected target build attempts, one successful, plus that preflight failure;
they do not claim an exhaustive inventory of all private preparation activity.

The corrected build records eight empty compile logs and two empty link logs.
The baseline exposes 53 APIs plus toolchain _init/_fini; the reviewed anonymous
export map leaves exactly seven graph exports. All 18 strong imports exist in the
pinned musl provider; five weak imports remain. Direct ELF parsing agrees with
captured nm/readelf. The independent byte audit passed on its first invocation
and verifies retained failures, exact source transformations, dependency headers,
object symbols and before/after provenance. No passing host matrix was repeated.

No object or library was loaded or executed. This establishes compile/link closure
against retained inputs, not running guest/firmware ABI compatibility, native VFS
custody, authentication or a security boundary. Source/format patch/export map and
sanitized evidence are public; raw artifacts, toolchain, snapshots, logs and
scripts remain private. No endpoint, restart, installation, physical control,
official core, package, catalog or earlier cleanup changed.

The next concrete boundary is a guest stream owner that retains both supplied
transport and graph before I/O, records partial header/body reads and response
writes, and preserves pending response/graph evidence after EOF, write failure
or disconnect. Disconnect must not reopen, replay, dispose the graph or imply
quiescence. Binding that owner to the host transport and guest supervisor is
separate. Authoritative native originals/stop, external writer exclusion,
deadlines, both-file restoration and durable/process-death recovery remain open.
Main and all observer, stems staging, USB completion/cancellation, lifetime/race,
audio-quality, physical-timing and release gates remain CLOSED.

## October 29: retained guest byte-stream owner

The [separate stream experiment](../experiments/emulator-guest-stream/) now owns
a supplied stream fd and a fresh actual graph before transport I/O. Bounded
steps record partial header/body reads, one graph dispatch and response writes;
explicit next archives one completed record without read-ahead. EOF, errors,
impossible counts and partial writes retain both resources and exact evidence.
There is no automatic retry, replay, reopen, flush or cleanup. Disposal requires
independent quiescence and uncertainty accounting, retires the graph first, and
preserves uncertain graph/stream closes until explicit finalization.

Two fresh focused matrices passed 14 final compound checks and 12 exact negative
controls per plain, ASan/UBSan and TSan mode. Each records 61 clean compiles,
39 clean links and 78 expected executions. Their sanitized result bytes match
SHA256 3c1e91ed681a42f5a1b01053b384b9683ad08682ec7ac80f6032f6d7b88aad7f.
The first byte-only audit verified 9 captured inputs, 27 frozen inputs and
28 generated files, with two successful matrix attempts and no failed attempts.
It reconstructs exact driver/core/fixture cuts and expected control logs without
rerunning tests. Only the old graph fixture support prefix is reused; no parent
selector or passing parent suite was replayed.

This is serial host-model evidence. Transport calls and controller descriptors
are modeled; the thin POSIX wrappers were compiled separately against host
headers, never linked into the fixture or executed. No new stream ARM64 build,
native endpoint, emulator restart, installation or physical-deck control occurred.
A one-call step does not bound blocking time, prove SIGPIPE survival, authenticate
a peer or establish native ownership. Complete local response writes can still
carry UNKNOWN after a modeled native commit. External aliases, pending work,
supervisor/host-transport binding, native-stop authority and durable recovery
remain open. Raw captures, scripts, objects and logs stay private; source and
sanitized evidence are the development checkpoint. All earlier observer, staging,
USB completion/cancellation, lifetime/race, audio-quality, physical-timing and
release gates remain CLOSED. Continue at supervisor/transport ownership rather
than replaying these passing selectors unchanged.


## November 4: actual host process and retained stream composition

The [separate process fixture](../experiments/emulator-stream-process/) connects
the unchanged Rust WireBackend to the retained C stream/graph through two
unnamed socketpairs and an actual child process. The actual POSIX stream
wrappers execute; controller descriptors/ioctls and driver surroundings remain
modeled. The parent retains child/control before GO, and the child retains the
graph/stream before READY. EOF and write errors keep that owner until explicit
fixture disposal, a returned cleanup report and actual process wait.

Four new compound selectors and three strict controls pass per plain,
ASan/UBSan and TSan C mode in two fresh first-invocation matrices. Each has
37 clean compile invocations, 12 C links and 21 executions. The standalone Rust
host is unsanitized; no unchanged parent suite ran. Both sanitized results have
SHA256 204da7920697f64cd58f29c6ac81bbc2abb8935d3ec78efe550438eb23304685.
Raw reports preserve nondeterministic positive fragmentation and remain separate
from result equality. The byte-only retained-file audit passed on its first
invocation, verifying all source/log inventories, canonical bytes and actual
cleanup/wait records. Of 63 raw reports per matrix, 36 differ with fragmentation;
those values are checked independently. No failed matrix or audit attempt occurred.

Normal ADOPT plus two FRAMEs preserves all canonical bytes and advances the
host/model together. Modeled commit followed by synthetic EIO remains Unknown,
with no resend. Real response-write shutdown yields actual EPIPE under fixture
SIGPIPE ignore; the positive pending response stays retained after host EOF.
A scripted host error after 61 actual bytes produces C body EOF without
dispatch. All paths collect evidence and explicitly dispose known modeled
effects before the final oracle. That cleanup supplies no native authority.

The runner's private child-group timeout cleanup and output retention were
corrected during source review before either matrix. It is a fixture abort,
never a disposal receipt. No historical parent failure or policy was changed.
Source review and exact boundaries are in the experiment's REVIEW and HOST-TESTS.

Next bind the retained transport/graph to a separately reviewed real guest
service and production supervisor, preserving failed construction and returned
cleanup evidence; the old stdin-EOF kill policy cannot supply that contract.
This host fixture adds no production launcher, native acquisition, authentication,
alias exclusion, authoritative originals/stop, deadline or durable recovery.

No running emulator or physical deck was contacted; no settings, core, package,
catalog, installation, prior cleanup or snapshot changed. Frozen parents and
unrelated deck-lights/ remain preserved. Main and all observer, stems staging,
USB completion/cancellation, lifetime/race, recovery, audio-quality, physical-
timing and community-release gates stay CLOSED. Do not replay this passing
matrix unchanged or call it native observation evidence.


## November 4: ARM64 stream closure and actual service boundary

The [retained stream ARM64 build](../experiments/emulator-guest-stream/ARM64-BUILD.md)
compiled ten production units once and linked two shared libraries. All twelve
compile/link logs were empty; the first independent byte-only audit passed.
Twenty-five prepared inputs and 24 frozen repository inputs are bound, all eight
inherited graph objects match the prior build, and all 39 actual system-header
dependencies match the retained rootfs. The additive map exports graph7 plus
stream8; six syscall wrappers stay local. The 20 strong imports resolve against
the pinned musl provider. No ARM64 artifact was loaded/executed and no host suite
was repeated. Historical failures and frozen source remain intact.

The [source-only guest-service review](../experiments/emulator-guest-stream/GUEST-SERVICE-REVIEW.md)
shows why the existing ctrl.sock/forwarder cannot host this protocol unchanged:
legacy record sizes, detached threads, automatic reopen and Restart=always have
incompatible lifetime behavior. The supervisor also converts controller loss to
termination and treats polling errors as completed waits. Next implement a
separate inactive successor of that actual observation/event branch, retaining
the borrowed child and exact error/exit distinctions; reuse existing custody.
No stop/restart authority follows from a transport error, join or matching state.
No emulator/physical contact, installation, settings/core/package/catalog change
or runtime gate reopening occurred. All product and native-lifetime gates remain
CLOSED; the new evidence is compile/link and source review only.


## November 4: retained actual supervisor observations, inactive

The [separate supervisor observation source](../experiments/emulator-supervisor-observation/)
now borrows the caller-owned actual Child and journal for one exclusive scope.
A permanently bound journal uses PID only as a diagnostic. It retains first
controller loss, the complete first polling error and latest exact poll result
before classification. A poll that unwinds stays armed/Hold and refuses replay;
an actual exit refuses later polls. Returned running status cannot erase prior
loss/error Hold. The one-shot reader preserves actual io::copy results without
inventing a partial byte count on error.

Two fresh first-invocation matrices each passed eight selectors and four strict
controls. Actual private child liveness/exit, anonymous socket EOF and mpsc
disconnection are covered; reader/poll failures and panics are scripted.
Every semantic oracle follows explicit fixture exit and actual wait, including
the four expected failures. One locked/offline production check compiled the
inactive declaration in the pinned actual az-preview crate; the independent
repeat omitted that already-passed integration check and replayed no old suites.
The first byte-only audit verifies 13 upstream and 16 prepared files per run,
identical captured inputs, all 51 logs and exact control assertion locations.
Seven common raw logs differ only in private paths, process/thread identifiers
and durations; functional agreement is not raw hash equality.
[Detailed evidence](../experiments/emulator-supervisor-observation/HOST-TESTS.md)
and [sanitized receipt](../experiments/emulator-supervisor-observation/result.json)
preserve the limits.

This module is inactive: the legacy supervisor still collapses reader EOF/error
to stop, stops on reporter disconnect, returns 1 on direct poll error, and counts
a wait_exit poll error as completion. None of those executable branches changed.
Local Hold is not real route admission closure. Composition with existing
startup/exit custody, controller dispatch, guest framing, stop authorization,
external writer exclusion and recovery remain open. Counter exhaustion is
source-reviewed only; no whole-runtime lifetime or race claim follows.

No running emulator or physical deck was contacted, and no settings, core,
package, catalog, install, release, cleanup or old snapshot changed. Frozen
parents and unrelated deck-lights/ remain preserved. Observer, stems staging,
USB completion/cancellation, native lifetime/race, audio-quality, recovery and
physical-timing gates stay CLOSED. Do not replay these unchanged passing checks.


## November 4: retained supervisor observation now closes local admission

The [separate inactive admission composition](../experiments/emulator-supervisor-admission/)
now borrows the actual Child through the existing StartupHandle/lifecycle
owner and terminal-latches its exact port. Known controller loss latches before
polling; returned error/exit, inner poll unwind and every scope exit also latch.
Missing/busy/poisoned child access after resolving the owner records a separate
refused-scope latch while retaining the original error. The actual child,
stdin, backend and joined worker/session remain in their existing owners.

Two fresh offline matrices each passed seven new selectors and four compiled
strict controls. One locked/offline production check compiled the inactive
32-file composition; the focused repeat skipped it and neither run executed
the 260 inherited suite tests. Real sentinel Child custody, explicit fixture
wait, queued/reserved/in-flight distinctions, late ACK/settings completion,
caught unwind, pre-entry poison refusal and blocked settings are covered with
a modeled backend. Four expected failures reached their exact final assertions
after cleanup. [Evidence](../experiments/emulator-supervisor-admission/HOST-TESTS.md)
and the [sanitized receipt](../experiments/emulator-supervisor-admission/result.json)
record this bounded scope. The corrected independent byte audit verifies all
47 logs and ten retained executables; its initial metadata-inventory failure
is preserved separately, with no build or test rerun.

This closes terminal local port admission, not synchronous producer closure or
native cancellation. Work already past the terminal check may still dispatch
and commit settings. Explicit local finish/join remains separate from guest
stop authority. Main's legacy executable supervisor branches remain unchanged;
a continuous observation/event owner, production dispatch, guest framing,
external writer exclusion, teardown/restoration and recovery remain open.
No emulator/device access, install, package/catalog change, native observation,
stems activation or USB completion proof follows. All existing runtime/product
gates remain CLOSED; do not rerun the unchanged passing matrix.

## November 4: inactive retained reader and notification ownership

The [separate reader-reporter successor](../experiments/emulator-reader-reporter/)
retains the supplied reader before thread spawn, stores the actual JoinHandle
before gate release, and publishes the full frozen ReaderJournal before private
unit notification. Copy result, notification attempt/return, receiver observation
and explicit join remain independent. Exact custom errors and panic payloads are
retained; blocked Read does not block diagnostics, and uncertain attempts do not
replay. Final Drop and process death still limit custody.

Two fresh offline matrices each passed eight new selectors and four strict
compiling controls. One production check compiled the inactive actual crate;
267 inherited selectors were compiled/listed and filtered, not repeated.
The [required independent audit](../experiments/emulator-reader-reporter/HOST-TESTS.md)
verifies 49 logs, ten retained executables, 48 frozen parent inputs and exact
source/semantic agreement. It also closes a runner-only acceptance gap by
rejecting extra earlier fixture panics; four offline parser controls verify
that policy without a test/build rerun. Both matrices and the first audit passed.
This is host read/thread evidence, not sanitizer or whole-runtime proof.

Source review confirms two distinct Child owners: the application launches the
supervisor, and that separate process launches the child that execs QEMU. The
active supervisor still discards the stdin-copy result and treats EOF/channel
loss as termination. This inactive module adds no Child or port owner, main
selection, status return channel or continuous observer adapter. An owned launch
contract must resolve that actual topology before production composition.

No running emulator/physical device, settings, core, package, catalog, install,
cleanup or old snapshot was touched. Frozen sources and deck-lights/ remain
preserved. Observer, stems staging, USB final completion/cancellation, native
lifetime/race, audio-quality, recovery and physical-timing gates remain CLOSED.
Do not rerun unchanged passing checks.


## November 4: USB full-extent ABI and concrete timer teardown review

The [new static successor](../experiments/usb-browser-switch/activation-abi/)
resolves the timer entry's concrete deleting destructor. It removes an entry
from JUCE's schedule under the global mutex, then deletes it; the timer-message
path invokes the callback outside that same mutex. This supplies no callback
quiescence or cancellation of the separately queued raw StateRefresh timer
capture. Native serialization, task-box ownership and safe listener reentry
remain unresolved; no native use-after-free or activation permission is claimed.

The guarded action's complete 761 instructions and named source service's
1,033 instructions were reviewed with their known caller continuations. Both
have real cleanup/unwind paths. The final source dispatch has no local landing
pad in the action, so an exception can bypass its ordinary epilogue. Caller
non-consumption of return registers does not establish a success status or void
return type. Direct register-use findings do not prove opaque transitive ABI
contracts, wrapper/trampoline unwind correctness or live receiver provenance.

A new static verifier passed once: 14 complete FDE/code extents, 79 selected
anchors, 13 pointer relations, four RTTI strings, eight imported symbols and
three explicitly decoded cleanup LSDAs. Independent source/receipt review and
preparation-only bookkeeping failures are retained in its evidence. No native
code, wrapper, old test suite or successful runtime branch was executed.

No adapter, USB button, cancellation, post-return native read, emulator/device
action, core/package/catalog update or release was added. The previous USB
proofs and all frozen experiments remain unchanged. Full wrapper, retention,
final source readiness, missing-media/lifecycle/alternate-database/scroll-sort,
stems and all existing runtime gates remain CLOSED. Do not repeat the
unchanged static check. Next resolve actual task-box retention/cancellation and
permitted destruction/serialization, or review another owned activation route.


## November 4: native USB owner graph and shared queue lifetime

The [new ownership review](../experiments/usb-browser-switch/activation-lifetime/)
finds paired native reference ownership for SourceSelectView in ViewObjectManager
and the dispatcher's registered array. Ordinary goToView uses a separate selected
pointer; its concrete notification stops/starts timer IDs 0/3, not a demonstrated
ID-1 schedule. View destruction removes its listener before the embedded timer.

The timer and source service each own a reference-counted DefaultMemPool; those
controls do not retain their enclosing objects or own the shared execution queue.
GuiObjectManager owns BrowseSourceSelect at +0x178. The separate TimerTaskBox::Impl
registers a DeletedAtShutdown base and its own destruction reaches pending-task
cleanup. Its originating pools do not perform that queue drain.

A concrete MessageManagerLock handshake was also identified. The selected
null-thread constructor retries acquisition, so the caller's false-status branch
does not establish ordinary fail-open behavior. The dispatcher releases that
guard before registered-view cleanup. Same-message-thread reentry, queued raw
captures, exceptions and actual manager/shutdown scope remain unproved; no
native lifetime defect is asserted.

One check of the new pins with the unchanged static verifier passes 42 complete
code/FDE extents, 328 selected instructions, 71 pointer relations, 19 names and
six imported symbols. No new LSDA table is decoded. Independent source and
receipt review, exact diagnostic hashes and preparation-only failures are
retained with the evidence. The preceding verifier pins and runtime suites were
not replayed.

No post-return native read, reference-count change, adapter, USB button,
emulator/device action, installation or release was added. Full wrapper,
live owner scope, reentrant/callback quiescence, final Local Library/readiness,
missing-media/lifecycle/alternate-database/scroll-sort and all existing stems,
observer and physical gates remain CLOSED. Next connect a permitted invocation
to an owner that actually spans it, including the source manager's earlier
shutdown participants and the shared queue's real execution ordering; do not
rerun this passing static check unchanged.


## November 4: ordinary USB shutdown chain and earlier manager participants

The [new bounded shutdown review](../experiments/usb-browser-switch/activation-shutdown/)
identifies GuiObjectManager's earlier +0x1a0/+0x198 members as two SubDisplay
instances. Their actual deleting slots perform local cleanup. Mutex destruction
and paired provider calls do not establish service-task cancellation or drain;
provider/mapping/member descendants remain explicit transitive boundaries.

A concrete DeletedAtShutdown dispatcher uses a reverse pointer snapshot,
rechecks membership under its lock, then unlocks before deleting each object.
The selected application runner returns from its outer message loop before
shutdown and application deletion. EP147's actual ordinary shutdown calls the
member reset that releases UiLayerObjects and GuiObjectManager. A later registry
pass occurs when the shared GUI initialization count reaches zero. If the shared
TimerTaskBox::Impl remains registered until that pass, its cleanup follows.

This supplies positive normal return ordering: nesting a loop does not let the
outer runner advance past its still-active callback. It does not demonstrate
reentrant destruction, live operation-scoped retention, other producer shutdown,
or general queued callback quiescence. Actual replacement/destruction entry
points and live receiver/context provenance remain unresolved.

The first and only check of these new pins passes 27 complete code/FDE extents,
296 selected instructions, 30 pointer relations, seven names and 11 imports.
No new LSDA table is decoded. Independent source and retained-result review,
diagnostic hashes and preparation-only bookkeeping failures are recorded.
The preceding 42 full extents and runtime suites were not replayed.

No native adapter, USB button, cancellation, reference-count change, post-return
native read, emulator/device action, package/catalog change or release was added.
Final Local Library/readiness, complete wrapper ABI/unwind, actual live owner
scope, alternate/reentrant paths and queued execution remain open. All stems,
observer and physical gates remain CLOSED. Next connect a permitted invocation
to actual owner scope and execution context; do not repeat the passing pins.


## November 4: USB source normalization and carrier provenance

The [new source-value review](../experiments/usb-browser-switch/activation-source-value/)
resolves a structural constructor predicate, exact normalization and per-helper
identity contracts. Requested Source discriminator 1 becomes zero on predicate
failure while the invalid carrier remains copied. The kind mapper's ordinary
unsupported-category fallback returns zero, colliding with supported category 2;
that return is not availability or acceptance.

DeviceLibrary rows keep a parent Source plus a distinct row kind. They later
resolve a new action carrier through a Source-keyed RootSourceDataSet provider.
Lookup miss returns a real fallback object with initially cleared meaningful
carrier bytes; nonnull is not a found/available result and permanent immutability
is unproved. The temporary provider-factory control is released before the raw
provider getters, supplying no custom operation-scoped lifetime lease.

Provider ordering compares Source discriminator, carrier word and full byte 4;
the native row-matching predicate also compares byte 5. The row-update return
reports whether a flag changed, not a found, unique or accepted source. Both
comparisons preserve the distinction between historical USB1/USB2 descriptors
0102/0122 even though their category bits are both 2. Those old named-port
observations remain bounded to their existing export fixture; they are not a
fresh provider identity or universal port enum.

One first-time check passes 12 new complete code/FDE extents, 238 instruction
relations, 13 pointers, four RTTI names and five imports, with independent source
and retained-result audits. All 83 earlier full extents are excluded and no
runtime suite is replayed. No new LSDA table is decoded. Private captures and
diagnostic logs remain private.

No native call, emulator/device action, adapter, USB button, post-return read,
reference-count mutation, package/catalog change or release was introduced.
Next connect actual permitted Local Library arguments to a live row/provider
and owner scope; label-to-DeviceLibrary-kind correlation, media freshness and
ambiguity, full wrapper ABI/unwind, queued/reentrant lifetime, readiness and
cancellation remain open. All existing runtime gates remain CLOSED. Do not
repeat the new passing pin set unchanged.

## November 4: native row image/text contract and bounded observer plan

The new [activation-label-contract review](../experiments/usb-browser-switch/activation-label-contract/)
connects the actual indexed DeviceLibrary row to its component updater and
separate provider slots for image and text. Image keys 0/1 select named
local-library USB/SD resources; the corresponding text getter returns empty.
This is a resource association, not proof of rendered pixels, a universal
English label-to-kind mapping, a USB1/USB2 enum, or a current live action.
The reviewed helper anchors establish comparison and forwarding to another
helper, without claiming that unreviewed helper's internal assignment semantics.
No asset image was unpacked or rendered.

One first-time static check passes six new complete function/FDE extents,
394 instruction relations, six pointers, 11 short strings and four imports.
Independent source and retained-result reviews pass; all 95 earlier complete
extents and old runtime suites remain unreplayed. No new LSDA is decoded.
The large initializer fingerprint binds its complete extent while semantic
claims remain scoped to the first two image keys and their direct consumer.

[NEXT-OBSERVER.md](../experiments/usb-browser-switch/activation-label-contract/NEXT-OBSERVER.md)
is a concrete plan for two passive register-entry snapshots with an 800-byte
frame and tail forwarding. It remains unimplemented and untested. Action x2
is opaque pointer bits, service Source is captured from registers, no pointee
or post-return native read is proposed, and separate records do not prove
cross-entry event ownership. Next implement a separate offline fixture with
full fixed-width state, caller stack/result, nesting/exception and helper
backtrace checks and safe negative controls; the old executor tests do not
validate this new wrapper. Installer/island unwind remains a separate gate.

Historical named-port evidence and structural Source/provider limits remain
intact. Live row/action arguments, operation-scoped owners, alternate destructive
or replacement paths, queued/reentrant work, callback quiescence, final readiness
and cancellation remain open. No emulator/device action, adapter installation,
USB button, core/package/catalog change or release occurred. Every existing
runtime gate remains CLOSED; do not replay these passing static pins unchanged.

## November 4: offline USB register-entry fixture

[activation-entry-observer](../experiments/usb-browser-switch/activation-entry-observer/)
implements the separate passive plan with two 800-byte entry frames, fixed-width
register/control restoration and a final original-slot tail branch. A sixteen-
record append-only sink uses try-only exclusion and a separate capture-ready
gate. It stores only entry register values: action x2 stays opaque, service Source
uses twelve register bytes, and no native pointee or post-return read is added.
No return interceptor, pending TLS action or cross-entry pairing is introduced.

The first new ARM64 matrix passes 31 checks in eight selectors, plus five
compiled exact-oracle controls and two compiled-only malformed-CFI controls.
Each entry has 87 instructions, one helper call, one terminal BR and 88 checked
CFI states. Both entries have full synthetic state coverage; typed scalar/pair/
HFA/vector/x8 aggregate returns, stack arguments and target/caller exception
cleanup exercise the action entry. Two later normal invocations succeed after
the nested throw. Disabled, full and forced-contended capture still forward.

The production helper configuration is compiled separately; the executed helper
is symbol-renamed with fixture-only sink-hold functions, while an external
interceptor supplies state stress and a live stack walk through the entry frame.
That backtrace is distinct from target exception unwinding after the tail branch.
Contention uses two actual threads under a fixture-held flag and is not a general
recording-race proof. No sanitizer or old suite was run. The initial synthetic
STP offset error was repaired during source review before any compilation; the
first completed matrix passed without changing its assertions. Independent source
review and a retained-byte audit pass; the latter checks all 60 commands, 44
artifacts and 330 retained files without test replay or new ELF analysis.

Original slots require stable initialized storage before reachability. Closing
ready supplies neither quiescence nor reclamation permission. x16 tail scratch,
installer x17/island behavior, live row/provider/argument provenance, event
ownership, alternate destruction/replacement, queued work and readiness remain
separate gates. Next review exact SDK eligibility and publication/lifetime for a
separately scoped passive binding; do not repeat this passing matrix unchanged.
No hook installation, emulator/physical action, USB button, core/package/catalog
change or release occurred. All existing runtime and product gates remain CLOSED.

## November 4: SDK slot contract and passive binding lifetime

The separate [binding review](../experiments/usb-browser-switch/activation-sdk-binding-review/)
finds a concrete interface mismatch before installation: frozen USB originals are
`std::atomic<uintptr_t>` objects, while the exact SDK `claim_hook` requires
`void **`. Existing target size/alignment checks do not justify a cast. One new
ARM64 compile-only matrix confirms a permanent aligned plain-pointer argument
compiles, direct atomic passage is refused at its call, and a wrong expected
SDK signature fails the named assertion. Both refusal cases emit no object.
The one positive object is never linked/executed; no old matrix is replayed.
Independent source and retained-byte audits pass, including all five compile
inputs, eight command receipts, both extractor attempts and 110 source blobs.

The intended separate binding must pass each permanent original slot directly,
because native entry can occur before claim return. A failed second claim keeps
the first forwarding path and storage reachable; capture close is not unpatch,
quiescence or reclaim permission. The frozen reusable bool capture gate is not
a terminal installer protocol. A separate state CAS followed by setting that
bool has a possible close/reopen window; the successor should check one
authoritative terminal state directly in capture. This is a source-level
schedule and an unimplemented correction, not a new runtime observation.

Prior prologue eligibility remains accepted within its exact static scope.
A 110-blob pinned-source search finds no overlapping 0x/EP_ address literal;
computed targets and current loaded hook ownership remain unproved. Only the
service +4 CFI row is newly decoded: CFA SP+272, saved x29−272/x30−264. The
action +4 row is reused from retained evidence. Neither supplies anonymous
island/trampoline unwind metadata. The initial extractor import failure is
retained; preserving the venv launcher fixed it before the first ELF read.

Next implement and check the separately versioned slot/capture-state composition
for first/second refusal, entry before claim return, repeated init and terminal
close schedules. No source of the passing observer, SDK/core, community package,
catalog or device is changed. No installable observer or USB button is supplied.
Live native provenance/owner scope, writer exclusion, stopped recovery and all
existing runtime, activation, stems and release gates remain CLOSED.

## November 4: offline permanent-slot and terminal capture-state composition

The separate [binding composition](../experiments/usb-browser-switch/activation-binding-composition/)
implements the next scoped correction without changing the frozen observer.
Permanent aligned plain `void *` slots pass directly to the exact SDK `void **`
interface; one authoritative atomic enum governs installation and helper
admission. A final Installing-to-Ready CAS cannot overwrite close, failure
preserves an earlier Closed state, and no temporary copy, retry, reset, unpatch
or reclamation path exists. Partial successful claims retain their forwarding
storage even if another claim is refused.

The first complete ARM64 matrix passes 39 checks across 14 fresh selectors per
plain and ASan/UBSan mode, with five exact safe semantic controls in each mode.
Checks cover exact slot/entry identity, callback before claim return, authentic
prepublication first/second refusal, malformed missing-original success,
reentrant/repeated init, close during installation/refusal and after admission.
A real worker deterministically publishes its admitted record after close, then
joins before assertions; later closed calls still forward without recording.
This is selected ordering evidence, not callback drain or general race proof.

Production helper/binder configurations are compile-only and contain no fixture
hooks. Execution uses two explicit compile-time scheduling seams and the newly
compiled entry object; the latter is byte-identical to the retained parent.
The parent object is comparison-only and no old test matrix is replayed. The
new run retains 17 inputs, 106 commands, 56 artifacts and 550 files privately;
source and sanitized result metadata are shared. Independent source review and
a retained-byte audit pass, verifying all inputs, command receipts, 212 output
streams, artifact bytes, mutations and public projection without execution. ASan/UBSan covers selected
instrumented helper/binder/fixture paths, not assembly instrumentation or the
whole engine. No TSan or installed-hook unwind result is claimed.

Next review the exact installer's scratch-register and anonymous-island/trampoline
unwind contract, plus actual native row/provider/argument/owner execution context,
for a separately scoped passive adapter. Nonnull-before-reachability remains an
SDK obligation; detecting null on return cannot protect an earlier malformed
callback. Closing capture does not establish quiescence or reclamation. Native
pointee/post-return reads, return interception and cross-entry pairing remain
absent. No emulator/device, official core, community package, catalog or release
was changed. Writer exclusion, stopped recovery, USB activation, stems staging
and every unresolved runtime/product gate remain CLOSED.

## November 4: offline actual installer transfer and ordinary exceptions

The [new authored-target transfer fixture](../experiments/usb-browser-switch/activation-transfer-fixture/)
executes the pinned SDK's real late island/trampoline path through the frozen
production binder/helper/entry in an isolated ARM64 process. A validated
claim-provider substitution maps the requested identities only to authored
assembly targets. Real allocation, protection queries, cache flushes and
one-word patching are exercised; actual community-loader admission, firmware
and live application ownership are not.

The first complete new matrix passes 32 named checks across four fresh selectors
per plain and ASan/UBSan mode. Both authored entries have return and ordinary
body-throw coverage, with exact SP/LR/argument/capture and body/caller cleanup
checks. Structural route/RX validation precedes invocation. Body x16 equals the
original trampoline, and x17 equals target +4; the island's wrapper-entry x17
value is inferred from emitted instructions rather than directly captured.
One paired x16 control preserves valid destinations and fails only two exact
oracles per selector. Two malformed CFA controls retain identical code but are
compiled/rejected without linking or execution.

This is the first execution of the unchanged production helper/binder
configuration in this scoped route; no prior suite is replayed. Independent
source/claim reviews and a retained-byte audit pass over 14 inputs, 82 commands,
164 output streams, 72 artifacts and 462 files. Raw evidence stays private.
Assembly/generated code is not sanitizer-instrumented, and no TSan or general
concurrency/lifetime proof follows.

Ordinary body exceptions use authored native/caller FDEs after the tail transfer.
The installer supplies no anonymous island/trampoline FDE, particularly after
the copied STP changes SP. Interrupted-PC unwind and actual native execution
scope therefore remain open. Resolve that unwind policy together with permitted
native invocation/row/provider/owner scope and alternate destructive/queued
paths before a passive adapter; do not install this fixture or rerun its passing
matrix unchanged. Closing capture still grants no drain, unpatch or reclamation.
No emulator/deck action, official-core/package change or release occurred.
Writer exclusion, stopped recovery, USB activation, stems staging and all
unresolved runtime/product gates remain CLOSED.

## November 4: anonymous transfer policy and generated-code attribution

The [new source-only review](../experiments/usb-browser-switch/activation-unwind-policy/)
resolves a distinction left open by the authored transfer fixture. Ordinary
body exceptions after tail transfer and normal signal return do not require
unwinding anonymous transfer PCs. Interrupted-PC stack walking remains
unsupported, while nonlocal exits/forced unwind through the noexcept helper
are separate unresolved contracts. No new handler, unwinder, core patch,
fixture, adapter or native observation is implemented.

The exact SDK crash reporter and blame path walk PC/LR and x29-linked frame
records, then look up module PT_LOAD envelopes. They do not use DWARF. The
installer records claimed native functions but no generated-range ownership.
A first-attribution attempt with an island/trampoline PC, native LR and native
frame chain all outside mod envelopes therefore misses attribution even after
a successful claim. This is a conditional source counterexample, not a
witnessed crash, fault-causation result or quarantine reset.

The SDK has no generated-range preparation receipt. If precise anonymous-PC
unwind is required later, registration must precede reachability and account
for cleanup that frees a trampoline before publication, and for the concrete
target-unlock-to-publication interval holding protection/claim locks. Closing
capture does not remove the transfer route or establish quiescence.

The bounded capture retains twelve inputs, eleven pinned Git source reads,
twenty-two output streams and fifty-seven files privately. Independent source
review and a retained-byte audit verify the evidence without re-executing the
collector or any prior matrix. The lexical search covers only the eight
captured SDK files and establishes no whole-program absence. Source and
sanitized receipts are shared; raw sources/commands remain private.

Next add a separate offline attribution-contract fixture around the exact
existing ownership/frame-selection logic and synthetic owned records. Preserve
PC/LR/frame precedence, unreadable-frame truncation and envelope-gap limits.
Only then review a core-owned per-island-slot/trampoline-range registry with
publication-before-reachability, failure retention, signal-reader lifetime and
explicit capacity/overlap refusal. Attribution is ownership, not causation.
This proposal does not fix or replace the unwind contract.

Register-only passive capture and activation have different prerequisites:
opaque pointer/register copies need no native pointee dereference, but live
image/entry provenance, actual SDK admission/hook ownership, stable code/originals,
accepted control-flow scope and writer/topology/recovery safeguards are still
missing. Final Local Library row/provider/readiness, arguments and operation
ownership, alternate destructive/queued paths and post-callback timer lifetime
remain activation gates. No firmware/emulator/deck access, code installation,
package/catalog change or release occurred. Main/live dispatch, stems staging,
USB activation and every unresolved runtime/product gate remain CLOSED.

## November 4: exact attribution functions with synthetic owned records

The [new offline contract fixture](../experiments/usb-browser-switch/activation-attribution-contract/)
executes exact pinned cm_range_cb, cm_owner_of and cm_blame_fault bodies. It
reuses the retained source without rerunning its collector. Explicit synthetic
program-header/mod fields, integer-keyed frame cells and file-operation ledgers
replace native loader/layout, memory and persistence dependencies. Each selector
runs in a fresh process; the original function-local static written is retained
without any reset seam.

The first complete serial host matrix passes 111 checks across seventeen
selectors per plain and ASan/UBSan mode. Five safe source controls compile and
fail exactly their reviewed functional oracles in each mode. They distinguish
upper-bound inclusion, PC/LR ordering, failed second-read truncation, a
seventeenth frame and once-guard bypass. No assertion changes, crashes or
sanitizer diagnostics are accepted as a control result.

The source counterexample is now exercised as a lookup model: an anonymous PC
and native-only LR/frame chain miss every mod envelope. No successful claim,
generated transfer or actual fault occurs in this fixture. Positive cases
retain PC/LR/first-frame precedence and exact read order. The range callback
includes non-executable PT_LOAD segments and gaps in its half-open envelope;
overlaps choose the first index. The sixteenth frame may match; the seventeenth
is not read. Selected unreadable/invalid/nonincreasing-frame behavior is checked.
Ownership can still return an id despite modeled open/write/sync/close failure,
and later calls are suppressed; that id does not establish persisted blame.

Independent source/control review and a retained-byte audit verify four inputs,
57 commands, 114 streams, 70 artifacts and all 303 files. No old matrix or
capture is replayed. Apple Clang targets ARM64 macOS: this is not a Linux SDK
build or actual loader ABI test. ASan/UBSan covers the compiled fixture/extracted
functions with leak detection disabled; no TSan, real signal, volatile-guard
synchronization, durable logging, general race or lifetime proof follows.
Raw source copies, generated includes, executables/debug files and logs remain
private. Source and sanitized hashes/outcomes are shared.

Next review the separate core generated-range contract: exact per-island-slot
instruction coverage and trampoline extents, validated owner custody, bounded
capacity/overlap refusal, signal-reader visibility before redirection, and
every success/refusal/cleanup path. Failed prepublication cleanup must leave
no stale attributable range; published records/code need permanent retention.
Keep existing native PC/LR/frame precedence and quarantine protections. No
registry or repair is implemented yet, and ownership is not fault causation.

No hook, firmware, emulator/deck action, core installation, package/catalog
change or release occurred. Real admission/current hook ownership, native
provenance/control-flow scope, writer/topology/recovery and Local Library
activation/lifetime gates remain unresolved. Main/live dispatch, stems staging,
USB activation and every unresolved runtime/product gate stay CLOSED.

## November 4 — generated-range contract review after attribution fixture

This source-only successor to c95d8f3d0db3ce8155395a09918f09dc58a8b00a is
[activation-generated-range-contract](../experiments/usb-browser-switch/activation-generated-range-contract/README.md).
No registry, repair patch or executable fixture is implemented here.

Exact emitted instructions exclude literals: island [I,I+8), trampoline
[T,T+4(r+2)), where r=n+3m, n is one/four copied instructions and m counts
four-instruction ADRP expansions. Shared 64-KiB pools and 4096-byte trampoline
mappings are allocation extents, not ownership extents. Every inline-installer
refusal, cleanup attempt and warning-success branch is distinguished.
Existing unchecked munmap calls do not prove successful release.

The loader retains handles and installed hooks after failed init, while
g_cm_live counts only successes. Its static IDs have a current one-start call
graph, but community_init lacks an internal once guard. Claim admission checks
an init window without checking thread identity; those checks precede the
claim lock. Ambient g_hook_owner and existing postpublication bookkeeping do
not provide a stable authority/publication protocol. The latter silently stops
at 512 targets; the separate diagnostic table has 256 entries.

Selected remove/disable paths change disk state, and Reload/Apply request
process restart rather than in-process reclamation. A failed request does not
end lifetime. Independent review caught a precise Reload limitation:
non-EINTR waitpid failure is normalized to status zero and may permit the
restart request; this does not prove daemon-reload succeeded. No such path was
executed. Direct core callers and community callers have different existing
lock contexts; a successor needs one explicit compatible writer protocol.

Proposed records copy validated owner kind/ID/length, reserve capacity and
checked nonoverlapping instruction extents, remain invisible during fallible
preparation, and publish after successful target unlock but before either
original-slot or redirect publication. Publication must be bounded and
infallible, with no new callback/lock/allocation in the protected interval.
After visibility, code/identity/record remain permanently retained. Readers
skip unpublished payload and never wait for an interrupted writer. Exact
lock-free atomic code generation and cross-thread/signal ordering still need
proof; legacy signal guards, module reads, handlers and persistence are not
certified by this design.

Lookup remains PC then LR then eligible frames, module-envelope first at
each address. A registered generated community PC can now beat a module LR;
that is the intended extension, not preservation of every historical winner.
Core ownership continues community fallback and never becomes a fabricated
community ID. New generated intervals refuse overlap with known module
envelopes, but later envelopes may mask them. No reverse loader admission,
DSO unloading or permanent global non-overlap is claimed.

The packet reuses eight exact SDK inputs and captures five additional sources
at 3e0c5cb010454228f4ee63226b60d060494eeaae: fifteen total retained inputs,
five Git reads, ten streams and 36 private files. Source review and independent
byte audit pass without replaying old capture/test matrices. Public material
is source and sanitized receipts; raw source copies/commands remain private.

Next implement a separate offline bounded metadata/lookup fixture with modeled
installer transitions, copied-owner and failed-init retention, exact interval
bounds, capacity/overlap refusal, invisible/visible reader schedules,
metadata-before-original-before-redirect ordering, Core fallback and later
module masking. Use strict safe negative controls on new behavior only.
No live adapter follows from that fixture. Native provenance/admission/current
ownership, control-flow scope, writer/topology/recovery, Local Library
row/provider/readiness/owner lifetime and stems staging gates remain CLOSED.
No firmware, emulator/deck action, core installation, package/catalog change
or community release occurred. Ownership remains distinct from causation.

## November 9 — offline generated-range metadata and modeled publication

The separate [generated-range fixture](../experiments/usb-browser-switch/activation-generated-range-fixture/README.md)
follows the source contract at 53e0b13abab00828439939a5ef131d871faf988b.
Eight fixed slots retain copied synthetic Core/Community identity, a target16
reservation and exact instruction-only island/trampoline ranges. Tickets bind
registry/instance/slot/generation; a move-only permit freezes validated data.
Only invisible reservations may be reused. Published payload and identity never
change, and readers acquire committed flags before touching those values.

This fixture adds conservative full emitted-storage exclusion, including literal
tails, against other blobs, known module envelopes and retained target16 ranges.
Lookup still excludes generated literals/slack and remains module-first per
PC/LR/frame. A generated community PC may beat a module LR; Core continues the
community search. Later module snapshots may mask earlier generated records.
No allocation/page ownership or reverse loader admission is inferred.

The first completed host matrix passes 73 named checks across sixteen fresh
processes per plain and ASan/UBSan mode, eight exact safe controls per mode and
two compiled-only memory-order controls. It covers all seven relocation-length
variants, literal/overflow/capacity boundaries, copied identity, stale tickets,
modeled refusal/cleanup and publication order, permanent retention and selected
ordinary-thread/nested-call schedules. The frame walk is newly authored scalar
composition; no old exact SDK function suite was replayed.

Two prior attempts stopped before any runtime execution: clean default and
-march=armv8-a-only compilations emitted LDAPR where the strict oracle required
LDAR. A retained non-compiling driver diagnostic confirmed the explicit generic
CPU configuration. With -mcpu=generic -march=armv8-a, lookup has LDAR and commit
STLR, without the reviewed atomic/lock-helper calls. Library/test sources and
the strict oracle stayed unchanged. LDAPR is not labeled an incorrect acquire.

Independent source/control review and retained-byte audit are linked in the
fixture. The completed matrix retains six inputs, seventy commands, 140 output
streams, 130 artifacts and 417 files privately; two refused configuration packets
retain nineteen files each, and the driver diagnostic retains four. Only source
and sanitized receipts are shared.

The library requires external single-writer serialization and stable registry/
module-snapshot lifetimes. It supplies no SDK owner authentication, real emission
receipt, original/redirect, signal handling, native cleanup or persistence.
Warning outcomes test record retention; their exact SDK stage order is source
evidence. Failed init is modeled; disable/remove/Reload paths are not executed.
Generation wrap is refused in source but not driven to exhaustion.

Next perform a source-only integration review of enforceable common core/community
writer admission, exact owned emission receipts, original/redirect ordering,
immutable module inputs and failed/partial-init resource custody. Do not install
this fixture or rerun its passing matrix unchanged. Apple ARM64 macOS host codegen
and ASan/UBSan, with leak detection disabled, do not prove Linux SDK ABI, signals,
whole-engine races/lifetime, callback quiescence, native timing or recovery.
No emulator/deck, package/catalog, official core or release changed. All passive
attachment, Local Library activation, writer/topology/recovery and stems gates
remain CLOSED. Ownership is not fault causation.

## November 9 — source-only generated-range core integration review

The separate [integration review](../experiments/usb-browser-switch/activation-generated-range-integration-review/README.md)
follows the offline fixture at cd48d522da0f9c2b6ff0d5c8e04733c900928d7b.
It identifies a common azmod inline-installer boundary, rather than claiming a
universal patch lock. The proposed order is existing recursive g_cm_mu, new
inline writer, then the azmod protection mutex; direct Core uses writer then
protection and consumes immutable module inputs without reversing that order.

One genuinely new pinned Makefile confirms azmod and azstem are separate C
builds. Their header-static protection mutexes are distinct. Vtable, raw patch
and azstem writer families remain outside the proposed inline transaction.
Constructor-to-late transition must share the writer discipline, while actual
native quiescence and loader/interposition/nonlocal-exit behavior remain open.

Claim admission must capture the caller RA at the outer SDK entry and revalidate
the init epoch under the same discipline used to close it; successful bookkeeping
must finish before release. The proposal preserves existing claim predicates and
does not invent a thread-identity restriction. Claimant identity is not proof
that the replacement hook resides inside that owner. Permanent original-slot
and replacement-code custody remain required beyond nonnull or shape checks.

The source review identifies a conditional range-provenance gap: cm_range_of can
run after failed dlinfo, using a fresh record's zero scratch base. If enumeration
finds a zero-based main image, its envelope can be selected without proving the
intended DSO. No such failure was observed. The proposed module boundary needs
explicit successful-handle/base provenance, checked private scratch and fixed
per-index one-way publication. Valid module attribution precedes entry/descriptor
validation and survives failed descriptor/init; it does not cover earlier dlopen
constructors. Later module masking and module-first PC/LR/frame ordering remain.

An owned installer receipt must use actual relocation output, retain pool/slot
and mapping custody before failed helpers discard details, separate island and
target protection outcomes, and bind the exact original/replacement storage.
Successful relock restores saved bits, not necessarily RX after an earlier warning.
All fallible work precedes target unlock; metadata commits infallibly before
original and redirect, with permanent retention after visibility. The frozen C++
fixture's defensive commit errors and Apple codegen are not an implemented C/Linux
core contract.

The new packet reuses the retained 110-source corpus, extracts 23 selected reused
SDK sources, and captures only src/Makefile: one Git read and two output streams.
It retains 33 inputs and 39 files privately. Independent source review and a
retained-byte audit are linked; no old capture or passing matrix, compiler, make,
firmware, emulator or deck action was repeated. Raw material stays private.

Next implement a separately versioned offline C admission/transaction fixture
for these new source boundaries, with exact safe negative controls. Do not install
or compose it into the observer. No production registry integration, repair patch
or new executable fixture exists in this review. All live, Local Library activation,
writer/topology/stopped-recovery and stems gates remain CLOSED. Ownership is not
fault causation; quarantine and every historical failure remain preserved.

## November 9 — offline C admission and transaction fixture

The [separately versioned C fixture](../experiments/usb-browser-switch/activation-admission-transaction-fixture/README.md)
follows the source integration review at 3f9b8078e090f7b688d1902b261653a0fb130548.
The first complete matrix passes **51 named checks in twelve fresh-process
selectors per plain and ASan/UBSan mode**, plus seven exact safe controls per mode.
One standalone C object without fixture seams compiled only; it was not linked
or executed. No compiler or runtime attempt failed. Old captures, configuration
attempts and passing matrices were not replayed.

Actual pthread mutexes enforce the authored cm → writer order for Community
mutation and writer-only Core/late transition. A claim delayed until after init
close refuses; an admitted claim completes and accounts nhooks before close can
advance. Real EBUSY observations establish the selected contention schedules.
These extra trylock and gate callbacks are fixture-only instrumentation, not a
signal/native scheduling proof. The authored cm mutex is nonrecursive, narrowing
scope relative to the SDK's recursive g_cm_mu.

A single loader-start guard and active init epoch bind copied input values and
owner identity. Explicit handle/link-map/object provenance is separate from a
zero load base. Per-index immutable module flags preserve holes and first-index
precedence, publish before descriptor/init and survive their failure. Target
eligibility, caller RA, loader facts and supplied envelopes remain synthetic;
full SDK/native predicate and ELF discovery are not implemented.

Both caller families reserve the same eight emission slots, target spans and
bounded attempt ledger. All reservations remain consumed on failure, a stronger
conservative availability policy than the older C++ fixture. Private permit
faults refuse before modeled target unlock. The successful cut performs
metadata publication, internal original store, then redirect without a new
callback/provider/lock/refusal. Fixed atomic integer slots do not prove arbitrary
SDK void** or replacement-code lifetime.

Separate modeled allocation/seal/cleanup and island/target protection receipts
retain raw outcomes and uncertain custody. Burned island slots remain consumed;
successful relock can restore writable saved bits. Failed init and postpublication
warnings preserve visible records. No actual mapping, code relocation/cache
publication, native patch, protection syscall or recovery was performed.

Seven inputs, 56 commands, 112 output streams, 110 artifacts and 342 files are
retained privately. Independent source/claim reviews and a retained-byte audit
are linked. Source review corrected three draft harness setup/oracle mismatches
and added entry value copies before the first execution; these were not hidden
failed runs. Exact control failure sets are preserved. The epoch-control macro
changes both admission and final permit checks. Apple ARM64 macOS C evidence,
with leak detection disabled, is not Linux core ABI/codegen, TSan, real signal,
general race/lifetime or native custody proof. Epoch exhaustion, pthread failure
paths, disable/remove/Reload and asynchronous exits are not exercised.

Next scope an offline composition with the exact retained C installer, replacing
authored emission/protection results with actual owned receipts. First specify
permanent original/replacement custody and the prepublication failure boundary;
do not treat this model as a live adapter or replay its passing matrix unchanged.
Nonparticipating writers, loader/interposition, actual topology/stopped recovery,
Local Library readiness/operation lifetime and stems staging remain unresolved.
No device, official core, community package/catalog or release changed. Every
live gate remains CLOSED; ownership is not fault causation.

## November 9 — permanent installer custody and actual receipt plan

The new [activation-installer-receipt-plan](../experiments/usb-browser-switch/activation-installer-receipt-plan/)
is a source-only successor to the C admission/transaction fixture. Existing
implementations and tested source bytes remain frozen. The next implementation
must bind exact authored target/replacement symbols and permanent aligned plain
`void *` original objects through fixed process-lifetime descriptors. Synthetic
atomic integer slots and test-system destruction cannot own reachable real code.

Calling the completed model before or after the unchanged installer cannot
establish metadata-before-original-before-redirect. The installer owns the
actual island, `ncopy`, relocation `out`, trampoline and protection stages,
and publishes internally. A separately named copied-source successor needs an
explicit reviewed diff and private lexical publication cut. First reserve
target, binding, metadata and journal capacity; then retain actual mappings and
derive exact spans before final validation/target unlock. Immutable lookup
metadata is separate from later mutable warning/custody receipts. No callback,
new lock, allocation, provider or refusal is allowed between successful target
unlock and metadata → actual plain original → target branch stores.

Under the serial private allocator invariant, one call makes at most six
candidate island mmap attempts and one trampoline mmap. Out-of-reach successful
mappings also need exact cleanup receipts; failed munmap cannot imply release.
Retain syscall results/valid failure errno before cleanup or logging, and
distinct initialized island/target tokens valid only after successful unlock.
Relock restores saved bits, which may be writable. A checked round-up guard
must use the same page size as the future copied helper.

Two bounded source counterexamples remain explicitly unobserved: helper
rounding does not check its second addition, and saturated optional diagnostics
can make the old transfer bridge return refusal after an installer success.
The historical fresh two-claim transfer result remains valid. New success and
custody decisions must come from internal receipts, not the diagnostic table.
A published route with warnings is never cleanup-eligible refusal; unsupported
interruption requires retained incomplete/unknown state, not rollback.

The packet reuses twelve inputs and adds only the plan/retention script:
fourteen inputs, eighteen line-byte anchors and sixteen retained files.
Independent source/future-control review and a byte-only audit are linked.
No new SDK Git reads, compiler, installer, matrix replay, firmware or device
action occurred. Ten future case groups and five safe controls are specified,
not executed; metadata ordering needs actual store/load observations rather
than ordinal labels. Deterministic discarded-candidate cleanup needs a declared
owned placement seam, otherwise first coverage uses trampoline cleanup.

Next implement a separate offline late-island-only receipt successor for both
typed Core and Community authored bindings, preserving the pinned emitter and
eligibility policies with a reviewed transformation ledger. Defer fallback and
old ABI/exception replays. No production integration, new executable installer,
repair patch or live observer exists in this checkpoint. Arbitrary DSO/slot
custody, other writers, loader/interposition, signals/nonlocal exits, native
publication/recovery, Local Library activation and stems staging remain gates.
Quarantine and every live gate stay CLOSED; ownership is not fault causation.

## November 9: offline installer receipts over owned targets

The separately versioned [receipt fixture](../experiments/usb-browser-switch/activation-installer-receipt-fixture/) now runs a transformed late-island installer over permanent authored target/replacement bindings and strong, aligned plain-pointer original objects. It reuses the pinned SDK sources and preserves ten selected emitter/policy/parser bodies; its complete transformation ledger distinguishes the changed orchestration. No community-loader or production registry integration is present.

The first runtime matrix passes 60 summed assertions (48 distinct names) across 16 fresh-process selectors per plain and ASan/UBSan mode, with five exact safe controls per mode. This is GCC 7.5 Linux AArch64 C11 evidence in the pinned isolated image; the standalone configuration is compile-only and all executions use declared fixture seams. The two Community emission selectors observe ADRP expansion to out4; the two core-emission selectors use out1. No ADRP re-encoding, old ABI/exception suite or successful predecessor matrix was replayed.

Actual owned allocation, relocation, maps reads, protection and cleanup outcomes are retained separately from injected pre-call refusals. Prepared immutable metadata is released before the actual original and redirect stores; an attached metadata load discriminates the ordering control. Published warnings retain originals/code, separate island/target tokens preserve saved writable bits, and non-RX routes are never invoked. Queued epoch closure refuses before installer entry; admitted publication accounts under the common writer before closure. Community identity/envelopes remain authored facts, and Core has no community blame ID.

The first clean standalone compile was refused because GCC emitted the tentative original array as SHN_COMMON. An explicit null initializer supplies a strong object; the ELF oracle remains strict. A separately discovered raw-result schema omission of authentic -3 was corrected before runtime. Preserve the failed packet and exact tested successor bytes. The complete runtime packet retains 12 inputs, 143 commands, 286 primary streams, 176 work artifacts and 769 files; the earlier compile-only packet has 42 files. Independent source/claim review and retained-byte audit are linked in the fixture.

Next scope the exact retained community claim ingress, init-close and hook-accounting composition around these receipts, identifying transformations and authored caller-RA/loader/capability inputs before implementation. Preserve fixed original/replacement custody and irreversible partial-publication accounting. Other writers, arbitrary DSO lifetime, native visibility, signals/nonlocal exits and recovery remain unproved. No emulator/deck access, live observer, production patch, package/catalog release or stems fix occurred. All live, activation, recovery and staging gates remain CLOSED; ownership is not fault causation.

## November 9: exact community claim and receipt composition plan

The new source-only activation-claim-receipt-plan binds exact retained community ingress/init-result code to the completed owned receipt boundary. It specifies one recursive SDK cm → common writer authority, a private already-locked receipt entry, entry-captured caller RA and a one-use atomic session handle that cannot adopt a later init while queued. It identifies the SDK's 32-character IDs versus the frozen receipt's 31-character limit and specifies 33-byte copied owner storage. The extra fixture replacement-envelope predicate is removed in the planned successor while fixed replacement/original custody remains. Normally returned published warnings map to SDK0 and increment actual SDK nhooks once before init close; failed init keeps published originals, metadata, receipts and count. Loader, caller-RA and manifest capability facts remain explicit authored inputs, with valid-zero-base provenance and callback execution outside both locks. Fifteen reused inputs plus the new plan/script retain 17 inputs, 23 anchors and 19 files; independent source/control review and retained-byte audit pass without new SDK Git reads, transformer/compiler/installer/matrix replay or device work. Ten future case groups and six safe controls are specified, not executed. The pre-open session control omits the entire new nonzero/current/index veto so legacy loading checks do not mask it; immutable permit construction has no duplicate token veto. No production integration or live composition exists. Next implement a separately versioned offline C claim/receipt composition with the reviewed transformation ledger and authored inputs; preserve the frozen receipt fixture and all live gates.

See [the separate plan](../experiments/usb-browser-switch/activation-claim-receipt-plan/README.md), [pinned transformations and future controls](../experiments/usb-browser-switch/activation-claim-receipt-plan/composition-plan.json), and [verification](../experiments/usb-browser-switch/activation-claim-receipt-plan/verification.json). The separately hash-bound README clarifies release publication of session open/close; the first retained machine-plan bytes remain unchanged. This is source reasoning and a future test contract, with no new executable composition, native evidence, package release or stems fix.

## November 9: offline community claim and receipt composition

The separately versioned offline C claim/receipt composition now passes its first GCC 7.5 Linux AArch64 matrix: 64 summed assertions (54 distinct names) across eleven fresh-process selectors per plain and ASan/UBSan mode, plus six exact safe controls per mode. The ordinary builtin-caller-RA configuration is compile-only; all 34 runtime processes use declared authored-ingress/scheduling seams. Three selected SDK helper bodies and the retained generated installer/protection files remain exact; a complete source ledger identifies the new admission/lifecycle orchestration. One recursive community mutex → common writer preserves entry-captured one-use session handles, 33-byte copied IDs, fixed original/replacement custody, warning-to-SDK0 mapping and one module nhooks count before close. Failed/partial init retains published code, originals, metadata, receipts and count. An actual EBUSY observation distinguishes close contention, while entry/init callback probes complete on other threads with both locks released. Loader/caller/capability facts remain authored; no patched target or replacement is invoked. The first packet retains fifteen inputs, 151 commands, 302 streams, 190 work artifacts and 823 files; no unexpected compiler/runtime failure or predecessor replay occurred. Independent source/control review and retained-byte audit are linked. No production integration, emulator/deck action, live observer or release occurred; all unresolved gates remain CLOSED.

See the [offline claim/receipt fixture](../experiments/usb-browser-switch/activation-claim-receipt-fixture/README.md), [complete transformation ledger](../experiments/usb-browser-switch/activation-claim-receipt-fixture/composition-ledger.json), [sanitized result](../experiments/usb-browser-switch/activation-claim-receipt-fixture/result.json) and [independent verification](../experiments/usb-browser-switch/activation-claim-receipt-fixture/verification.json). Loader holes and valid base zero are lifecycle observations with no claim across those holes. Core-first/community target conflict is sequential. Actual builtin caller RA, actual loader admission, signal/nonlocal exits and native activation remain unproved.

Next make a separately scoped source-only plan for an offline Linux authored-DSO provenance/custody successor: actual builtin caller-RA and handle/link-map/PT_LOAD/entry/init observations, fixed permanently retained authored modules and exact original-slot bindings, constructor-before-admission behavior, and reviewed transformations before implementation. Retain successful handles after later provenance/descriptor/init refusal. Arbitrary DSO/slot lifetime, nonparticipating writers, loader/interposition, native visibility, signals/nonlocal exits and recovery remain gates. Do not repeat this passing matrix or old scalar/ABI/exception suites.

## November 9: source-only authored DSO provenance and custody plan

The separate activation-dso-provenance-plan specifies the next offline Linux authored-DSO experiment; it does not implement or run a loader. It reuses fifteen pinned inputs and retains the plan, README and retention helper: eighteen inputs, twenty-one line-byte anchors and twenty files. Eight transformation groups, eight future case groups and five exact safe controls are specified, not executed. The plan preserves all fixed host targets, replacements and strong plain-pointer originals while replacing authored caller/loader facts with proposed actual observations. Every successful handle must enter permanent custody before later provenance, descriptor or init refusal; all fixed root/dependency loads precede entry/init sessions with no locks held. Actual SDK descriptor/init ABI and a DSO BLR return marker distinguish ordinary builtin RA from authored input. One combined entry-owner veto avoids masked dependency controls; raw RA and admission-used RA have separate oracles, and the custody control keeps a declared safety hold without unloading. The first retention and independent source/control review passed; retained-byte audit evidence is linked. No new SDK Git reads, compiler, transformer, loader, installer, matrix replay or device action occurred. All live gates remain CLOSED.

The later current-auditor hash check failed because an output-parent normalization edit overlapped after the first successful audit. Both variants are retained; reversing that single confirmed edit recovers exactly the executed-source hash recorded in the first result. No plan input changed and no audit was replayed. The 1,636 audit assertions are structural byte checks, not executed loader cases.

See the [reviewed plan](../experiments/usb-browser-switch/activation-dso-provenance-plan/README.md), [pinned transformations and future controls](../experiments/usb-browser-switch/activation-dso-provenance-plan/provenance-plan.json), and [independent verification](../experiments/usb-browser-switch/activation-dso-provenance-plan/verification.json). Next implement a separately versioned offline authored-DSO provenance successor from this reviewed plan, using the exact retained SDK header and frozen host binding/receipt cut. Freeze and review the complete source-transformation ledger and exact safe-control failure sets before the first Linux build. Use actual ordinary caller RA and loader/entry/init observations only for fixed pinned authored modules; preserve explicit raw-versus-consumed fault outcomes, successful handles through later refusal, and irreversible partial-publication accounting. Do not move original/replacement custody into DSOs, invoke patched targets, replay the old scalar/ABI/exception suites, integrate a production loader or compose a live observer.


## 2026-11-09: actual authored-DSO provenance fixture

The separately versioned activation-dso-provenance-fixture now passes its first GCC 7.5 Linux AArch64 C11 runtime matrix: 37 summed assertions (36 distinct names) across eight fresh-process selectors per plain and ASan/UBSan mode, plus five exact safe controls per mode. All 26 processes execute ordinary builtin caller RA and actual dlopen/dlinfo/link-map/PT_LOAD/entry/init observations on four fixed authored default-namespace DSOs. Exact SDK header, fixed host target/replacement/strong plain originals and retained receipt cut remain unchanged; old RC_FIXTURE/CC_FIXTURE ingress seams are absent. Raw RA equals the ordinary DSO BLR return marker, foreign B calling A is refused while A session1 is open, and all eight authored target/replacement counters remain zero. Constructors and all loads occur at session0 before admission; joined reader probes complete from actual entry/init callbacks. Successful handles and module ranges survive descriptor refusal; partial init -7 retains published code/original/metadata/receipt/count1 and later call captures0/refuses. Raw successful query is separate from injected consumed failure; dependency and query controls are dry decisions and handle-bookkeeping control uses an explicit safety hold. All actual bases were nonzero; no zero-base DSO or new warning outcome was exercised. Initial clean DSO compilation hit a parser-only overlapping .debug_info extent refusal; narrowing instruction lookup to allocated executable sections preserved the strict oracle. The next host link exposed omitted assembly counter definitions, corrected with strong aligned counters. Both failed packets remain; nine successful DSO compile/link commands and eight metadata commands were reused under exact source/result/artifact pins. Final independent source/control review and first retained-byte audit pass over 32 inputs, 140 commands, 280 streams, 158 build artifacts, 26 reports and 814 files, plus 121-file and 140-file failed packets. The audit's 72949 structural assertions are not runtime case counts; its exact frozen source was copied before execution and unchanged afterward. Leak detection is enabled; receipt mapping/cache/protection occurs but no patched target/replacement or emitted code is invoked. No actual approval/manifest, arbitrary DSO/interposition, concurrent native invocation/publication, signals/nonlocal exits, anonymous unwind, callback quiescence, external-writer coordination or general lifetime/recovery proof exists. Generated-only lookup remains separate from module-first fault attribution. No production integration or live composition occurred.

Source, sanitized results and independent review are in [activation-dso-provenance-fixture](../experiments/usb-browser-switch/activation-dso-provenance-fixture/README.md). The earlier plan checkpoint is preserved; this successor supplies its first scoped runtime evidence. Next make a separately scoped source-only plan for composing immutable actual module publications with the retained generated-range reader and exact module-first PC/LR/frame fault consumers. Specify bounded first-index input publication, lifetime and constructor/provenance-refusal visibility gaps, typed Core/community ownership and partial-init retention before implementation. Reuse retained inputs and preserve both DSO fixture failures; do not replay the passing matrix or older scalar/ABI/exception suites. This remains offline ownership attribution, not fault causation, production integration or live observer authorization.


## 2026-11-09: immutable module and generated attribution plan

The separately scoped activation-module-attribution-plan reuses nineteen pinned inputs and retains the plan, README and helper: twenty-two inputs, twenty-seven line-byte anchors and twenty-four files. Eight source-transformation groups, ten future case groups and eight safe controls are specified, not executed. The design separates four fixed immutable module payloads from mutable lifecycle state, with canonical zero-padded 33-byte IDs and one-way release/acquire publication. A bounded module-first adapter reuses unchanged rc_lookup, resolving generated Community process identity plus full ID to exactly one permanent module index; Core and unresolved identities continue LR/frame fallback. Scoped aliases preserve the exact 962-byte cm_blame_fault body, its once guard and modeled persistence sequence without returning temporary metadata IDs. Positive attribution is independent of hook permission and survives descriptor/partial-init refusal; admission predicates remain intact. Constructors run before publication, provenance refusal leaves held handles invisible, and rootstub/dependency receive no borrowed Community identity. Per-slot reads are not a coherent snapshot; overlap/later masking belong to separate synthetic policy cases. Independent source/control reviews and first retention pass. The initial byte auditor falsely classified the helper's two re.compile calls as dynamic builtin compilation; its frozen failed packet is retained. A separately frozen auditor permits only that exact qualified regex AST, keeps bare/arbitrary compile calls refused, and passes 2,310 structural assertions on its first invocation. The plan and retained packet were unchanged; no old passing audit/matrix/capture was replayed. No new SDK Git reads, source transformer, compiler, loader, installer, runtime matrix or device work occurred. Exact tested predecessor bytes, both DSO failures and all live gates are preserved. No implementation, production integration, live observer, release or fault-causation proof follows.

See [the source-only plan](../experiments/usb-browser-switch/activation-module-attribution-plan/README.md), [pinned transformations and future controls](../experiments/usb-browser-switch/activation-module-attribution-plan/composition-plan.json), and [independent verification](../experiments/usb-browser-switch/activation-module-attribution-plan/verification.json). Next implement a separately versioned offline C/Linux immutable-module/generated-range attribution composition from this reviewed plan. Freeze and independently review the complete source ledger and exact safe-control failure sets before the first build. Keep the exact cm_blame_fault body through scoped aliases, unchanged generated reader/receipt cut and fixed host custody; use actual fixed authored-DSO inputs for loader cases and separate model-only paths for overlap, masking and identity-edge cases. Preserve copied canonical IDs, permanent publication, root/dependency visibility gaps, ordinary partial-init accounting and zero target/replacement invocation. Do not run native frame/signal/persistence paths, replay old matrices, integrate production or attach a live observer. Ownership is not causation; signal/nonlocal exit, arbitrary loader/interposition, external writers, native visibility, quiescence and recovery remain gates.

## 2026-11-09: offline immutable module and generated attribution composition

The separately versioned activation-module-attribution-fixture passes its first GCC 7.5 Linux AArch64 C11 matrix: 54 summed assertions (51 distinct names) across eighteen fresh-process selectors per plain and ASan/UBSan mode, plus eight exact safe controls per mode. The 52 processes separate seven actual fixed authored-DSO selectors from eleven model-only selectors; leak detection is enabled. Four immutable module slots retain canonical zero-padded 33-byte IDs and one-way release/acquire publication independently of mutable permission/session/count state. The module-first adapter uses unchanged rc_lookup, resolves generated Community process identity plus full ID to exactly one permanent module index, and lets Core/unresolved identity continue LR/frame fallback. Scoped aliases preserve the exact 962-byte cm_blame_fault body and its volatile once guard; frame cells and persistence providers are modeled. Actual loader cases preserve constructor/root/dependency visibility gaps, consumed-query refusal with later-slot lookup, capability-independent ownership, descriptor/partial-init retention and generated-PC precedence over another module LR. Partial init -7 retains one counted published hook; no patched target, replacement or emitted code is invoked. Model cases establish selected overlap/masking/identity/copy/one-use/frame boundaries only. The held-writer reader completes while locks are held, but publication predates thread creation and join supplies synchronization. Per-slot reads are not a coherent snapshot. Selected standalone codegen shows payload stores before release publication and acquire branches before payload reads; normal direct dependencies include memcmp/memcpy. Initial standalone compilation/disassembly succeeded but the strict oracle refused compiler-injected __stack_chk_fail. A separately reviewed exact method/instruction/relocation exception classifies only the canary-mismatch failure edge, preserves stack protection and adopts the two clean artifacts/commands without replay. A separate review-lock metadata preparation was refused before inputs/commands/runtime; only lock schema was corrected. Both packets remain. Independent source/control review and the first frozen retained-byte audit pass over 163 inputs, 218 commands, 436 streams, 248 build artifacts, 52 reports and 1,373 files. The audit also covers the 240-file initial oracle failure. Root review separately checked the retained five-file preparation refusal. The audit's 210,559 structural assertions are not runtime case counts. The exact auditor stayed unchanged. Previous fixed DSO artifacts and 34 selected command receipts were reused, preserving both earlier DSO failures without compiler/matrix replay. Signal/reentry/volatile-guard safety, canary-failure and transitive libc behavior, native frames/persistence/visibility, arbitrary loader/interposition, external writers, concurrent native invocation/publication, quiescence and recovery remain unproved. Ownership is not fault causation. No production integration, live observer, device action or release occurred; all live gates remain CLOSED.

This successor supplies scoped runtime evidence for the preceding retained plan; earlier checkpoints remain historical. See the [offline module attribution fixture](../experiments/usb-browser-switch/activation-module-attribution-fixture/README.md), [source ledger](../experiments/usb-browser-switch/activation-module-attribution-fixture/source-ledger.json) and [verification](../experiments/usb-browser-switch/activation-module-attribution-fixture/verification.json). Next make a separately scoped source-only fault-reader operating-contract review. Reuse retained actual handler/reporting call sites and consumer/providers to distinguish ordinary and failure entry contexts, same-thread nesting and cross-thread reentry, the unchanged volatile once guard, libc and stack-canary dependencies, native frame-read lifetime/failure constraints, and permanent publication/custody. Capture only genuinely new pinned sources if required. Identify established facts and unproved requirements before selecting an executable successor. Do not install handlers, trigger signals, exercise nonlocal exits or native frame/persistence paths, weaken protections, replay passing matrices, integrate production or attach a live observer. Ownership is not causation; all live gates remain closed.

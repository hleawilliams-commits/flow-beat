# October 22 continuation

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

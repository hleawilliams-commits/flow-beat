# Follow-up investigation — October 17

**Later correction:** [October 22 continuation](CONTINUATION-2026-10-22.md) supersedes stale branch/cleanup assumptions. The [Waveform observer attempt](../experiments/mods-shortcut-delivery/direct-knob-observer/runtime-evidence.md) was refused: the installer validates all four opening instructions and rejects `TBZ` at entry +8. No runtime argument, provenance or handback observations were obtained. Subsequent emulator development must be preserved when removing its residue.

The physical XDJ-AZ was not changed, reloaded, restarted or controlled during this follow-up. Later read-only observations established the MODS filter quarantine described below. MODS Shortcut 0.1.2 was published after the later diagnosis and emulator checks; no physical install was performed. Phase Strip 0.1.3 remains the current phase correction; source and experimental evidence below are shared for review, not installation on the practicing board.

| Work | New evidence | Remaining gate |
|---|---|---|
| [MODS delivery](../experiments/mods-shortcut-delivery/README.md) | Normal emulator callback delivery opens SOURCE. Suppressing delivery to the shortcut for 1250 ms reproduces acceptance followed by expiry before any SOURCE pulse. A separate [direct-menu design](../experiments/mods-shortcut-delivery/DIRECT-MENU-DESIGN.md) maps the core changes required to avoid that dependency. | The earlier cancelled request has no timing trace. The later sustained failure is now confirmed as filter quarantine, below. Direct menu opening is not implemented; knob handback and native-view cancellation still need solutions and tests. Don't weaken native-input guards or publish the gap adapter. |
| [Phrase grid retry](../experiments/phrase-grid-retry/README.md) | Extracted production functions reproduce a grid arriving after the one attempted read. A bounded retry patch passes 112 assertions in plain and ASan/UBSan. Its isolated ARM64 build ran actual Grouped Panels phrase consumers and Drum Roll Beat Sync on all four emulator decks. | Native delayed-grid recovery was not reproduced: emulator loads had grids on their first attempt. Drum Roll checks establish consumer operation, not sample-accurate timing. Physical Deck 2 attribution remains unconfirmed. |
| [Stems activity guard](../experiments/stems-retry-activity/README.md) | The original retry mistakes sampled moving loops or a stale playhead for stillness. The follow-up passes 98 focused assertions per mode and an ARM64 build. All four emulator decks show fresh still observations while paused and motion while playing. | Scheduling only. The subsequent [publication regression](../experiments/stems-retry-publication/) confirms resume during decoding remains unguarded. Observed stillness is not authoritative transport pause; hardware timing cost is unmeasured. The follow-up does not increase memory capacity. |
| [Stems publication regression](../experiments/stems-retry-publication/README.md) | Actual scheduling, offset-release, publication and injection code reproduces recovered stems becoming usable after observed resume on all four fixture decks. The actual render-before-observation prefix also permits injection before the first resumed motion report. 560 publication/ordering checks plus 46 existing identity checks pass per plain, ASan/UBSan and TSan mode; independently rerun. | Reproduction only, no production fix. A final worker motion snapshot cannot guarantee safe activation. [Activation design review](STEMS-RECOVERY-ACTIVATION.md) records ownership, lifetime and deliberate-intent alternatives. Synchronized fixture schedules do not establish general engine race freedom or hardware timing. |
| [Private stems staging draft](../experiments/stems-recovery-staging/README.md) | Retry decoders use private buffers and immutable job identity; offset measurement follows decoder joins, and recovery never publishes availability, frames or envelopes. Actual extracted loader/flusher/pbread tests cover all four fixture decks, resume, replacement, eject and completion/start races. ARM64 compilation passes. | No activation API or emulator install. An early ASan/UBSan run failed one ordinary-restart assertion; later passing diagnostic repetitions did not explain it. Its runtime gate remains closed. A separate ordinary-load TSan finding is reproduced without the staging patch; private-mode TSan serializes ordinary fallback and is not whole-engine coverage. See the retained failure and scope before continuing. |
| [USB observer V2](../experiments/usb-browser-switch/observer-v2/README.md) | 299 host assertions per mode and ARM64 SDK validation pass. The unchanged two-hook observer now [ran in the emulator](../experiments/usb-browser-switch/observer-v2/runtime-evidence/): both claims accepted, eight valid parent snapshots and five entry/result pairs on TID == PID. Native switching retained distinct selected rows on USB1/USB2. All 122 baseline files, both cores and original loaded-mod list were verified restored. | Entry snapshots and scalar return codes still do not verify correlated switch completion. Missing-media, disable/re-enable, different export databases and scroll/sort retention remain untested. No USB buttons or activation operation are implemented. |
| [USB observer V3](../experiments/usb-browser-switch/completion-contract/README.md) | A third ordinary function-entry hook copies the committed identity from its live native argument, correlated to a stack-local request scope. All three claims were accepted in the emulator; return to USB1 produced one matching `COMMITTED_AT_ENTRY` observation, followed by a scalar return. Two earlier requests produced no boundary and remain UNKNOWN. All 124 baseline files, modes, inventory, both cores and original loaded list were restored; the probe and opt-in were removed. | This is a boundary observation, not final completion or browser readiness. No post-return native reads, activation call or USB buttons. Missing-boundary paths, failure/cancellation lifecycle and a native activation contract remain open. |

## Later physical MODS failure: confirmed quarantine

[Read-only physical evidence](evidence/mods-filter-quarantine-2026-10-17/README.md) found MODS Shortcut 0.1.1's exact callback registered and disabled with `off=1`, `strikes=8`. The core and mod were otherwise active, the setting ON and the button visible. The sole pending request had survived more than 30 minutes. Once the watchdog disables dispatch, 0.1.1 cannot clear that request and ignores further taps.

This establishes the later sustained cause. It does not identify which operation consumed each timing budget, nor explain the earlier short-lived cancellation. Removing the unconditional idle clock read is a mitigation under test, not proof of hardware timing reliability. The 0.1.2 community update uses token/CAS ownership for UI timeout. [Host and emulator checks](evidence/mods-shortcut-0.1.2/README.md) now pass, including repeated no-dispatch timeout/acceptance and three SOURCE requests with four decks playing. Physical validation remains pending; it does not reactivate an already quarantined handler or guarantee prevention.

The separate [forced-log experiment](../experiments/frame-log-force/README.md) reproduces why the watchdog message can disappear with normal logging off. Its source-only core patch preserves the force bit through the existing queue; 48 checks pass in plain and ASan/UBSan. It changes no timing protection and is not part of the community mod.

The missing MODS button itself was previously explained by its **MODS source shortcut** toggle being OFF. Whole-mod enable is separate. Turning that row ON requires no Reload; that finding does not explain the earlier accepted but cancelled tap.

The physical stems incident is still documented [separately](evidence/physical-stems-2026-10-17/README.md). The later long track's 590.24 MiB decoded requirement exceeded the 114.53 MiB headroom in its snapshot. Recovery logic can address a refusal that stays cached after memory returns; it cannot make an oversized combination fit.

The emulator's original UI/stems core hashes and preview community set were restored after the temporary tests. Restarts unloaded the emulator tracks. No experimental core binaries, private music, raw physical captures, credentials or USB backups are included in this handoff.

## Further source review and controlled diagnostics

The [ordinary producer candidate](../experiments/stems-ordinary-publication/) is a separate patch directly against pinned upstream. It gives decoded prefixes release/acquire ordering, keeps progressive envelope mapping under its mutex, and reconciles the public prefix after decoder joins while preserving hold/stale guards. It is not a recovery activation feature or a diagnosis of the historical ASan assertion. Its acquired-prefix worker scope excludes plain public-frame readers, the flusher’s direct correlation path and RT buffer reclamation. The final suite passes 336 assertions in each plain, ASan/UBSan and TSan mode, independently repeated with identical production extraction. The exact ARM64 build also completed one ordinary all-four emulator load, with progressive/final buffers and brief play/pause under the same PID. Its [runtime evidence](../experiments/stems-ordinary-publication/runtime-evidence/README.md) records corrected harness expectations, non-atomic sampling and restoration. No memory recovery, private staging or physical audio behavior was tested.

The [focused staging diagnostic](../experiments/stems-recovery-staging/diagnostics/) records actual per-thread progress operations, original assertion state and independent worker joins. It passed 1,000 restart cases / 15,000 assertions under ASan/UBSan. A deliberately empty ordinary codec then failed the unchanged assertion as expected, with all ten workers joined and zero decoded progress; its complete failure trace is retained. This establishes useful failure discrimination, not the cause of the earlier lost-state failure. The frozen staging patch and its closed emulator gate are unchanged.

The [USB V4 observer](../experiments/usb-browser-switch/missing-boundary-investigation/) adds the exact empty-list request entry. Its host suite passes 1,122 assertions per plain/ASan/UBSan/TSan mode, with ARM64 SDK build and validation. Static review proves that an inner request failure can still lead to outer return code zero, and that a newer media event can replace an active one. The bounded emulator sequence now explains V3’s missing boundaries: the first USB1 and USB2 selections take the empty-list request path, while the return to USB1 reaches the existing copied commit boundary. Native admission and final readiness remain separate; the supplied category byte is recorded but its expected value is not independently known. Restoration and exact runtime records are recorded in the experiment’s evidence.

The [direct-menu knob boundary review](../experiments/mods-shortcut-delivery/direct-knob-boundary/) identifies the waveform view’s corresponding native virtual-handler slot and verifies its exact current-firmware mapping. It consumes four integer arguments, so copying the existing three-argument Browse wrapper would lose an argument. The later [observer](../experiments/mods-shortcut-delivery/direct-knob-observer/) passed host/ABI checks but its function-entry hook was refused; no input change or native observation followed. Knob provenance, counter ordering, return semantics and queued-event ownership still need observation before menu handback can be implemented.

## Private staging composition and USB intent policy

The [new incremental composition](../experiments/stems-staging-publication-composition/) applies after memory retry, activity guarding and the unchanged frozen staging patch. It adapts the ordinary producer fix with a recovery check at finalization and an independent private-routing veto under the publication lock. The actual-source suites pass 336 ordinary and 1,020 staging assertions in plain, ASan/UBSan and TSan; root independently reproduced identical manifests/results. Ordinary producers remain concurrent in TSan. Tests retain the original restart assertion, challenge direct helper bypass and mode transition at the lock, retain token/generation checks, and verify a previously exported envelope remains unchanged through private preparation. Three deliberately broken guards produce only their expected failures. The exact four-patch ARM64 build passes. No emulator installation or activation API follows: the unexplained historical failure and reader-lifetime/activation gates remain open.

The [USB activation-contract prototype](../experiments/usb-browser-switch/activation-contract/) adds a pointer-free request policy, with expiry, missing/ambiguous-media results, token cancellation and a documented dispatch linearization point. Its native adapter remains absent. Static review identifies a fresh SourceSelectView timer-listener entry routed through JUCE, but the caller reads the embedded timer after returning: navigation must prove view/timer retention. Source-row selection also only focuses Local Library; the final library argument/dispatch contract remains unproven. These are specific implementation gates, not another success-path observer or working USB buttons.

## October 22 source follow-up

The [encoder dispatcher review](../experiments/mods-shortcut-delivery/dispatcher-boundary/) verifies different native entries that pass the unchanged installer policy. Its 66 policy/overlap assertions and 59 instruction anchors were independently reproduced. The preferred passive entry `0x1adeee0` takes five integer-register inputs and may enqueue; it is not the refused four-argument Waveform target. A subsequent [passive observer](../experiments/mods-shortcut-delivery/dispatcher-observer/) passes 16,686 host assertions per sanitizer mode, independently repeated, and 55 ARM64 ABI assertions. Its exclusive emulator claim succeeded, but the bounded empty-Waveform knob sequence produced no entries. Later observer-only status showed a progressing heartbeat and empty capture queues; that is not proof of gating at the earlier input instant or native delivery. The 58-anchor thread-predicate review separately shows native owner-thread comparison rather than Linux PID/TID equality. Trace the actual panel-input route before probing again; main-thread provenance, counter ordering, arguments/returns and event ownership remain open.

The [negative-result source audit](../experiments/mods-shortcut-delivery/dispatcher-observer/source-audit-2026-10-22.md) found no static explanation for the absent captures, while retaining the silent-gate uncertainty and the missing standalone status-sampler provenance. The [new input-stage fixture](../experiments/mods-shortcut-delivery/input-stage/) passes 60 assertions per sanitizer mode with an independent identical result and a strict copy-reset control. It demonstrates why RAW input records and normal filter records have different limits: core anchoring, earlier EATEN and later editing can separate them from native delivery. The [native input-route review](../experiments/mods-shortcut-delivery/input-route/) then identifies a separate view dispatcher at `0x1b01cf8` whose task can call handlers directly, bypassing the earlier generic entries. Its 88 instruction anchors and 22 actual policy assertions supported the separately bound route observer below; live recipient/child identity, ownership and completion remain unknown.

The separate [menu watchdog-order patch](../experiments/menu-watchdog-order/) reproduces knob/button disagreement at overlay-heartbeat stall and recovery. Refreshing the existing count ahead of both paths passes 978 assertions per plain, ASan/UBSan and TSan mode; unchanged source fails exactly 21 transition observations. Independent rebuilding reproduced identical results, and a full isolated ARM64 build passes. It preserves the threshold and input gates. It has never been installed, changes no community-filter timing watchdog, and supplies no native handback or concurrent ownership proof.

The [route observer](../experiments/mods-shortcut-delivery/route-observer/) now passes 31,427 actual-source assertions per sanitizer mode, independent binding checks and ARM64 validation. One emulator claim at `0x1b01cf8` succeeded. After a host-only counter alignment from stale 133 to the observed RAW baseline 128, a distinct four-detent empty-Waveform sequence produced four native attempts, one off-thread scalar sample (`w3=1`, `w4=1`, original LR `0x22c6e08`), three intentional one-shot skips and nine RAW records. Its retained ELF/maps sampler checked 26 own-module scalar values; all samples are noncoherent. This positive entry does not establish the active child, all counter ordering, returns, task ownership or completion. [Restoration evidence](../experiments/mods-shortcut-delivery/route-observer/runtime-evidence.md) verifies all 147 fresh baseline files, settings/loaded set, both cores, caches and the exact original host counter restored; probe/opt-in absent, clean empty restart and natural guard expiry passed. Emulator released, no physical access or release. The [caller review](../experiments/mods-shortcut-delivery/route-observer/caller-review/) verifies a static tail-call chain consistent with the saved LR, without proving the live class. The separate bounded follow-up below addresses that deliberate sampling gap; preserve this earlier result unchanged.

The subsequent [bounded route observer](../experiments/mods-shortcut-delivery/route-observer-burst/) replaces the off-thread one-shot with 16 immutable reservations and leaves the earlier source/evidence unchanged. Host tests pass 32,339 assertions and six strict controls per sanitizer mode, independently reproduced; 47 binding checks/30 rejection controls and ARM64 validation pass. One fresh emulator run captured four native records for two clockwise and two counterclockwise detents: `w3` signed-32 interpretations +1,+1,−1,−1 and `w4` 1,2,1,0. RAW byte 34 was 129,130,129,128; equal argument bits and LR `0x22c6e08` do not prove object identity, final counter delivery or event ownership. Five noncoherent own-module samples validate 135 individual scalar reads. All 147 fresh baseline files, current settings/loaded set, cores, caches, absences and original host-counter bytes/mode were restored, with clean empty restart and natural guard expiry. Emulator released; no release or physical access. Next audit native admission/execution and event lifetime rather than repeating the successful four-record capture. Native returns, children, cancellation and handback remain open.

The [USB activation ownership review](../experiments/usb-browser-switch/activation-ownership/) now traces the final argument to a 12-byte `Source` passed by value to `BrowseSourceSelect::setSource` at `0x20a1550`, after guarded DeviceLibrary checks. Its verifier checks 87 anchors, exact unwind boundaries and 20 extracted-policy assertions. This resolves the static argument shape; a live receiver and MediaID correlation remain unobserved. Both timer and source tasks retain raw owner pointers in their captures, and callbacks may be followed by more native owner reads. A value copy or task count is not a proven navigation lifetime contract. No native adapter or USB buttons were added; the timer-vslot route is ineligible under current SDK policy.

See [the continuation note](CONTINUATION-2026-10-22.md) for these next steps and the completed selective cleanup. The later passive-observer run used its own fresh backup and selective removal. Its initial cleanup mode refusal, narrow FAT mode correction and final verification of all 146 baseline files are retained in [runtime evidence](../experiments/mods-shortcut-delivery/dispatcher-observer/runtime-evidence.md). Existing mods, settings, both original cores, caches and protection flags were preserved; the emulator is released after natural guard expiry. No physical access occurred.

## Next development pass

The separate [task capture contract](../experiments/mods-shortcut-delivery/task-capture-contract/) now passes 67 assertions per sanitizer mode with independent reproduction and exact return-truncation/dispatcher-overread controls. It borrows valid task storage only at entry, copies 24 scalar payload bytes plus a separate vptr check, and records full return bits from private values after the synthetic allocation is unmapped. Caller-provided identity scope, duplicate/wrap/missing-boundary ambiguity and unpublished context lifetime are explicit. This serial fixture supplies no native retention or concurrency proof.

A separate [ARM64 call/return fixture](../experiments/mods-shortcut-delivery/task-call-return-abi/) passes 319 actual-assembly assertions, five executed negative controls and two compiled CFI rejection controls. It exercises helper reentry, native nesting, four threads and exception unwinding through the wrapper, without calling firmware. The callee sees a new wrapper LR and branch scratch; no arbitrary stack-argument, trampoline or unload guarantee follows. Its frozen assembly is reused by the new integration below; the old suite was not repeated unchanged. Neither artifact was installed; no native submission return, executor invocation, accepted input or cancellation was observed.

The new [value transport](../experiments/mods-shortcut-delivery/task-value-transport/) assigns bounded observer invocation IDs and publishes immutable entry/normal-return records independently. All 220 assertions across nine scenarios pass in plain, ASan/UBSan and TSan, with four exact controls and an independent identical result. Concurrent readers acquire payloads before producer completion; close permits already admitted work and returns, never resets storage, and is not quiescence. Native TaskNo duplicates, zero, wrap-like values and missing records remain ambiguous.

The [synthetic ARM64 integration](../experiments/mods-shortcut-delivery/task-observer-integration/) connects actual capture/transport headers, the unchanged submission wrapper and a one-input executor adapter. It passes 239 assertions, four strict controls and two CFI rejection controls, including execution-entry publication before submission returns, real synthetic-task unmapping before after, rejected-capture forwarding, reentry and one exception through three submission frames. Independent review caught a shared plain destructor counter in the test fixture; it is now atomic and earlier passing integration runs are superseded. Exact synthetic LR labels are explicitly mapped by test helpers: this does not establish firmware caller provenance. No sanitizer instrumentation is used in this ARM64 suite.

The [hook installation/lifetime audit](../experiments/mods-shortcut-delivery/task-hook-lifetime/) executes extracted pinned installer source on owned host buffers: 56 assertions in plain and ASan/UBSan and two strict controls independently reproduce. The original slot is stored before redirection and must remain valid even if another claim fails. Libraries/hooks remain mapped until process restart; capture close neither unhooks nor drains. The original-pointer store is relaxed, distinct from the fixture's release/acquire setup, and instruction-cache/cross-core correctness is outside these serial tests. Native-body exception metadata and asynchronous PCs inside anonymous trampolines are separate cases. The separate SDK adapter below now implements and tests those startup/lifetime requirements; owned runtime status sampling and a fresh selective restoration plan remain prerequisites for a new passive emulator run. Do not retry blocked slots, interpret missing records as cancellation, or add navigation/input consumption. Preserve the released emulator state and every existing gate.

The [passive SDK task adapter](../experiments/mods-shortcut-delivery/task-sdk-observer/) now passes **660 actual-source assertions across 41 scenarios in each plain, ASan/UBSan and TSan mode**, with independent byte-identical results and five strict controls (exact 1/2/2/1/1 failures per mode). It tests worker-before-init-return schedules, first/second claim failure, permanent original retention, fatal reader shutdown, independent entry/late-return copying, suppressed logging, unpublished holes, reentry, concurrent publication and synthetic task unmapping. Host tests use mock SDK/native services and direct helpers; they do not execute the wrappers or firmware. The unchanged wrappers/transport retain their prior evidence by exact identity.

Exact binding passes 96 checks and 52 rejection controls, independently identical, including 44 instruction anchors, five data words and two RTTI names. The new ARM64 module builds and passes SDK validation; binary hash `71d7fc088f4def67d1307e4faeef301020aed04bfa3da7ae4bc6a422ece3e255` remains private and uninstalled. Capture stays closed until both claims succeed, and a partial install keeps permanent targets. Tick updates only its own heartbeat; the worker closes observation after bounded iterations and keeps scanning late returns. Its void log service makes one attempt per copied record, not a delivery guarantee. A stalled heartbeat is not quarantine, and a worker exit is not producer quiescence.

No new native submission return, executor invocation, event identity or completion/cancellation observation exists. The later [preparation checkpoint](../experiments/mods-shortcut-delivery/task-sdk-observer/PREPARATION-EVIDENCE.md) now supplies the exact-binary scalar planner (81 checks/54 controls), driver identity/frame policy (99 checks/51 controls), fresh runtime plan and private startup/sampling mock review. Both public suites independently match. A missing-`stat` preflight failed before acquisition; the changed tar collector passes 1,440 mock checks/82 scenarios and acquired one valid 128-byte emulator MISO frame. A separate saved-receipt audit passes 36 checks. This was read-only: no observer installation, input or service/core change. Next finish and test fresh action-bound counter/progress acquisition and one-step host-counter ownership/restoration before installation. The sample is not a later input baseline. Existing ownership/policy/ABI and physical-timing limits remain; no direct menu, USB buttons, recovery activation or release was added.

The later October 22 [native task audit](../experiments/mods-shortcut-delivery/route-task-ownership/BOUNDARIES.md) distinguishes queued submission/TaskNo from execution and cleanup, without observing them live. Its dispatcher guard is a shared sentinel, not an event cancellation token, and task storage ownership does not establish retained dispatcher lifetime. Static checks and unchanged prologue policy pass for three possible passive entries; no new hook or runtime run occurred. The [task-box note](../experiments/mods-shortcut-delivery/route-task-ownership/TASKBOX.md) keeps conditional cancellation, optional notification and post-return lifetime limits explicit.

Separately, the [0.1.3 lane counterexample](../experiments/mods-shortcut-lane-observation/) passes 82 actual-source assertions per sanitizer mode, independently repeated. Delayed delivery between one frame's callbacks can trigger three false core-off diagnostics and a lane move with SDK `off=0`, zero strikes and all registrations loaded. An authoritative-quarantine hypothesis fails exactly once per mode. This corrects the diagnosis/lifetime claims in the unpublished candidate's documentation; it is not a physical reproduction or explanation of the earlier tap cancellation. No package, catalog, core or emulator state changed. Resolve honest delivery feedback and safe lane-handover semantics before promotion; preserve the watchdog and prior archives.

1. Preserve the private-staging draft’s unresolved ordinary-restart outcome and its closed installation gate. The focused diagnostic now distinguishes actual worker joins and codec-empty results; use it to capture a recurrence when new evidence warrants another schedule, rather than repeating unchanged passing loops. Review the separate ordinary producer candidate and its runtime evidence independently. The [separate composition](../experiments/stems-staging-publication-composition/) now supplies explicit private-publication guards and actual-source interaction tests, with a full ARM64 build. It is not a gate reopening or a diagnosis of the earlier failure; no composed runtime install occurred. The baseline ordinary TSan finding and relaxed progress/publication hypothesis are separate evidence. Once the runtime gate is resolved, test private staging and cancellation in the emulator, then resolve activation using [the design review](STEMS-RECOVERY-ACTIVATION.md). No authoritative pause signal or selected deliberate-token action exists. Keep the reserve and official core unchanged; neither scheduling-only recovery nor private preparation is a finished stems fix.
2. Build on V4’s observed empty-list request path and V3’s copied commit boundary. The first-two missing boundaries are now explained for the bounded sequence. Neither admission nor committed-at-entry identity is final readiness. Resolve asynchronous request replacement/cancellation and owned native activation, or specify a narrower intent-only contract with honest UI semantics. Keep post-return pointer reads absent. USB buttons remain planned for the old WAVEFORM heading; missing-media and lifecycle checks are still open. Do not repeat successful claim/thread checks without a change that warrants it.
3. Implement the direct-menu service only after resolving the design's knob handback and view-cancellation questions. Treat the SDK/core change separately from the community shortcut, and retain all native input regression coverage.
4. Preserve the isolated phrase-grid results. When composing a candidate engine, apply patches explicitly and test their interaction. The physical Deck 2 report still needs evidence; no physical reload is required for this development work.

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


## October 27 — owned lifecycle composition remains inactive

The next [separate emulator patch](../experiments/emulator-owned-exit/) addresses
local ownership through exit and returning startup errors. It holds the matching
port/worker and optional guest value outside AppCreator, and returns a rejected
pair intact. The owner transitions Running → Joining → Retained around an actual
thread join without holding its own mutex during stop/backend work. Concurrent
completion requests report Joining, then reuse retained diagnostics; a worker
cannot consume its own join handle. The owned App branch returns before explicit
guest Quit/stop, and a returning runner fallback also handles an App never made
or dropped without on_exit. This does not permit final resource destruction.

Review identified and corrected a draft path that could discard a completed
session on post-join owner-lock poison before any candidate run. Completion now
stores the typed stop/session/error without clearing poison; read-only diagnostics
expose that flag. Poison
before the worker is taken still refuses completion and may leave the worker
active; that exceptional state is preserved in a focused test. A diagnostic
NotRecorded stop value is not a claim that stop has never been called while a
join is in progress. Main keeps the owned pair absent, with no factory or flag.

The first frozen run compiles production with 32 visible unused-code warnings,
passes 12 focused actual-crate tests, and compiles all seven mutants which each
fail only their reviewed test/source assertion with exit 101. There are 138
listed tests; the 126 inherited cases remain filtered. Controls distinguish
pairing loss, omitted fallback/App finish, illegal owned Quit, lost post-join
session under poison, erased stop error and premature synthetic guest release.
Cleanup retains real joins before the control assertions. Independent run and
comparison/review are preserved in the experiment, separately from raw logs.

Candidate patch SHA-256:
`b278c09b4db47e341ebc5542386dfa79d185586c905b8c4904c430f2aef51a3b`.
Primary result SHA-256:
`e9e1fafe9ac95cc677e9e582db49ab19c0690e87b5ec1268a1379d1eb23ee9d5`.
All 18 candidate files are pinned; only App/main/transport plus the new owner
differ from the frozen parent. Rust/Cargo 1.98.1 ran on macOS ARM64; no sanitizer,
Linux ARM64, Windows, native framework event-loop or audio test is claimed.

A cached-source audit pins eframe 0.29.1/winit 0.30.13. Native initialization can
fail before AppCreator is called; on_exit cannot veto closure; macOS Cmd-Q can
terminate without returning the runner. Holding a Unix Child keeps its stdin
pipe locally alive, but final Drop/process death can still trigger supervisor
shutdown. Windows retains only a PID, not the leaked process-scoped job handle;
real owned construction is unavailable. The candidate does not alter these
policies, leak resources to conceal the gap, or supply live shutdown recovery.
No real guest, settings file, kernel, input route, physical deck or emulator
endpoint was contacted, and no publication/staging/runtime gate reopened.

# Generated-range ownership contract — source review only

This separate review follows the [offline attribution fixture](../activation-attribution-contract/README.md) at development commit c95d8f3d0db3ce8155395a09918f09dc58a8b00a. It specifies a possible core extension before implementation. **There is no generated-range registry, repair patch, new executable fixture or live adapter here.** All live, activation and staging gates remain CLOSED.

The proposed extension associates exact anonymous instruction intervals with the validated claiming owner before publishing the original pointer or redirect. It preserves the existing PC → LR → frame order, module-envelope precedence, native fallback and quarantine. Ownership describes an association, not fault causation or successful persistence.

## Evidence boundary

[Source receipts](result.json) and the [review/audit record](verification.json) reuse the eight SDK byte inputs retained by [the unwind-policy review](../activation-unwind-policy/README.md), without repeating their Git capture. Five additional pinned files cover the selected initialization, SDK dispatch, UI Reload and loader apply paths. All thirteen SDK files are at 3e0c5cb010454228f4ee63226b60d060494eeaae. Fifteen retained inputs include the collector and prior receipt; five new Git reads have ten output streams. No compiler, test matrix, native firmware, signal, emulator or deck operation occurred.

The source references below use that exact revision:
[inline installer](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/inline_hooks.c),
[protection helper](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/common/azprot.h),
[community loader](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/community.c),
[overlay](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/mod_menu_overlay.c),
[SDK process services](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/sdk_proc.c),
[community screen](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/screen_community.c),
[community update helper](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/deck/stick/deck/lib/az-community),
[USB loader](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/deck/payload/usr/sbin/az-usbload).

This is a bounded source review, not a complete program call graph or proof of every destructive path. The earlier exact-source attribution matrix, frozen observer/binder/transfer sources and all previous captures remain unchanged. Raw source copies, command receipts and audit materials remain private.

## Actual emission extents

azm_abs_jump (inline_hooks.c:93–96) emits two instructions followed by an eight-byte address literal. The island allocator (122–147) hands out sixteen-byte slots in four 64-KiB pools. The trampoline copy loop (298–313) copies one native instruction for an island or four for constructor fallback. An ADRP can stay one relocated instruction or expand into four MOVZ/MOVK instructions.

Let n be the number of copied native instructions, m the number of expanded ADRPs, and r = n + 3m the relocated instruction count. These are the exact source-derived intervals:

| Region | Instruction interval | Literal interval | Allocation extent |
| --- | --- | --- | --- |
| Selected island slot | [I, I+8) | [I+8, I+16) | Shared 64-KiB pool |
| Trampoline | [T, T+4(r+2)) | [T+4(r+2), T+4(r+4)) | Private 4096-byte mapping |

Island-path trampoline instruction lengths are 12 or 24 bytes. Constructor fallback permits 24, 36, 48, 60 or 72 bytes. These describe emitted instruction bytes, not proof that every instruction executes. Range membership is byte-wise and half-open; no low-bit normalization is proposed.

Attribute only the instruction intervals. Exclude literal bytes, page slack, unused or failed island slots and adjacent slots belonging to another emission. The copied native instruction is associated with the hook owner while executing in its trampoline, without assigning fault causation. Native patch-site bytes are not added to this anonymous-range registry. The direct vtable patch helper at 366–379 emits no anonymous code; SDK vslot admission/ownership is outside this packet.

## Owner custody and selected lifecycle

community.c keeps static g_cm[CM_MAX] records with inline id[33] storage (80–119). Its selected initialization appends records and copies validated IDs (1885–1913). cm_load_one retains each successful dlopen handle permanently (1804–1817), computes its module envelope before invoking the entry/init (1818–1851), and does not close it on validation or init failure. On failed init, it clears only the m/screen callbacks and marks failure (1853–1857). Existing installed hooks, code custody, index, ID and range survive; g_cm_live increments only on init success (1859–1861). Therefore a generated owner cannot require state LOADED or membership in g_cm_live.

cm_claim_hook checks identity, caller envelope, the current init window/loading index, declared hook capability and native-address range (1226–1247). These checks do not identify the calling thread. They precede g_cm_mu acquisition; init-window flags are cleared outside that mutex. A source-level caller on another thread during the init window is not excluded by those checks. This is not an observed concurrent claim or an authorization bypass claim.

The claim body serializes its existing target-overlap check and install under g_cm_mu (1251–1262), with g_hook_owner temporarily set to c->id. The installer itself has no universal whole-install mutex. That ambient plain global is diagnostic context, not an immutable authority token. Direct core installation and community installation need one compatible writer protocol in a successor. Do not assume that the protection mutex, which covers individual unlock/relock intervals, serializes allocation, ownership or the entire install.

The selected overlay calls community_init from ov_load_state (mod_menu_overlay.c:754–758; invocation at 2517). community_init has no internal one-shot guard; the bounded source search found no removal/compaction/ID rewrite of g_cm records, but its current one-start call graph is a premise, not enforcement against arbitrary reinvocation. Its Reload worker normally gates an EP147.service restart request on a zero daemon-reload exit status. However, a non-EINTR waitpid failure is normalized to st=0 and also permits the restart path (544–564); that branch does not prove daemon-reload succeeded. This is not an in-process DSO unload/reuse operation. SDK Reload first checks its caller and loaded-track state and sets a pending flag (sdk_proc.c:602–611), consumed by the overlay at 2476–2478. UI remove/disable requests (screen_community.c:309–318,1845–1859) become on-stick package/config/approval changes in az-community:766–791, not an in-process unpatch. Its apply operation at 880–885 is consumed by az-usbload:483–495; apply_restart at 1096–1127 requests a service restart at 1126. A failed request does not end the current owner lifetime. These are source paths, not evidence that a restart occurred or that actual supervisor/QEMU topology is resolved. No restart or loader script was executed.

A future core-only owner token must contain a validated owner kind, process-start identity and bounded copied community ID plus length. A stable index may be retained diagnostically, but the signal reader must not fetch the ID later through g_cm or g_hook_owner. The token must be issued by the trusted admission boundary, not accepted from community-supplied strings without revalidation. Init failure after a successful claim must not invalidate it. Owner/code custody must include callbacks and threads a DSO may have started before failing; normal callback disable is not reclamation.

## Every inline-installer outcome

The following covers the source branches of azm_install_hook_at, not arbitrary termination, allocator internals or all core patch mechanisms. Existing cleanup calls do not check munmap results, so the source proves cleanup attempts rather than successful release.

| Existing branch | Current code behavior | Proposed ownership rule |
| --- | --- | --- |
| Null/misaligned target or rejected four-instruction scan (275–279) | Refuses before island/trampoline work | No visible generated record |
| Island search maps an out-of-reach pool (122–147) | Attempts to unmap it; selected slot increments used immediately | No whole-pool ownership; failed attempts never publish |
| No island during late phase (282–285) | Refuses without trampoline | Keep refusal unchanged |
| No island; four-instruction cover rejected (287–290) | Refuses constructor fallback | Keep refusal unchanged |
| Trampoline mmap fails (292–294) | Refuses; an already handed-out island slot stays consumed | Discard invisible reservation |
| Trampoline RX seal fails (180–185,315–317) | Attempts trampoline unmap and refuses | No visible range survives cleanup |
| Island unlock fails (321–323) | Attempts trampoline unmap and refuses | Reservation remains invisible |
| Island relock fails (325–328) | Logs a warning and continues | Distinguish this from refusal; do not claim RX restoration |
| Target unlock fails after island bytes exist (330–332) | Attempts trampoline unmap and refuses | No visible trampoline or island ownership |
| Fallback target unlock fails (346–348) | Attempts trampoline unmap and refuses | No visible ownership |
| Target unlock succeeds, original/redirect published (335–336 or 352–354) | Original precedes branch/jump; flush/relock follow | Metadata must precede BOTH publications |
| Target relock fails after publication (338–339 or 356–357) | Logs a warning and still reports success | Retain code, original, owner and records permanently |
| Successful install bookkeeping (340–342 or 358–360) | Notes target and attempt after publication | Post-return discovery is too late for first reachability |

No registry cleanup may turn an already visible record back into an unpublished slot. The current island used count is not rolled back when later work fails. A successor must not quietly recycle those slots or infer that attempted munmap succeeded.

## Proposed writer and reader transaction

This is a contract for a new offline implementation, not behavior added to the pinned core.

1. **Reserve, invisible.** Before any externally reachable mutation, obtain an explicit validated owner token and reserve generated-record capacity plus inline target-ownership bookkeeping capacity. Preserve the current sixteen-byte target exclusion and prologue/late-hook refusals. Use checked integer arithmetic for every endpoint. Serialize all participating writers, including core and community callers, with a documented lock order; revalidate owner/admission and relevant known module envelopes within that transaction. Never invoke an installer from a signal reader.
2. **Prepare, invisible.** Allocate and emit exactly as the retained installer permits. Compute the actual instruction intervals after relocation; validate them before target unlock. Fully initialize one record containing both intervals, copied owner kind/ID/length and diagnostic target/emission identity. Reserved records participate in writer overlap checks, but readers must ignore their payload.
3. **Publish, irreversible.** After successful target unlock (330 or 346), before the original-slot store (335 or 352), publish the record using one bounded release operation. Everything that may refuse, allocate, inspect arbitrary pointers, call a provider or acquire a further lock must already be complete. The metadata publication cannot fail. For community claims, the existing claim and protection locks are held here. Direct core callers currently have the protection lock at this point; a successor must apply its documented writer protocol to both. No arbitrary registration callback belongs here.
4. **Redirect, retain.** Publish the permanent original and target branch/jump using the selected route, then complete existing cache/protection handling. A signal between metadata publication and redirect may see an owned but not-yet-reachable emission. Ownership does not promise that redirection completed. Any later abort, close, init failure or warning retains the visible record, copied identity and code for process lifetime. No unpatch/reset/retry/unmap path is introduced.

For an invisible failed reservation, a writer may reclaim the metadata slot only while keeping its publication state uncommitted. Readers skip its payload entirely. Once published, the state and payload never revert, change owner or reuse an address during this process. This avoids stale attributable addresses after prepublication cleanup while avoiding a wait for readers.

A candidate representation is a fixed array of one-way committed slots, one slot per inline emission with up to two instruction ranges. Start with a bounded design no larger than the existing 512-entry inline ownership ceiling; the exact byte layout and selected capacity must be explicit in the next implementation. Reserve both resources before publication. The current g_hooked array silently drops additions at 512 (100–155), after redirection; the 256-entry diagnostic table separately counts lost attempts (205–224). Neither is a sufficient admission receipt. Four pools contain at most 16,384 island slots, including failed attempts; that is not the registry capacity.

Reader requirements:

- Fixed bounded traversal; one acquire of each slot's committed flag before reading its immutable payload. Do not publish a count early and treat it as initialized-prefix evidence.
- No allocation, mutex, lazy initialization, callbacks, waiting, spinning or retrying until a writer progresses. An interrupted writer and nested read-only lookup must not prevent completion.
- Correct alignment and always-lock-free publication on the exact target/toolchain, with generated-code inspection excluding out-of-line atomic helpers. This is a requirement to demonstrate, not a signal-safety result from this review.
- No writes to registry state, borrowed IDs or mutable owner dereferences. Returned identity storage lasts for process lifetime.
- Establish ordering from full payload initialization to metadata visibility to original/redirect visibility on the actual architecture. An acquire/release sketch and prior ordinary-thread tests do not prove all instruction-cache or asynchronous-signal observations.

This proposal does not fix or certify the legacy volatile written guard, mutable module-envelope reads, crash-handler reentrancy, file calls, handler chaining, asynchronous unwind or recovery. They remain explicit inherited limits. A synthetic fixture may validate the new registry's bounded behavior without claiming those broader properties.

## Lookup precedence and overlap policy

Use typed results: NoMatch, Core and Community. For each address in the existing PC → LR → up-to-sixteen-frame sequence, first retain the exact existing first-index module-envelope result. Only a legacy miss may consult the generated instruction records. Core ownership is never converted into a fake community index or an AZ-MODS blame ID; it continues the existing LR/frame search. A generated Community result ends selection at that address. Keep the original frame guards, read truncation and once-only persistence behavior unchanged.

This intentionally refines one prior result: a generated community-A PC followed by a module-B LR selects A under the proposed contract, where the old lookup would have selected B. PC-first ordering is preserved, not every historical winner. Existing module ownership at the same address always wins, including envelope holes and non-executable PT_LOAD coverage. No second frame walk is proposed.

Reject invalid/overflowing/empty or overlapping generated instruction intervals, including conflicts with another invisible reservation; adjacent half-open intervals remain distinct. Also reject a new interval overlapping a currently known community module envelope under the writer's admission snapshot. This is a conservative refusal, not permission to shrink an envelope or weaken a claim guard.

A later dlopen can introduce an envelope that masks a previously published generated record. This review chooses the smaller **module-first masking policy**. It does not add reverse loader admission, clear an existing module range, unmap a retained DSO or promise permanent non-overlap. dlopen constructors already execute before cm_range_of populates that range; refusing a later module cannot honestly mean its code never ran. Any future invariant forbidding such later overlaps needs a separately reviewed loader transaction and retained/refused-DSO accounting. Until then, generated attribution remains conditional on no module winner at lookup.

Concurrent legacy module mutation is not made signal-safe by this policy. A future live integration still needs an actual writer/publication contract covering its lookup inputs; this review supplies no general race proof.

## Next independent offline fixture

Implement a separately versioned bounded metadata/lookup fixture first, with synthetic owned records and an explicit modeled installer boundary. Do not modify or replay the frozen attribution, binding or transfer matrices.

New discriminating cases should cover exact instruction/literal/page/neighbor boundaries for both ncopy routes and every ADRP expansion count; endpoint overflow, adjacency, duplicate/conflicting reservations and capacity exhaustion; copied owner identity after mutation of its source string; failed-init owner retention; and distinct Core/Community results.

Exercise a reader at each invisible/visible transition, nested read-only lookup and a reader completing while its writer is held before publication. Model every prepublication refusal/cleanup outcome above, metadata-before-original-before-redirect ordering, and permanent retention after visibility. Include generated-PC versus module-LR, module-first same-address and later-envelope masking, Core-PC fallback to a community LR/frame, and the existing sixteen-frame/read-failure limits only where the new composition changes them.

Safe negative controls must fail named ownership/ordering oracles, not rely on crashing, weakening assertions or expecting a sanitizer fault. Keep deterministic transition models separate from real-thread schedules and any future signal experiment. State exact compiler/architecture/atomic-codegen evidence and sanitizer limits. No actual installer or native adapter is authorized by passing such a fixture.

Passive attachment still needs exact live image/entry provenance, current SDK admission/hook ownership, stable code/originals, accepted control-flow scope and unresolved writer/topology/recovery safeguards. Local Library row/provider/readiness, actual arguments, operation-scoped ownership, alternate destructive/queued paths and caller post-callback timer reads remain activation gates. Preserve opaque action x2, register-only service Source, x16 tail-transfer and installer x17 limits; no native pointee or post-return reads, return interception, TLS pending state or cross-entry pairing. This review changes no firmware/core/package/catalog or physical installation and is not a stems fix.

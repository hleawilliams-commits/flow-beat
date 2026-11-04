# USB anonymous-transfer unwind and fault-attribution policy

This source-only review follows the [authored ARM64 transfer fixture](../activation-transfer-fixture/README.md) at development commit 11703ad30f00d28beeae3f1849fe2b2514d88272. It changes no wrapper, installer, core policy or package and performs no compiler, test, firmware, emulator or device operation. The successful matrices remain frozen.

Missing anonymous island/trampoline FDEs do not, by themselves, impose a general unwinder requirement on register-only passive capture. Ordinary body exceptions after the tail transfer and a signal handler returning normally differ from unwinding an interrupted anonymous PC. The exact core's fault attribution has a separate gap that adding FDEs would not repair: ownership lookup uses module address envelopes and frame records, without consulting generated-code ownership.

All live gates remain **CLOSED**. This review narrows the next offline work; it does not establish the actual native control-flow environment or authorize attachment.

## Evidence and scope

[Captured metadata](result.json) and [independent verification](verification.json) identify eight SDK files at 3e0c5cb010454228f4ee63226b60d060494eeaae and three unchanged binding/observer sources at the development checkpoint above. The collector retains its own source, eleven git-show commands and their outcomes privately. Public evidence contains hashes, source identities and bounded lexical-match locations, not source dumps, host paths or runtime logs.

The lexical search covers only the eight listed SDK files. Matches may be declarations or comments as well as calls; the line-local regex misses multiline spellings, aliases and indirect calls. It does not identify reachable calls or establish absence across firmware, dependencies, native handlers or other source files. No earlier static extent check or execution matrix was repeated. Source-derived counterexamples below are not observed faults or test results.

All SDK references below use that pinned revision:
[installer](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/inline_hooks.c),
[protection helper](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/common/azprot.h),
[community loader](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/community.c),
[crash guards](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/crash_guards.c),
[alternate stack](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/common/azaltstack.h),
[SDK interface](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/azmod-mod.h).

## Distinct control-flow contracts

| Event | Evidence and contract | Remaining limit |
| --- | --- | --- |
| Ordinary C++ exception begun in the authored body after tail transfer | The prior fixture exercised native/caller FDEs and body/caller cleanup for both authored entries. Anonymous transfer instructions are not return frames on that route. | Not every native target, relocation, helper exception or interrupted-PC unwind. |
| Signal handler followed by normal signal return | Normal signal return restores signal-frame state, which the handler may have altered; it is not CFI-based unwinding through the transfer. | No signal execution or identification of every installed/chained handler in this review. Actual handler behavior and thread setup remain unknown. |
| Stack walk or unwind starting at an interrupted island/trampoline PC | The pinned installer supplies no generated FDE. The selected copied STP changes SP before jumping back. | Precise anonymous-PC CFI coverage remains unsupported. Ordinary-throw success cannot establish it. |
| Signal nonlocal exit across the helper | The helper uses RAII to release its sink flag and restore errno. Skipping cleanup could strand state; C++ restrictions on nonlocal exit across nontrivial destruction also apply. | An FDE alone is not a recovery contract. No such handler policy is established. |
| Forced unwind/cancellation through wrapper or helper | The helper is noexcept; no compatible cancellation/unwind contract there is established. | Starting later in the native body is a different path. No general cancellation/asynchronous unwind claim. |
| SDK fault reporting and quarantine attribution | Concrete consumers inspect PC/LR and x29-linked frame records, chain the previous handler and retain crash attribution for later handling. | They do not consume anonymous DWARF FDEs, prove causation or provide in-process recovery. |

The [production helper](../activation-binding-composition/observer.cpp) checks one authoritative capture state. Closing it suppresses future admission but leaves the island, entry and trampoline reachable. An admitted helper may still publish. Permanent originals and code remain retained; close is not drain, unpatch, reset, retry or reclamation.

## Existing fault path and ownership gap

The main reporter (crash_guards.c:422–454) records ucontext registers and walks up to forty frame records through crash_peek8. The optional simple reporter (51–158) also walks frame records, using direct loads and bounded address heuristics. These are not DWARF unwinder calls. The mapping report (455–475) filters selected named modules; ordinary unnamed generated mappings do not satisfy that filter, although raw PC/LR are logged.

cr_chain (484–499) calls the saved previous SA_SIGINFO or simple handler when applicable. Otherwise it restores the saved disposition, which can include SIG_IGN; it re-raises SIGABRT or a signal with nonpositive si_code, while a synchronous positive-code fault normally re-faults after return. cr_rearm (502–527) can reinstall the reporter ahead of replacement handlers. Those chained handlers remain opaque. Alternate-stack setup can fail (azaltstack.h:54–82); its worker wrapper (89–93) still invokes the worker, and existing native threads are outside that wrapper's setup. This supplies no universal alternate-stack or signal-exit guarantee.

Ownership lookup has a narrower source-defined coverage:

- community.c:1502–1522 computes each mod's minimum-to-maximum PT_LOAD envelope. cm_owner_of at 1533–1536 only searches those envelopes, which may include gaps between segments.
- cm_blame_fault at 1561–1576 tries PC, LR, then up to sixteen frame-record return addresses. If none matches, it records an unblamed crash.
- inline_hooks.c:100–105,150–155 records a claimed native function address and owner, without a generated range. Island pools at 114–117 record base/used; the trampoline at 292 is local to installation and published through the original slot at 335.
- The loader uses the hook registry to refuse overlapping claims (community.c:1252–1260), not to associate an island/trampoline PC with a mod.

A source counterexample follows for the first attribution attempt: an interrupted PC in generated code, with native LR and an entirely native frame chain, all outside every mod envelope. The tail route creates no mod return address. Every current ownership lookup misses, despite a successful prior hook claim. This is conditional source reasoning, not an observed crash, proof of a particular mod's guilt or evidence that quarantine fired incorrectly.

Adding generated FDEs alone would leave this lookup unchanged. Attribution and unwinding need separate contracts.

## Conditional prepublication metadata contract

If a later scope requires precise interrupted-PC unwind, generated metadata must be valid and visible to the selected consumer before any redirect can reach it. The public SDK interface offers no community preparation phase:

1. azmod-mod.h:199–204 accepts an original void** output, without generated-range receipt or preparation callback.
2. community.c:1251–1262 holds g_cm_mu through installation. This serializes claims, not native execution.
3. inline_hooks.c:280–316 privately selects and emits code. Relocation can expand instructions; the prior one-STP twenty-byte trampoline is not a universal extent.
4. inline_hooks.c:335–336 publishes the original then the target branch with adjacent relaxed/release stores. No community callback sits between them. Post-claim registration or polling the output cannot guarantee metadata before first reachability.
5. Target-page unlock failure at 330–332 frees the trampoline. Earlier metadata registration without changed cleanup could leave references to freed code. Registration inserted after that successful unlock but before original/branch publication would occur while the target page is writable and the protection mutex remains held (azprot.h:123–145), inside the claim lock. An arbitrary callback there needs its own locking/failure contract.

For the selected one-STP route, instruction states differ at island +0/+4 and trampoline +0/+4/+8; at trampoline +4 the copied STP has changed SP. Literal-pool bytes are not executable instruction states. Neither native +4 CFI nor wrapper CFI describes an anonymous trampoline address.

A future separate core design would need exact instruction extents and immutable per-PC states, a defined supported relocation subset and proven visibility in the actual unwind provider before original/branch publication. Registration outcomes must reflect that provider's real API, not an invented success return. Code/metadata must remain valid on later failure as well as success; process-lifetime retention is the conservative scope unless deregistration and quiescence are separately established. Preserve existing eligibility, late-island refusal and postpublication protection-warning distinctions. No such design is implemented here.

## Next bounded work: generated-range attribution

The immediate source-backed improvement to review is a separately scoped **core-owned generated-range ownership contract**. First add an offline fixture around exact existing ownership/frame-selection logic with synthetic owned records. Discriminating cases should expose the anonymous-PC/native-chain miss, retain mod PC/LR/frame hits and ordering, distinguish unreadable-frame truncation and preserve envelope-gap behavior as a recorded limitation.

A later core candidate should associate each emitted island slot and actual trampoline instruction extent with the validated claiming owner before redirect publication. It must not assign an entire shared island pool to one mod. Use bounded immutable retained records, explicit overlap/capacity refusal and a signal-consumer publication/lifetime contract. Prepublication failure or unmap must not leave stale attributable ranges; after publication, close must not remove ownership or reclaim code. Preserve native PC/LR/frame fallback, attribution precedence and quarantine protections. Attribute ownership, not fault causation. This is a proposal only: no registry, repair patch or new fixture is implemented here.

Passive capture and activation have distinct gates. Copying action x2 as opaque bits and service Source from registers does not require native dereference or proof of the final Local Library action. Passive attachment still needs exact live image/entry provenance, actual SDK admission/current hook ownership, stable original/code lifetime, accepted handler/control-flow scope and unresolved writer/topology/recovery safeguards. Row/provider/visible-label correlation, actual arguments and operation owner scope must be established before interpreting an event as accepted activation; alternate destructive/replacement paths, queued callbacks and the caller's post-callback timer access remain unresolved.

No native pointee/post-return read, return interception, TLS pending state or cross-entry pairing is introduced. The x16 tail-transfer exception and installer x17 limits remain explicit. USB activation, native cancellation, main/live dispatch, emulator recovery, stems staging and all unresolved product gates remain closed. No physical action, core installation, community update or release occurred.

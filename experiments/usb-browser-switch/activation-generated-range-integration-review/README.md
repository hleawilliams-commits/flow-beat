# Generated-range integration review — source only

This successor to the [offline metadata fixture](../activation-generated-range-fixture/README.md) at cd48d522da0f9c2b6ff0d5c8e04733c900928d7b identifies where a future core transaction must obtain authority, retain resources and publish ownership. It adds no production registry integration, repair patch, executable fixture or live observer. Every live, activation, recovery and staging gate remains CLOSED.

The main result is a concrete **azmod inline-installer transaction** with a proposed lock order and immutable module inputs. It is not a process-wide patch lock. The current source has separate patch families and a separate azstem DSO; those remain outside this proposed boundary. The frozen C++ metadata fixture cannot be inserted into the C core as though it already supplied SDK admission or mapping custody.

## Evidence and scope

[Source receipts](result.json) and [independent verification](verification.json) retain 33 inputs and 39 files. Twenty-three selected SDK sources are reused from the previously retained 110-blob corpus at SDK commit 3e0c5cb010454228f4ee63226b60d060494eeaae. The only new Git read is src/Makefile: one command, two output streams. The packet also retains the old corpus bytes/manifest/tree and five exact frozen fixture/contract inputs. No old source capture, passing matrix, compiler, make invocation, firmware decode, emulator or deck operation was repeated.

A new lexical index covers all 110 retained production blobs, with 102 installer, 28 protection, 57 vtable and 14 owner-ledger matching lines. These counts include declarations and comments; they are not an executed or compiler-resolved call graph. Additional extracted files support bounded manual review. Source-derived schedules below are conditional counterexamples, not observed runtime failures.

Pinned references: [core aggregate](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/azmod.c), [internal declarations](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/azmod_internal.h), [installer](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/inline_hooks.c), [community loader](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/community.c), [protection helper](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/common/azprot.h), [vslot service](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azmod/sdk_vslot.c), [azstem](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/azstem/azstem.c), and [new build recipe](https://github.com/Kyle-Hosman/xdj-az-mods/blob/3e0c5cb010454228f4ee63226b60d060494eeaae/src/Makefile). Line references use these exact bytes.

## Actual writer boundaries

azmod_internal.h:167–168 routes the install macro to one static azm_install_hook_at. azmod.c textually includes the installer at 1709. Community calls enter the same implementation at community.c:1260. Direct Core calls currently bypass the community-only overlap check at 1251–1258; the installer itself never checks that ledger before allocating or publishing. Reserving target and metadata capacity inside a common transaction would therefore add enforceable Core admission. Compatibility with existing duplicate-target intentions must be reviewed before changing the core.

The new Makefile confirms separate recipes: azmod uses four C source translation units (79–82); azstem builds separately (86–93). azmod.c:48 and azstem.c:50 include azprot.h, whose mutex is static at 119. The azmod include aggregate shares one instance through the header guard, while azstem has a different instance. The header's “process-wide” comment is not evidence of cross-DSO serialization. Its lock covers each successful unlock through relock (129–145), not island allocation or whole installation.

The selected startup sequence installs Core hooks, calls azm_hooks_go_late, then starts the overlay (azmod.c:2835–2886); overlay state loading invokes community_init later. However, the constructor attempts to start a crash-watch thread at 2868, and selected day-mode/stem-mark setup attempts to start workers before go_late. The phase bit is not proof that only one thread exists. Its current acquire check at inline_hooks.c:282 does not reserve constructor eligibility until the four-word fallback write at 354.

Other patch families remain explicit: azm_patch_vtable at 366–379 emits no generated range; sdk_vslot has its own 64-slot ledger at 14–20 and 91–105. install_msgtick_hook (azmod.c:2425–2445) patches at runtime using a separate once state and waiting callers. source_list_entry performs callback-time vtable binding. Raw code/art writers and azstem's own installer are also outside an inline-only ledger. A known target can still be altered by a nonparticipating writer; this review establishes no universal exclusion.

## Proposed lock and admission protocol

This is a design for a separate offline successor, not a lock added here.

| Operation | Proposed acquisition order | Obligations |
| --- | --- | --- |
| Community inline claim | Existing recursive g_cm_mu → new inline writer W → azmod protection mutex | Capture caller RA at the outer SDK function; revalidate admission; copy owner; reserve and publish; finish nhooks bookkeeping before release |
| Direct Core inline install | W → azmod protection mutex | Construct an internal typed Core owner; consume immutable module inputs; never acquire g_cm_mu while holding W |
| Loader record/module publication and init open/close | g_cm_mu → W, then release both | Commit copied facts only; invoke dlopen, loader enumeration, entry, init and callbacks outside both |
| Constructor-to-late transition | W | Serialize the phase transition with complete fallback installs; do not turn this into a native quiescence claim |
| Attribution reader | No writer/protection lock | Acquire committed immutable records in a bounded scan; never call loader/admission code |

W must cover target/capacity reservation, island allocation, preparation, metadata publication, original/redirect and retained bookkeeping. It must not be recursively acquired through a public wrapper around the same private implementation. No W → g_cm_mu edge is allowed. Any service that already holds the protection lock must not call the inline installer. The existing g_cm_mu is recursive (community.c:1095); that does not make a new W automatically reentrant.

The loader must enforce a one-start/load-epoch guard before callbacks, rather than rely on community_init's “called once” comment at 1876. An in-progress repeated call must refuse without waiting on its own callback. All new records and module inputs have process lifetime; there is no reinitialization or slot reuse after publication.

cm_claim_hook currently checks ID, return-address envelope, init flags, capability and target envelope before acquiring g_cm_mu (1226–1251). Init closes its flags outside the lock (1850–1851), and nhooks increments after unlocking (1271). A claimant can pass preliminary checks, wait while init closes, then enter the installer under the old structure. The proposed transaction must recheck the epoch and record under the shared admission discipline and increment nhooks before releasing it. Init return closes that epoch under the same order: an already admitted transaction finishes before close; one ordered after close refuses. Closing admission does not drain callbacks, threads or previously installed hooks.

Preserve the existing absence of a thread-identity predicate; do not silently add a new SDK thread rule or infer one. Capture CM_RA at the outer cm_claim_hook entry before introducing helpers, or a helper may authenticate the core return address instead of the caller. Copy the bounded ID from validated core-owned record custody, never from a later ambient g_hook_owner read. Core tokens are internal and cannot be requested by passing a community string.

A copied claimant identity is not proof of replacement-code ownership. Inline claim/installer do not check hook/orig for nonnull, lifetime or aliasing, and inline claim does not require hook to reside inside the claimant. sdk_vslot:74–81 does check nonnull and replacement envelope and allows a different Loaded/loading admission rule at 63. Do not accidentally substitute that policy for inline claims. Exact permanent original-slot storage, replacement-code custody and nonaliasing require a separately specified internal receipt; nonnull alone is insufficient. A general raw void** API cannot prove arbitrary caller memory lifetime.

This ordering proposal does not prove all dependencies or safe nonlocal exits. Installer diagnostics call dbg, which uses stdio (azmod.c:609–621); allocation/protection uses libc and syscalls. Loader locks, interposition, signals, callback recursion, cancellation and forced exit remain limits. No lock or reader should attempt reclamation after such an unproved exit.

## Immutable module inputs and the new provenance refusal

Current module lookup reads g_cm_n and mutable lo/hi directly (community.c:1533–1536). The count increments before record memset/ID initialization (1888–1891 and 1910–1914). Range discovery uses c->lo as a scratch load base, sets hi=0 (1821–1822), then replaces lo/hi in cm_range_cb (1515–1516). Copying those fields without synchronization does not create a coherent immutable input.

There is also a conditional provenance counterexample: dlinfo failure at 1820 does not stop cm_range_of at 1824. For a fresh zeroed record, lo stays zero; cm_range_cb matches the object whose dlpi_addr equals that value (1506). If an enumerated main image has base zero, its range can be selected without establishing the intended DSO. No dlinfo failure or misattribution was observed here.

A successor must retain an explicit successful-handle/link-map-base result, collect program headers into private scratch, require the intended object and a checked nonempty envelope, then publish copied identity and endpoints under the loader/writer discipline. Base zero is neither a sufficient provenance check nor inherently invalid. Failed provenance leaves the loaded handle retained and refuses new inline admission for that record; it must not substitute a guessed envelope.

Use a fixed CM_MAX-sized set of one-way module publication flags in original g_cm index order. Reader acquire precedes immutable payload; no early count or initialized-prefix assumption. The writer uses these same committed inputs for admission. Keep min/max PT_LOAD envelope semantics, including holes/non-executable segments, rather than silently changing attribution to executable sections only. Checked arithmetic must reject malformed overflow rather than wrap an endpoint.

Publish a valid module envelope before calling entry/validating its descriptor (the current range→entry sequence is 1824→1827), and retain it when the descriptor is rejected or init fails. Module attribution is separate from hook permission and successful init. dlopen constructors run before this range publication; their execution and faults are not covered retroactively.

At each PC/LR/frame, module lookup remains first, then generated instruction lookup. Generated Community PC can precede a module LR; typed Core continues fallback. Later valid module records may mask earlier generated instructions or own literal addresses under their envelopes. There is no reverse loader admission or permanent non-overlap promise. New immutable records would not repair every legacy g_cm reader, the volatile written guard, handler chaining, persistence or arbitrary loader concurrency.

## Owned emission receipt and exact publication cut

The fixture accepts asserted addresses/ncopy/ADRP counts. The real installer computes ncopy and authoritative relocated word count out at inline_hooks.c:281 and 298–315. A core-private receipt must retain those actual outputs, source words already inspected by the installer, continuation, replacement and original-slot identity. Keep the sixteen-byte target exclusion separate from actual four- or sixteen-byte patch coverage.

The receipt also needs island pool/slot identity as used increments (122–146), the trampoline mapping before seal (292/316), and each returned allocation/protection/cleanup outcome. azm_tramp_seal internally attempts munmap on seal failure (180–185); later failure branches also ignore munmap results. An invisible metadata cancellation does not prove that native storage was released. Burned island slots remain consumed; failed or uncertain cleanup needs retained resource custody even when no generated record becomes visible.

Island and target protection tokens must be retained separately: the existing local pt is reused at 318/321/330. Successful relock restores saved bits, not necessarily RX. If an earlier relock failed and left a shared page writable, a later unlock/query/relock can save and restore those writable bits successfully. Full emitted-storage separation, including literals, does not imply separate protection pages or recover original protections.

| Phase | Required internal receipt behavior |
| --- | --- |
| Before allocations | Copy trusted owner; reserve target and fixed metadata capacity together; bind stable original/replacement custody; refuse known target/capacity conflicts before allocation |
| Allocate/relocate/seal | Retain actual mapping and pool slot, ncopy/out/continuation, instruction and full storage bounds, seal and cleanup outcomes |
| Prepare island | Validate storage against retained targets/blobs/modules before island write; retain island unlock/relock outcomes separately; warnings do not become successful RX claims |
| Final authorization | Under uninterrupted W, freeze the exact receipt and module inputs; finish all fallible validation before target unlock at 330 or 346 |
| Successful target unlock | Publish metadata immediately before original store at 335 or 352, with no new lock, provider, allocation, pointer check or ordinary refusal branch |
| Original then redirect | Preserve existing route order at 335–336 or 352–354; complete cache/relock attempts and retained diagnostics |
| After metadata visibility | Never clear/reassign metadata or release code/identity/original custody, including warnings, failed init or abnormal interruption |

The existing C++ commit returns defensive errors (registry.cpp:299–303). It cannot simply be called after target unlock with a new failure branch. A production internal permit must be bound and consumed in the same lexical writer transaction so those misuse conditions are excluded before that cut. The successful commit is a bounded infallible publication; errors remain preparation/refusal outcomes. This review does not implement that API or prove actual instruction-cache/cross-thread/signal ordering.

The Makefile uses C compilation and conditional -mno-outline-atomics probing (12–18,41–45,79–92). The Apple ARM64 C++ fixture's LDAR/STLR result does not establish this GCC/Linux core's ABI, lock freedom, generated code or unwind behavior. Even make -n can evaluate the probe: no make invocation was made here. A separate C-compatible integration design and exact toolchain validation remain necessary.

## Partial initialization and next bounded work

Retain successful dlopen handles, copied owner/module records, visible emissions, permanent original slots and any uncertain failed-allocation custody. Descriptor/init failure clears callbacks/state in selected paths; it is not proof that constructor-created threads, installed hooks or queued callbacks stopped. Disable/remove and failed Reload requests do not end process lifetime. The previously documented non-EINTR Reload wait failure, quarantine counterexamples and all prior failed/passing evidence remain unchanged.

Next implement a separately versioned **offline C transaction/admission fixture**, using this reviewed boundary and synthetic owned resource outcomes. New cases should distinguish a claim queued across init close, common Core/community target/capacity reservation, late-phase transition during an admitted fallback, wrong/reused internal permits, invalid module provenance versus valid zero base, count-before-ready and immutable first-index publication, descriptor/init failure retention, and separate warning/cleanup/protection receipts. Use exact safe negative controls for these new behaviors; do not replay the old registry or installer matrices. This is an offline follow-up, not permission to patch or install the live core.

Real replacement/original-slot lifetime, nonparticipating writers, loader/interposition and signal/nonlocal-exit scopes remain integration gates. Passive attachment additionally requires exact live image/admission/current ownership, stable originals/code and application/supervisor/QEMU/stopped-recovery evidence. Local Library row/provider/readiness, actual arguments, operation owners, alternate destructive/queued paths and caller timer reads remain activation gates. Preserve action x2 as opaque bits, service Source from registers, the x16 tail-transfer exception and x17/island-unwind limits. No native pointee/post-return reads, return interception, TLS pairing, quarantine reset, device action, release or stems-fix claim is introduced. Ownership is not fault causation.

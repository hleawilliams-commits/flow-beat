# Offline installer receipts over owned synthetic targets

This implements the bounded successor described in the [installer receipt plan](../activation-installer-receipt-plan/README.md). It combines permanent authored bindings, real Linux AArch64 mapping/protection operations, a separately transformed late-island installer, and an internal metadata-before-original-before-redirect publication sequence. It is an offline fixture, with no firmware addresses, live hook claims, community-loader integration or product installation.

**The first runtime matrix passed** in plain and ASan/UBSan modes: sixteen fresh candidate scenarios, sixty summed checks per mode, and five controls with exactly their declared failures per mode. The sixty checks use forty-eight distinct names because the refusal cases share labels. The [result](result.json) is separate from the retained [initial compile/object refusal](initial-result.json). The independent source/claim reviews and first-pass [retained-byte audit](verification.json) pass without compiler, installer or matrix replay.

The sources are [receipt.h](receipt.h), [receipt.c](receipt.c), [transform-installer.py](transform-installer.py), [fixture.S](fixture.S) and [test_receipt.c](test_receipt.c). [test-contract.json](test-contract.json) declares the selected named checks and exact negative-control failures. [run-tests.py](run-tests.py) snapshots those inputs and refuses changed reviewed bytes.

## Exact source boundary

The transformer consumes retained copies of SDK commit `3e0c5cb010454228f4ee63226b60d060494eeaae`, with these SHA256 pins:

| Retained input | SHA256 |
| --- | --- |
| inline_hooks.c | f141b383c25e66c8acb1839280832a612a6ee0e25e9ada59f374ccbef8f81908 |
| azprot.h | 0f1cc9c5d4ca212f798bf2e9d74a69507a82ce5cc59bd328bacc46816a119b33 |

There is no new Git source capture. The frozen SDK sources remain unchanged. The generated successor has separate filenames, complete unified diffs and a [transformation ledger](transformation-ledger.json). The runner reconstructs the diffs, binds every generated output hash, and checks selected before/after function bodies against the ledger.

The instruction classifier, ADRP target/re-encoding/materialization helpers, absolute-jump encoder, branch reach check, function-end check, four-opening-instruction policy and maps-line parser retain their exact bodies. The selected installer orchestration, mapping/cleanup boundaries, protection handling and publication cut change. Receipt-bearing maps queries retain at most sixty-four read outcomes and refuse a further read at that journal limit. Separate diagnostic map queries are unjournaled and do not use that receipt cap. This is not byte-identical execution of the whole SDK installer or protection helper.

The selected route always requires an island and copies one opening instruction. Constructor fallback is removed from this successor. Other inherited static helpers remain in the generated translation unit where needed for compilation; unused hook-dump/vtable paths are not exercised. The frozen passive observer, binder and tail-entry assembly remain unchanged elsewhere and are not linked into this fixture. New scalar targets and replacements keep this test focused on receipts, custody and publication rather than replaying their ABI/throw suites.

## Fixed bindings and admission

Four process-image descriptors bind exact authored target symbols, replacement symbols, permanent aligned plain-pointer originals and retained attempt records. Descriptor3 deliberately aliases target0 while retaining a distinct replacement and original slot. This supplies a common-target refusal case without inventing an arbitrary executable address.

Requests carry a binding index and copied replacement/original values. Identity checks precede dereferencing an original pointer; only the descriptor's known slot is subsequently inspected atomically. The cases include a different replacement, a deliberately inaccessible original-pointer value, a temporary stack slot and a preoccupied genuine slot. No rejected pointer is probed.

Each admitted binding retains its target reservation and attempt. There is no reset, retry, unpatch, unload or destroy API. Successful published code, held allocations and uncertain cleanup custody last until fixture process exit. Successful prepublication cleanup releases only its own mapping and preserves the receipt. A later binding failure does not clear an earlier original or alter its published record. Diagnostic addresses remain usable capabilities; the API does not enforce alias isolation or protect against external writers.

Community admission uses one copied, authored module descriptor and one init epoch. It holds the fixture's nonrecursive cm → writer mutexes through the actual installer and hook accounting; Core uses writer alone. Closing the epoch uses the same order. A queued request rechecks the closed epoch before entering the installer, while a request already holding admission completes and accounts before close proceeds.

This is narrower than SDK authority. The supplied identity and envelope are fixture facts, not recovered DSO/link-map provenance or authenticated caller-RA evidence. The fixture requires the exact fixed replacement entry to lie in that supplied envelope, which is an authored admission rule rather than proof of the SDK's claimant policy. It does not execute community.c, capability checks, loader constructors/entry/init, arbitrary module admission or the SDK's recursive mutex ABI. Core is a distinct typed owner with an empty community-ID field.

## Actual storage and the publication cut

Reservation happens before resource calls. Each actual allocation return is retained before later work. A conservative maximum trampoline footprint is checked after its retained mmap and before its first instruction write. After relocation, exact spans are derived from the actual output count; fixed targets, original objects, retained emitted blobs and the copied module envelope participate in the selected storage checks. These checks operate within this private allocator/process-image scope, not arbitrary mapping custody.

The island contains eight instruction bytes followed by its eight-byte literal. The trampoline contains the copied or materialized instruction output plus the two-instruction jump; its final eight bytes are the continuation literal. One copied instruction produces either one relocated word or four materialization words. Thus the full trampoline blob is twenty or thirty-two bytes. The generated-only lookup attributes instruction intervals, excluding literals and padding; it does not attribute an entire island pool or mmap page. It does not implement module-first blame or stack walking.

Immutable metadata is filled while still invisible. The selected permit validates fixed binding and owner values before target unlock. After successful target unlock, the lexical publication cut contains no allocation, callback, new lock or ordinary refusal:

1. Release-publish the prepared metadata record.
2. Store the actual trampoline into the genuine plain original object.
3. Release-store the actual branch instruction into the authored target.

Fixed diagnostic assignments and loads surround these stores. In particular, the metadata observation occurs immediately after the actual original store; the ordering control moves that store, rather than merely swapping labels. This is a local discriminator. It is not a proof of cross-core instruction-fetch visibility, arbitrary signal readers or all weak-memory executions.

Mutable syscall, stage, warning and accounting receipts are separate from the immutable lookup payload. The target-cache operation and postpublication relock continue after the redirect. An interruption can leave an armed or partially published transaction; no nonlocal-exit recovery, lock recovery or process-death persistence is implemented.

## Receipts, failures and protection

The journal separates attempted/returned, underlying-called/injected, raw result, and whether saved errno is meaningful. Pthread return codes, maps parsing, cache-clear return observations and aggregate unlock results retain their own meanings; the fields do not imply that every record is a syscall. Maps open/read/close outcomes are separate. Cache clearing has no returned success code, so only its armed and normally returned state is recorded.

Under the private serial pool invariant, at most six island candidate mmap calls and one trampoline mmap fit the bounded mapping journal. The allocator can skip low hints; six configured candidates do not mean six actual calls. Existing pools can supply a slot without a new mapping. The retained pool base/length and burned slot identify that case separately from allocation receipts. Pool custody remains separate from instruction attribution.

The tested failure paths use declared pre-call injection, returning an error without calling the selected syscall. They do not pretend to observe a real kernel refusal. Actual successful calls use ordinary mmap, mprotect, munmap, maps-file operations and cache clearing. The deterministic uncertain-cleanup case targets the actual owned trampoline after seal refusal; it does not fabricate a mapped pointer or depend on unpredictable out-of-reach placement.

Allocation, seal and cleanup results are preserved independently. A failed cleanup leaves held/uncertain custody; no automatic second unmap follows. Island and target protection tokens are separate, with token validity only after successful unlock and complete base/length/saved-bits evidence. Refusal never publishes invented token contents.

Relock restores the saved protection, which need not be RX. An injected relock refusal can leave a page writable; a later successful unlock/relock on that pool can restore the already-writable bits. Current map queries are separate diagnostics. The harness refuses to invoke a route whose target, island, trampoline or replacement is not readable, executable and nonwritable.

The install return, authoritative publication and warning status remain distinct. Selected successful installation returns the retained installer's 1 even if a relock warning occurred. The richer status reports warnings while keeping originals and metadata. It does not translate that state into ordinary refusal or authorize a retry. The diagnostic-ring saturation case checks that a lost diagnostic record cannot overturn authoritative publication.

Each protection helper uses its own actual page-size return for checked address-plus-length and round-up arithmetic. The successor explicitly requires the retained recipe's 4096-byte page size. Arithmetic-only overflow cases refuse before dereferencing any supplied address. This conservative guard does not generalize the installer to other page sizes.

## Selected checks and safe controls

Each selected scenario starts in a fresh process. The current contract covers fixed binding rejection; Community ADRP and Core scalar emission; trampoline map, seal, island-unlock, target-unlock and no-island refusals; uncertain cleanup; retained partial installation; independent token evidence and writable-page warnings; queued versus admitted epoch close; a Core/Community target conflict; diagnostic saturation; and checked rounding.

Before a scalar invocation, a separate gate checks fixed identities, current original value, unchanged uncovered target bytes, the actual branch destination, paired jump instructions/literals, actual ADRP relocation/materialization and RX protections. An unsafe route fails the named semantic checks without being entered. All scheduled installer/closer threads are released and joined before the final report. The callbacks are synchronous fixture scheduling points outside the publication cut, not production event adapters.

The five controls preserve executable destinations and do not use a crash, null branch, freed trampoline or sanitizer failure as an oracle:

| Control | Deliberate fault |
| --- | --- |
| original-before-metadata | Move the real original store ahead of metadata, preserving the following metadata load. |
| literal-as-instruction | Extend only the metadata instruction interval into the trampoline literal. |
| erased-cleanup-custody | Misreport failed cleanup as no longer held, without another unmap. |
| aliased-protection-evidence | Copy target-token values into island diagnostics while keeping real relock tokens intact. |
| warning-as-refusal | Misclassify published-with-warning status, retaining code and originals. |

The exact complete named failure lists are fixed in the contract before execution. Candidate output must contain no failures; controls must produce only their declared failures after safe completion. Unexpected stderr, sanitizer diagnostics, timeout, infrastructure failure or extra output rejects the attempt.

The matrix used GCC (Ubuntu/Linaro 7.5.0-3ubuntu1~18.04) 7.5.0, Linux AArch64, C11 and the pinned azmod-build:18.04 Docker image sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90. Builds use strict warnings and O1; the sanitizer mode enables ASan/UBSan with leak detection and halt-on-error. This does not establish general mmap leak freedom or cleanup of intentionally retained process-lifetime mappings. Containers use no network and no image pull.

The runner compiles a standalone receipt object without RC_FIXTURE and does not execute that configuration. Executed candidate/control builds include the explicit scheduling and fault-injection configuration. The copied emitter/policy helpers still run within the transformed source. Separate object/executable metadata and disassembly are retained; ELF checks establish AArch64 identity and the intended original-object definition, not a signal-safety or code-generation theorem.

## Retained first failure and current status

The initial standalone compilation completed cleanly, but the object checker found rc_originals as GCC SHN_COMMON: size32 with alignment8, rather than a strong definition in a retained section. The source repair adds an explicit null initializer; the ELF oracle was not weakened to accept common/coalescible storage. That failed packet, generated source, object and command receipts remain preserved.

Source review also found that the runner's raw-result schema omitted the real late refusal value −3. Before any runtime execution, the schema was corrected to the exact selected/unentered set {−3, −1, 0, 1}. This was an oracle defect, not a new runtime failure. The second packet used the repaired source and schema and completed the first runtime matrix. No successful runtime case from the first packet was replayed: that packet had executed none.

The successful packet retained twelve inputs, five generated files, 143 commands and 286 primary output streams, 176 work artifacts and 769 total files. The result’s count of 768 intentionally stops at private result.json; its separately written sanitized-result.json is the additional file. The first refused packet retains forty-two files, five commands/ten primary streams and one standalone object. These are packet inventories, not syscall counts.

Both Community emission cases observed one copied ADRP expanding to four materialization words (out4); the two core-emission selector runs used out1. The ADRP re-encoding alternative was not exercised. No-island refusal retained five injected pre-call mmap refusal receipts in each mode because one of the six configured hints was skipped; none called the underlying mmap. The richer warning cases retained installer result1 with publication, and later successful relock preserved the already-writable pool page.

Private packets contain exact source snapshots, transformed files/diffs/ledger, command environments and arguments, raw stdout/stderr and exits, partial artifacts, ELF records and full bounded per-process evidence. Public summaries are allowlisted and omit local paths, raw addresses and binaries. The independent retained-byte audit binds both closed packets, exact source repairs, commands, generated diffs and ledger, public copies, strong original-object storage, linked original target bytes, selected raw receipt facts and all named report/control outcomes. Its result SHA256 is `4f1dd82ee6e08d41cff8d4d8cd13ff7b7785b5e20498b268669c1377ce3f75a1`; the audit script is `543d9b3d25edc512dd89f382e333d9178eff31ba13f9e72a504663c6e9a377c2`. It ran once without executing captured tools or binaries. Scalar return values are not separately serialized, so the audit does not independently reconstruct every runtime predicate. See [verification.json](verification.json) for counts and scope.

These observations do not establish general SDK integration, live ownership, callbacks in firmware, asynchronous anonymous-PC unwind, signal safety, forced unwind/cancellation, external writers, concurrent invocation during installation, arbitrary module lifetimes, release persistence or hardware timing. Assembly hit counters and scalar target code are not sanitizer-instrumented; the selected calls occur after the scheduled installers join. No TSan or general concurrency proof is claimed.

The next bounded source task is a precise composition plan for the retained community claim ingress, init-epoch closure and hook accounting with this receipt successor. It must preserve the fixed descriptor/original custody and distinguish exact retained policy code from authored loader, caller-RA and capability inputs. The shared lock revalidation and irreversible partial-publication accounting are the new boundary; rerunning the same successful scalar routes would not establish it. That integration is not implemented here.

All live observer, native activation, writer/recovery, frozen staging and physical-testing gates remain unchanged. No emulator/device endpoint, physical control, package/catalog publication or stems fix follows from this fixture.

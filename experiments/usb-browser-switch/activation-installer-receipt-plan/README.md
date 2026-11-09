# Offline installer receipts and permanent binding plan

This source-only successor to the [C transaction fixture](../activation-admission-transaction-fixture/README.md) specifies one new authored-target experiment. **No receipt-enabled installer or new executable fixture exists here.** The first successor is late-island-only for Core and Community; fallback, old ABI/throw suites and live attachment are deferred.

The intended composition binds permanent originals/replacements and publishes ownership derived from actual emission before original/redirect. Separate passing parent fixtures do not establish it.

## Evidence and source boundary

[Source receipts](result.json) retain twelve reused inputs plus the plan and retention script: fourteen inputs, eighteen line-byte anchors and sixteen private files. The first retention pass completed without failure. The independent [byte audit](verification.json) passed 109 checks on its first invocation, checking every pinned input, the bounded anchor index, closed inventory and exact sanitized projection without importing or rerunning the collector. Independent source and future-control reviews also passed.

No new SDK Git read, compiler, make, installer, old capture/matrix replay, firmware or device work occurred. These are byte/provenance and source-review results, not runtime or semantic proof. Raw input copies and local provenance remain private.

Anchors refer to SDK `3e0c5cb010454228f4ee63226b60d060494eeaae`; [adapter-plan.json](adapter-plan.json) binds hashes and line intervals. Parents are the [integration review](../activation-generated-range-integration-review/README.md), [generated-range contract](../activation-generated-range-contract/README.md), [binding composition](../activation-binding-composition/README.md) and [transfer fixture](../activation-transfer-fixture/README.md). They supply admission premises, lookup policy and selected forwarding evidence, not executed real community-loader admission.

## Why the existing interfaces cannot simply be connected

The C fixture's `tx_plan` supplies synthetic addresses, counts and syscall results. It publishes internal integer original/redirect slots (transaction.c:513–541) and frees its owner after test quiescence (187–200). Neither operation supplies custody or teardown for reachable real hooks.

The installer calculates ncopy/out (inline_hooks.c:281,298–315) and publishes actual original/redirect at 335–336. Deferred fallback uses 352–354. Calling it before the model is too late; calling it afterward leaves two transactions. An outer wrapper cannot supply the internal prepublication cut.

The bridge validates identities (bridge.c:79–85), calls the installer at 91, then reads original/path/bytes at 92–98. Its twenty-byte trampoline oracle covers STP-only targets. New receipts must use actual relocated output, including ADRP expansion.

The bridge also conditionally requires a diagnostic path record before translating installer success to SDK success (89–101). Once `g_azm_rec_n` reaches 256, `azm_rec` increments the lost count and returns without appending (inline_hooks.c:205–224). A later installation could therefore return 1 after publication while that bridge returns failure because no path was recorded. This is a conditional interface limit; the earlier fresh two-claim result is unaffected. New authoritative receipts must not depend on the diagnostic ring.

## Fixed process-lifetime binding custody

Use a small fixed descriptor table, initialized from authored executable symbols and the unchanged replacement entries. Each descriptor binds a unique BindingId to:

- its authored target and sixteen-byte exclusion interval;
- the exact replacement entry and its retained executable storage;
- the exact aligned plain `void *` original object, initially null;
- the retained attempt/owner slot and one-way binding state.

Install accepts a binding identity and copied admission values, not arbitrary pointers. A compatibility SDK-signature ingress compares replacement/original identity to its descriptor before dereferencing, then uses only the known pointer. Reject mismatched, temporary, aliased or preoccupied slots before allocation, without probing rejected pointers.

observer.cpp:15–16 defines the permanent globals read by LDAR/BR in entry.S:234–238. Preserve genuine plain-pointer/GNU atomic access, with no temporary-then-copy publication or atomic-object-to-`void **` cast.

Lifetime depends on retained process image/static storage and no unload, reset, unpatch or teardown. Identity, alignment and readable mapping do not prove arbitrary caller lifetime or exclude native aliases; nonparticipating writers remain outside the contract.

Each binding admits one attempt. Retain successful, partial and uncertain custody until process exit; never clear a published slot after another claim fails. Closing capture/init admission does not drain callbacks or permit reclamation. Do not reuse the C model's destroy API.

## Reserve first, validate actual addresses second

Keep Community cm → writer and Core writer-only, with no reverse edge. Both paths require late mode and a real nearby island; unavailable-island cases refuse without fallback. Real SDK caller-RA/loader/capability policy and its recursive cm mutex ABI remain outside the authored admission fixture.

The transaction has two preparation stages:

1. Under the writer, copy trusted fixture owner/admission values and reserve the target, binding, attempt journal and metadata capacity before resource calls. Preserve the target/prologue/late policies; no fictitious trampoline address or asserted relocation count may authorize publication.
2. Retain each actual mapping/slot as it is returned. Validate storage against retained targets, originals, emitted blobs and the immutable module input before relevant writes. After relocation, derive the exact instruction and literal spans from actual `ncopy`/`out`; finish binding, overlap and permit validation before target unlock.

For the selected route, island instructions are [I,I+8) and its literal is [I+8,I+16). Trampoline instructions are [T,T+4(out+2)); the following eight bytes are its continuation literal. With one copied instruction, `out` is 1 or 4, so its complete blob is twenty or thirty-two bytes. Attribute instructions only; reserve/validate complete emitted storage. Do not attribute a shared island pool, whole trampoline page, failed slot, literal or padding.

The private permit binds owner, attempt, binding, target, original, replacement, emission and immutable module inputs. All conflict/capacity/token checks precede target unlock.

## Bounded resource and protection receipts

Arm each resource call and capture its return immediately, before logging can overwrite errno. Distinguish attempted/returned, underlying-called and injected outcomes. Errno is valid only for an operation that defines it on failure. Keep maps open/read/parse/close categories separate; a parser refusal has no invented syscall errno. Cache-clear has no success status: retain the request and observed normal return only.

The four-pool allocator has six hints (inline_hooks.c:122–147). Existing pools supply a slot or are skipped; the first empty pool either finds a reachable mapping and supplies a slot, or exhausts six hints and returns null. Under this private, uncorrupted, serial invariant, reserve at most six island-candidate mmap receipts plus one trampoline mmap receipt. The bound excludes interposition/concurrent pool mutation.

Out-of-range island mappings are unmapped at 139: retain each address and cleanup return. Selected pools remain retained; used++ at 146 burns a slot before later failure. Pool/slot custody is distinct from attribution, and refusal does not imply all storage was released.

Trampoline allocation at 292 precedes seal's mprotect/munmap sequence (180–185). Retain allocation, seal return/immediate errno and cleanup independently; a null seal result loses those distinctions. Unlock-refusal branches also unmap at 322/331. Failed cleanup retains uncertain custody; successful cleanup releases only that mapping, preserving its receipt. No automatic retry/reuse.

Separate island and target unlock/relock receipts: attempted/returned, raw result/errno, `token_valid`, and successful token base/length/saved bits. Token contents are valid only after unlock returns 0 (azprot.h:135–138). Do not copy uninitialized contents on refusal. Preserve the island token before reused `pt` (inline_hooks.c:318/321/330) is overwritten.

Relock restores saved bits, not necessarily RX (azprot.h:141–145). After a warning, another successful unlock/relock can preserve writable bits. Actual map queries are separate evidence; disjoint blobs need not occupy separate protection pages.

## Checked page rounding

The current helper checks `addr + len` overflow at azprot.h:125 but then computes `addr + len + pg - 1` at 128 without a corresponding round-up check. The new successor must add a guard using the actual page size returned by that helper invocation's sysconf call.

Require positive, power-of-two page size; checked address-plus-length; checked addition of page-size-minus-one; and a nonempty representable rounded interval before locking or mprotect. Use that same checked result for the token and syscall. A separate outer page-size query is insufficient to claim the helper's own arithmetic is bound. If the retained 4096-byte trampoline recipe is kept, explicitly require the selected page-size compatibility before allocation; do not silently generalize it to arbitrary host page sizes.

Overflow controls use values before memory access, never invalid targets passed to prologue dereference. This is a proposed conservative refusal, absent from the frozen helper.

## One internal publication cut

All validation completes before the actual target unlock at inline_hooks.c:330. A successful unlock enters a fixed internal sequence:

1. Release-publish the fully initialized metadata record.
2. Store the actual trampoline pointer into the descriptor's genuine plain original slot with the retained installer ordering.
3. Publish the actual B instruction to the authored target and continue cache/relock handling.

This sequence has no provider, external callback, allocation, new lock, capacity check or ordinary refusal branch. Use already-bound storage and a private lexical permit; do not invoke the older public commit API and then handle an unexpected error. Retain the owner before either becomes visible. Immutable lookup payload contains only prefilled identity/ranges/target/storage; keep later mutable stage and warning journals separate.

Record the installer return separately from publication progress and protection warnings. Island relock failure at 327–328 does not prevent publication; target relock failure at 338–339 follows it. The installer returns 1 in either case (340–342), and community.c:1263–1273 translates that into SDK success 0 and hook accounting. A compatible adapter preserves that success while its richer receipt says PublishedWithWarnings. It must not advertise ordinary refusal, clear the original or invite retry.

Interruption after metadata visibility but before redirect can leave an attributable, incompletely reachable emission. Retain armed/unreturned stages and possibly acquired resources without inventing completion or cancellation. No signal/nonlocal-exit recovery, lock recovery or process-death persistence is added.

## Source transformation and focused follow-up

The proposed implementation must retain the pinned source and a precise generated diff. Preserve the instruction classifier, four-word prologue policy, reach policy, ADRP calculations/materialization, absolute-jump encoding and selected original-before-branch semantics. Keep frozen entry/helper/binder sources unchanged.

The installer orchestration, allocator/seal outcome capture, separate protection-token storage, checked page rounding and private metadata cut necessarily change. Name them as transformed source. The receipt-enabled result cannot be described as byte-identical execution of the entire frozen installer or protection helper. Any fixture syscall fault injection must separately identify returned synthetic errors versus calls delegated to real mmap/mprotect/munmap. Do not weaken policy to make a case pass.

The ten proposed cases in [adapter-plan.json](adapter-plan.json) are distinct from prior ABI/throw coverage:

| Case | New discriminator |
| --- | --- |
| owned-binding | Wrong, temporary, aliased or occupied original/replacement refuses before installer entry or allocation. |
| community-actual-emission | Real addresses and ncopy/out agree with metadata and permanent original before one structurally validated scalar call. |
| core-actual-emission | Typed Core uses the same late path and never becomes a community blame ID. |
| prepublication-refusals | Declared pre-call map/seal/island-unlock/target-unlock injection retains side effects and leaves target/original unchanged. |
| cleanup-uncertainty | Declared pre-call munmap refusal retains the actual mapping; no hidden unmap/retry. |
| partial-install | First published route and both journals survive second-binding refusal. |
| separate-protection-and-warnings | Distinct tokens, warnings and saved writable bits; invoke only after structural/RX checks. |
| admission-handoff | Queued-close/conflicting claims never enter installer; admitted publication accounts before close. |
| diagnostic-independent-result | Saturated diagnostic capacity cannot convert actual publication into refusal. |
| checked-rounding-input | Invalid arithmetic refuses on values before any target dereference. |

Out-of-range candidate cleanup cannot be guaranteed from mmap placement alone. Select a declared placement-input seam with an actually owned mapping, or deterministic trampoline cleanup; never invent a mapped pointer. Injected refusal evidence is not a real kernel-failure observation.

Five proposed controls preserve executable destinations and custody:

- Move the actual original store before metadata, keeping a real post-original metadata load at that observation point. Swapping numeric order labels alone is inadequate; no callback enters the cut.
- Extend only metadata's instruction endpoint into its literal; actual code stays intact.
- Misreport a failed cleanup as released, without performing another unmap.
- Copy the target token into island evidence only; real relock tokens stay correct. Compare full base/length/bits and outcomes, since both tokens may start RX.
- Misclassify published-with-warning as refusal, without clearing originals or reclaiming code.

Require exact named failures after bounded safe completion. No null branch, invalid pointer access, freed trampoline, executed malformed CFI or sanitizer crash is an oracle. All published code stays retained. Do not replay the old ABI, return-channel or ordinary-exception suites.

All cases remain proposals, with authored Core/Community admission. Signal safety, anonymous-PC unwind, instruction-fetch ordering, external loader/interposition, other patch families and general lifetime/race proof remain open. Preserve opaque action arguments, register-only service values and no pointee/post-return reads, interception or pairing.

All live, activation, writer/recovery, staging and product gates remain CLOSED. No core/package/catalog change, installation, emulator/device operation or stems-fix claim follows from this source-only plan.

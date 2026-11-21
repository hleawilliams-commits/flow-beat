# Ordinary-entry witness plan

This source-only plan defines a separately versioned `activation-ordinary-entry-fixture` using the frozen attempt/attribution engine and exact retained readers. Every live gate remains **CLOSED**. The predecessor at `4e134822cd4fbaf821092ac06cf69b22c024cfdd` executed only the serial fixture driver; winning ordinary `ac_run` remains compile-only. The new work will witness its synchronous ordinary return without introducing native providers.

[ordinary-plan.json](ordinary-plan.json) contains seven transformation groups, five future case groups and five exact safe controls. Ten normal selectors and five dedicated control baselines give fifteen candidates per mode, with five additional control processes per mode: forty future processes across plain and ASan/UBSan modes, with leak detection enabled for ASan/UBSan. These are planned counts, not executed checks. Exact per-selector assertions must be frozen before any future build.

## One ordinary entry after setup

The successor copies the engine into its own experiment and retains the single process-lifetime attempt CAS, private cursor, raw/working/completed ownership and acquire-gated public observer. The six generated reader fragments and receipt types are reused byte-for-byte from retained artifacts; the old extractor and SDK capture are not rerun. No frozen volatile-guard consumer is called or changed.

A new `AO_WITNESS_SETUP` configuration exposes only the five module/generated/cell setup functions. It separates their staging and one-use storage from fixture stepping, inspection, auxiliary counters, snapshot refusal and diagnostic lookup. Old `AC_FIXTURE` and all eight old control macros are rejected. The address-only reader evidence anchor remains uncalled.

The stage and cell APIs already require Virgin. Both existing commit APIs lack that check, so the planned successor adds the same relaxed eligibility load to each, before existing predicates or stores. Existing bounds, one-use flags and release publication stay intact. This is serial setup eligibility, not another admission authority, a process-wide writer protocol or concurrent exclusion.

All authored staging and selected publication finish before the first synchronous `ac_run`. Request arrays, module/generated values and integer-keyed frame cells remain stable through ordinary return. There are no in-call callbacks, scheduling probes or private API calls. After return, the harness records the actual return enum and calls only `ac_observe` for attempt state/payload. Suppression cases may then make a second ordinary call; setup-refusal cases attempt only pre-staged unused commits after the first return. No reset or retry is added.

## New witnesses and exact controls

The successful cases select a direct module, a generated binding zero resolving module three with a full 64-bit process identity and canonical 33-byte ID, or one authored frame after PC/LR misses. They establish the ordinary driver reaches completed publication before it returns. They do not repeat the old reader boundary matrix.

The no-owner case completes selection and one opaque authored UNBLAMED event while deliberately invalid unused I/O values remain irrelevant. A genuine null request and a returned write length greater than the requested length are admitted but halt incomplete: ordinary entry returns INCOMPLETE, public state stays ActiveIncomplete, and no payload is exposed. The exact private error/receipt cut remains a source fact, not a private runtime observation.

Representative ordinary returned failures cover open -1 and a short write followed by sync -1 and close 0. Those valid authored return values produce RETURNED and immutable completed receipts; short-write and known-failure flags can both be true. Native fd conversion, errno, EINTR, crash-file persistence and native unblamed behavior are outside this plan.

Terminal cases repeat entry after completed and incomplete outcomes, expecting SUPPRESSED and unchanged public snapshots. Caller-owned request/ID source storage may be mutated only after return to test copied completion. Previously staged unused module/generated slots must refuse both post-entry commit attempts. No further lookup follows those attempts.

The five controls each have a dedicated selector and exact singleton failure set:

| Mutation | Required semantic failure |
| --- | --- |
| Return after successful admission without driving | `ordinary-entry-drives-to-publication` |
| Stop ordinary driving at raw selection return | `selection-return-does-not-end-ordinary-entry` |
| Map completed publication to INCOMPLETE | `published-entry-returns-returned` |
| Map the halted null-input path to RETURNED | `halted-entry-returns-incomplete` |
| Omit both new setup commit phase checks | `post-entry-setup-commits-are-refused` |

The first two use composite actual-return/public-completion oracles without redundant infrastructure predicates. Return-mapping controls must still pass independent state/payload checks. The setup control pre-stages unused slots so no old staged/used predicate masks either omitted check; it publishes authored storage only, then stops without another lookup. All reports retain actual outcomes. Crashes, sanitizer errors, timeouts, unexpected diagnostics or extra semantic failures are refused.

Changing `ac_step_internal(false)` to `true` alone is masked by the outer loop and is not an accepted control. The raw-selection stop is established by its exact source transformation and method review; public observations establish only ActiveIncomplete/no payload.

## Retention and future implementation boundary

Fourteen pinned local inputs plus this plan, README and helper form seventeen inputs. Twenty-five line-byte anchors identify the selected source cuts. The helper differs from the retained predecessor by scope and path/name literals only. It performs no child commands, Git reads, compiler, loader, installer, matrix or device work. Nofollow reads, exclusive writes, exact source hashes and a closed file inventory preserve the packet. Refusing an existing public result prevents unchanged successful replay; it is not a general concurrent-retainer lock.

The first source/control review and retained-byte audit must bind these exact planned bytes before implementation. Copy the auditor before invocation and preserve its executed source. Structural audit assertions are not runtime cases. Prior successful retention, matrix and byte audits are reused as pinned evidence, not replayed. The earlier FINI_ARRAY diagnostic/adoption and empty-directory audit failure remain historical; future artifacts need their own narrowly scoped diagnostic review.

Before any later implementation build, freeze and independently review the complete transformation ledger, setup-only API and symbol surface, authored request/frame/file setup, exact reader copies, case assertions and safe-control failure sets. Standalone and setup-only codegen must receive fresh method/dependency checks. This plan creates no fixture implementation or executable result.

The single logical strong CAS can lower to an exclusive-monitor retry loop; bounded or wait-free machine admission is not proved. The retained engine/readers depend on libc copying/comparison and canary machinery, with additional sanitizer dependencies. No transitive libc/interposition, canary-failure or signal-safety proof follows. Per-slot acquires are not a coherent snapshot.

Actual callbacks/nesting, concurrent/native publication, native frame/file providers, DSO/installer invocation, arbitrary DSO/slot custody, nonparticipating writers, loader/interposition, handlers, signals/nonlocal exits, native visibility, quiescence, recovery, production and live observer composition remain deferred. No physical deck, emulator, official core, community package, release or quarantine policy changes. Ownership remains distinct from fault causation.

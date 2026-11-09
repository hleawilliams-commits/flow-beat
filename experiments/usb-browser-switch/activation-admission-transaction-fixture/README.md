# Offline C admission and transaction fixture

This separately versioned successor to the [integration review](../activation-generated-range-integration-review/README.md) at 3f9b8078e090f7b688d1902b261653a0fb130548 exercises authored admission, locking and publication transactions. It adds no production core integration, SDK adapter, repair patch or live observer. Every live, activation, recovery and staging gate remains CLOSED.

The new C17 module uses actual pthread mutexes and C atomic publication. Loader facts, caller return addresses, native target eligibility, mappings, protection results, cleanup outcomes and replacement values are synthetic. Passing this fixture does not establish actual SDK admission, native ownership or fault causation.

## Executed evidence

The first complete matrix passes **51 named checks across 12 fresh-process selectors per plain and ASan/UBSan mode**, plus seven exact safe controls in each mode. One additional standalone C object compiled successfully without fixture seams and was never linked or executed. There were no failed compiler or runtime attempts in this checkpoint. Seven inputs, 56 commands, 112 output streams, 110 artifacts and 342 files are retained privately.

[The exact named test contract](test-contract.json), [sanitized results](result.json) and [independent verification](verification.json) bind the new sources and retained commands. Each selector and control runs in a fresh process. The standalone C configuration without TX_FIXTURE is compiled only. All executed configurations enable explicitly authored scheduler and permit-corruption seams; this is not execution of the standalone configuration.

Compilation uses Apple Clang ARM64 macOS, C17, explicit generic ARMv8-A, O1, frame pointers and strict warnings. ASan/UBSan use abort/halt on error; leak detection is disabled. The source has lock-free type assertions, but this checkpoint does not inspect instruction lowering or establish Linux GCC/core ABI, signal safety or weak-memory publication. No TSan, SDK compiler, installer, firmware, emulator or physical deck was used. Earlier matrices and source captures were not replayed.

## Admission and ownership

The fixture's Community mutators acquire cm then writer. Core installation and the constructor-to-late transition acquire writer only. No writer-to-cm edge is introduced. The authored cm mutex is nonrecursive: the fixture excludes reentrant public mutation and is not a replacement for the pinned SDK's recursive g_cm_mu. No real protection lock is acquired; protection operations below are modeled outcomes.

A one-start loader guard closes before any external callback would run. Fixed module indices retain copied identities and explicit handle, link-map and intended-object provenance. A valid zero base succeeds; missing provenance refuses publication even when supplied endpoints look plausible. Endpoints are supplied min/max PT_LOAD envelopes, not parsed ELF program headers. The fixture performs no loader discovery and does not cover earlier constructors.

Module attribution publishes before descriptor/init outcomes. Each index has its own one-way acquire/release flag, so a ready higher index is not hidden by an unready lower index. First matching index wins. Later module publication can mask generated ownership, and immutable attribution survives descriptor/init failure. Mutable permission/bookkeeping fields are not read by lookup.

Only one modeled init epoch is active at a time. Community calls copy caller input values at their modeled outer boundary and revalidate epoch identity, open state, descriptor capability and caller envelope while holding both locks. They finish nhooks bookkeeping before releasing admission. Closing the epoch uses the same order: a delayed claimant ordered after close refuses; an admitted transaction completes before close proceeds. This is not a thread-identity restriction, callback drain or proof of authentic caller RA capture in the SDK.

Core ownership is typed and copied internally. The fixture assumes selected native target sites have already passed their native eligibility policy. It does not implement the SDK ID/caller collection, native text envelope, firmware/prologue checks, claim ownership or generic void** lifetime contract. Its original and redirect slots are fixed internal atomic integers; they are not arbitrary SDK output pointers or executable replacement custody.

## Transaction and resource retention

Eight fixed emission slots and 32 attempt receipts are owned by the fixture instance. Core and Community share target/capacity reservation under the same writer. Refusal occurs before modeled allocation for known conflicts and exhausted capacity. Unlike the older C++ metadata library, this conservative C fixture never reuses a reserved slot, including invisible failed reservations. That stronger availability policy is a model choice, not established production compatibility.

The authored plan supplies addresses and relocation counts; it does not run relocation or create generated instructions. A private permit binds the system, instance, attempt, slot, epoch, owner, target, original and replacement to one lexical transaction. Wrong-system, wrong-slot, wrong-owner and already-consumed permit faults are injected only before validation. They test private defensive checks, not an externally forgeable capability API or actual token reuse.

All fallible validation precedes the modeled target unlock. After successful unlock, the cut performs metadata release publication, original store, then redirect store without another provider, allocation, lock, fixture callback or refusal branch. Diagnostic loads/order fields observe this authored cut. There is no instruction-cache operation, real target write or claim that an external thread can safely call the original.

Mapping allocation/seal/cleanup and island/target unlock/relock results are retained separately, including raw result/error pairs. Burned island slots remain burned. A failed cleanup keeps modeled mapping custody; successful cleanup records release without freeing a published emission. Island and target protection receipts preserve their respective saved bits. Successful relock means saved bits were restored, which can include write permission; it does not imply RX. Postpublication warnings and failed/partial init retain copied identity, metadata and internal original/redirect values.

The model's explicit destroy API requires all participating readers/callers to have joined; it is test teardown, not a live reclamation protocol. Epoch exhaustion, pthread initialization/destruction failures, arbitrary signal/nonlocal exits, disable/remove/Reload and a real loader's lifetime remain outside executed coverage.

## Schedules and safe controls

The queued-close schedule stops a claim before admission, closes its epoch and then releases it. The admitted-close and phase-lease cases use ordinary threads plus fixture-only trylock observation: a real EBUSY is recorded before the unchanged blocking mutex acquisition. This proves the selected contention and completion order in this harness. It is not an interrupted signal reader, native quiescence, fairness or general race/lifetime proof.

The seven control configurations skip both epoch-open checks, omit successful hook accounting, admit fallback after late transition, accept deliberately invalid private permits, accept missing module provenance, stop module scanning at an unready index, or store the original before metadata. Each is accepted only when its selected report fails the exact ordered named checks in the test contract, exits 1, and produces no stderr. Candidate reports must pass every declared check and exit 0.

Controls keep locking, valid C memory accesses and safe teardown intact. The epoch control changes both admission and final permit revalidation; it is a two-site configuration selected by one macro. No baseline assertion is weakened. The standalone object and fixture variants are separate configurations, and raw build outputs stay private.

## Review and limits

Independent source reviews corrected draft harness setup before execution: hooks must be configured before loader start, failed provenance cases need distinct IDs, and descriptor rejection returns TX_DESCRIPTOR. Entry input copies were added before freeze so the admitted transaction never rereads public plan/token values. Runner review preserved initial timeout output on a later drain failure and corrected the two-site epoch-control description. None of these was a discarded executed failure. A separate retained-byte audit passed on its first invocation across all 342 retained files, without importing the runner or replaying any compilation, selector or old suite. It also identifies 16 ARM64 Mach-O executables with matching dSYM UUIDs and a standalone object without the fixture-hook symbol; these structural checks do not prove machine-code/debug semantic equivalence.

The audit establishes retained byte consistency, command/report relationships and exact source/configuration receipts. It does not prove binary semantic equivalence, actual external operations from a receipt alone, authentic loader provenance or general concurrency correctness.

No previous source or evidence is replaced. The frozen Waveform refusals, publication reconciliation, quarantine counterexamples, x16 tail-transfer exception, installer x17 and anonymous-unwind limits remain. No native pointee or post-return reads, return interception, TLS pending state or cross-entry pairing is introduced.

Next scope an offline composition with the exact retained C installer, so actual relocation output and allocation/protection/cleanup receipts can replace the authored plan. That work must first specify permanent original/replacement custody and the adapter's prepublication failure boundary; this fixture's internal integer slots and conservative reservations cannot supply those native contracts. Preserve the completed matrix instead of replaying it unchanged. Nonparticipating patch families, external loader/interposition and signal/nonlocal-exit behavior remain separate blockers to any live composition.

All passive attachment gates still require exact live image/admission/current hook ownership, stable originals and code, accepted control-flow scope, actual writer/application/supervisor/QEMU topology and stopped recovery. Local Library row/provider/readiness, arguments, operation owners, alternate destructive/queued paths and caller timer lifetime remain activation gates. Stems staging remains closed for its unexplained historical failure. No release or physical installation/timing claim follows from this host fixture.

# Native-provider admissibility review

The ordinary-provider fixture now has bounded evidence for authored callbacks and same-thread nested losers. It does **not** establish a native provider boundary. The selected SDK persistence calls run inside fatal-handler paths, while the fixture runs an ordinary synchronous caller with authored owner/frame values. This source-only review maps that gap. Every live gate remains **CLOSED**; ownership is not fault causation.

[The machine-readable review](provider-review.json) pins reused local sources and line-byte anchors. [Source review](source-review.json), [retention](result.json) and [verification](verification.json) distinguish source findings from future requirements. No SDK source is fetched, no fixture or previous audit is replayed, and no compiler, callback, native provider, signal, loader/installer, emulator or device is run.

## What the existing evidence establishes

The [frozen ordinary-provider fixture](../activation-ordinary-provider-fixture/README.md) supplies one terminal admission before request/provider fields, a fixed table/context through ordinary return, copied owner/write arguments, full int64 accepted-open tokens, and public payload only after release/acquire completion. Its 60-process matrix and corrected audit remain frozen, including the failed first auditor and authoring-custody limits.

The six exact retained reader/type fragments (two layout/storage fragments and four functions) still use authored module/generated/cell setup. Their bounded lookup does not give custody of a native stack or a coherent snapshot of all slots. The tested callback journal records a prepared return before the callback returns. Source and selected codegen distinguish that from the caller's private raw capture, subsequent authoritative record and final public completion. No private intermediate payload was observed.

The preserved SDK consumer is different: it sets a plain volatile attempted-once flag before lookup, uses native frame reads, and calls native persistence. Both selected call sites—`cm_crash` and `cr_handler` after its per-TID claim—are fatal-handler paths. The source comments' signal-safety descriptions do not resolve the documented installation, stack, previous-action, dependency or concurrent-entry gaps.

## Callback-to-native mapping

| Callback | Selected native work | Required before an adapter |
| --- | --- | --- |
| Open | `int fd = open(CM_BLAME, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)`; nonnegative fd enables later calls. | Native descriptor provenance and checked int representation; stable path/file/context custody; immediate raw/error capture. |
| Write | One write of the selected ID, with an empty comparison body. | Copied bytes alive through return; checked return representation; retain short/error outcomes without retry. |
| Sync | `fsync(fd)`, outcome discarded. | Same owned descriptor, exact raw/error capture, and a limited file-sync claim separate from directory/crash durability. |
| Close | `close(fd)`, outcome discarded. | Explicit failure/unknown custody and no blind retry, reuse or reclamation from a numeric token. |
| Unblamed | Live-mod gate, append open, unchecked clock, decimal timestamp, write/sync/close; outcomes discarded. | A separate native operation/outcome contract. The current void callback records only opaque ordinary return. |

The engine's nonnegative int64 OPEN domain is intentionally wider than an `int` descriptor contract. Zero is supported. The authored wide-token success is not proof of fd provenance or permission to truncate to int. A future native binding must establish its actual ABI and checked conversions; narrowing cannot be justified by the fixture's domain check. Correct representability alone also does not exclude a foreign close, descriptor reuse or a changed underlying file.

For WRITE, the engine accepts only -1 or a count through the requested length; SYNC and CLOSE accept 0/-1. These are receipt-domain predicates, not provider guarantees. An invalid returned value halts the terminal attempt, potentially after earlier provider side effects. A missing reached callback similarly halts without exposing incomplete working payload. With real resources, that state needs independent custody; the engine has no cleanup/reset API.

Before a native OPEN, the fixed table must establish complete stable functions for its reachable WRITE/SYNC/CLOSE path and valid result-conversion contracts. This is a provider prerequisite, not permission to change the engine's reached-only validation or add another admission. Synthetic conversion/custody refusal must stay distinct from an actual native -1/error pair.

A future provider must capture the raw return and, on failure, its error value immediately before logging, journaling, nested work, normalization or another native call. The by-value pair then preserves that captured value through the engine. Today's authored sentinels are not native errno capture. Caller-entry error-state preservation is a separate undecided contract; an adapter cannot claim it merely because each failure has a copied error field.

A returned short/failed write still leads to sync then close in the engine. Completion says all selected calls returned and their receipts were recorded, even when known failure is true. It does not acknowledge durable blame, directory metadata, a closed descriptor after every error, bounded latency or recovery. An entered operation with no recorded return stays unknown. This review makes no platform-specific assertion about interrupted close; that contract must be established before implementation, without retrying or reusing a possibly released token.

The native consumer performs one write. The separate `sig_write` helper retries in another reporting path; substituting it would change the selected boundary. The fixture's canonical identity syntax is also broader than SDK `cm_id_ok` (which accepts 3–32 lowercase/digit/hyphen characters and excludes reserved names). Copied immutable identity is not native SDK admission.

A conditional source counterexample separates persistence from attribution: if the single write of an ID such as `foobar` leaves only `foo`, and the next startup reads exactly that prefix while a distinct `foo` record is present in `g_cm` at startup, `cm_find` can match the shorter ID and create its crashed marker. Both names satisfy the selected SDK syntax. This is conditional source reasoning, not an observed short write, startup, fault or causation claim. It justifies preserving short-write receipts and refusing to equate completed ownership with correct persisted blame; this review changes no persistence or quarantine behavior.

## The unblamed branch is a policy boundary

`cm_note_unblamed_crash` is not a no-op counterpart of the authored void callback. It reads plain `g_cm_live`, opens CM_CRASHES for append, converts unchecked `time(NULL)` to an unsigned value, formats it, writes, syncs and closes. Normal void return exposes none of those outcomes. An engine UNBLAMED completion with no known-failure flag therefore cannot acknowledge native append success.

The retained startup code consumes CM_BLAME to mark one mod crashed and consumes CM_CRASHES timestamps to suspend community mods. These paths are safety-policy state. No experiment may borrow their names, clear them, normalize them or turn copied owner attribution into fault causation. Existing quarantine and all startup protections remain unchanged. The current callback shape cannot separately express clock/open/write/sync/close receipts for this branch; it must remain opaque until a distinct reviewed contract exists.

## Context, lifetime and dependencies

The main reporter's per-TID fence applies after its own successful claim; it is neither the fixture's terminal admission nor cross-thread serialization of the original volatile consumer. The separate community handler and nonparticipating callers do not inherit that fence. Direct chained callbacks retain the TID slot until ordinary return. The previous-action publication interval, ignored installation outcomes and fallible alternate-stack setup remain unresolved. The fixture's guarded nested loser repairs none of these paths.

The retained `crash_peek8` source explicitly limits its intended use to fatal-handler reporting. Its Boolean exact-eight result discards raw native return/error and supplies no coherent frame pair or lifetime. It is not selected here as an ordinary frame adapter.

A copied callback argument lives through the synchronous call; a callback cannot retain its address. Table, code and context must remain valid through every admitted operation. No callback drain, unload, signal/nonlocal-exit cleanup or reclamation follows from capture closure or completed ownership. Native fd custody is a separate obligation from those pointer lifetimes.

The retained GCC codegen review shows a retrying LL/SC admission and normal libc/canary/sanitizer dependencies. It does not establish bounded/wait-free admission, dynamic binding/interposition, transitive libc behavior, cancellation, fatal-stack execution or signal safety. This review inspects no new machine code and supplies no native timing or filesystem measurement.

## Smallest defensible future boundary

The next defensible candidate is an **ordinary single-thread disposable-file witness**, separately planned before implementation: fixed callback code/table/context and one exclusively owned temporary directory/file capability; exact native open/write/sync/close returns and immediate error capture; authored immutable owner/module/frame inputs; and explicit terminal cleanup custody outside the attempt. Real CM_BLAME/CM_CRASHES, live-mod clocks, native frames, handlers and quarantine remain outside it. Its value would be ordinary provider/receipt correspondence only, with no crash durability, native attachment or recovery claim.

Before even that candidate can run, pin the target ABI and binding, permitted file operations, descriptor provenance/ranges, path and external-writer exclusion, callback/argument lifetime, entry-error policy, close-error/unknown disposition and no-retry rule. Specify how provider journal evidence is separated from callback return and engine receipt, how fresh processes/files prevent reset, and how stopped/nonlocal paths remain excluded. This review implements no adapter and authorizes no native I/O.

Next make a separately scoped source-only ordinary-file custody plan. Specify a fixed provider table/context and an owned temporary directory/file capability for one ordinary synchronous entry using the frozen engine and exact readers; keep frame cells and owner inputs authored. Define native int descriptor provenance and checked widening/narrowing, value/error capture before instrumentation, entered/returned/recorded/completed receipts, close uncertainty and terminal no-retry behavior, plus explicit cleanup custody outside the terminal attempt. Use fresh process/file identities and bounded exact controls before any implementation. Preserve the opaque unblamed boundary; do not invoke CM_BLAME, CM_CRASHES or native quarantine/startup policy. No adapter, native I/O, compiler, runtime matrix, callback/nesting, handler/signal/nonlocal exit, concurrent publication, DSO/installer, emulator/device, production or live work is authorized in that plan slice. Preserve all frozen sources/failures, LL/SC/libc/canary limits and every closed live gate; do not replay completed retention, audits or matrices unchanged.

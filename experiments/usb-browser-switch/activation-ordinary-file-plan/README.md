# Ordinary-file custody plan

This source-only successor to the [native-provider review](../activation-native-provider-review/README.md) defines an isolated ordinary disposable-file witness. **No adapter, native provider I/O, compilation, callback or runtime matrix is implemented or executed here. Every live gate remains CLOSED.**

[file-custody-plan.json](file-custody-plan.json) specifies seven future source groups, five case groups with eight ordinary selectors, six control baselines and six singleton controls. Plain and ASan/UBSan modes would use **40 fresh processes**, with leak detection enabled. These are planned counts, not coverage.

## Fixed engine and owned capability

Reuse the frozen ordinary-provider engine/header and six exact reader/type fragments: two layout/storage fragments and four functions. The five setup-only functions, sole terminal admission, ordinary driver and acquire-gated immutable completion remain unchanged. Module, generated-owner and frame-cell inputs stay authored. New provider code has a fixed complete table/context that remains alive through the synchronous return. A setup precondition establishes complete reachable callbacks; it adds no engine admission and changes no reached-only check. Unblamed remains an opaque authored void operation.

A future parent supervisor inside the same pinned isolated Linux instance uses explicit fork/exec inheritance of the directory and preopened report descriptors, with a reviewed allowed-fd list and close-on-exec handling. No host fd integer is passed through a container command. The 40-process count denotes fresh exec witness children; supervisors and build/inspection commands are separate. The supervisor never invokes the attempt engine.

Each future child receives a declared capability for a fresh parent-owned private directory, its native identity and the literal single-component name `witness.dat`. IDs cannot select paths. The parent retains an independent directory capability. The planned file call uses `openat` with exclusive creation, no-follow and close-on-exec, mode0600, without truncation. This differs deliberately from the SDK crash-file policy. The collision case starts with a parent-owned sentinel; the readonly case creates the owned file read-only to obtain a real write refusal. No SDK blame, crash history, startup or quarantine path is touched.

A successful native int descriptor enters held custody before any validation/journal helper. The future provider checks descriptor and fixed-name identities through its capability, retaining query outcomes. Validation failure keeps the descriptor held for external cleanup and supplies a tagged out-of-domain OPEN value to halt the unchanged engine; it is infrastructure failure, not a successful case or an actual native OPEN error. The isolated writer assumption is explicit; identity checks are not a coherent external-writer exclusion protocol.

Before every write/sync/close, the full int64 token must lie in the native int range, equal the saved successful descriptor and remain in verified held custody with no previous close attempt. Only then narrow it. A returned fd zero is valid; this first plan does not force or claim native-fd-zero coverage. The deliberate high-token control never sends a corrupt descriptor to native I/O.

## Returns, completion and cleanup

A separately named leaf captures each native raw return and failure errno before instrumentation, journaling or another call. Success carries no valid error. The later bounded errno-clobber probe tests consumption of the saved pair; it does not independently prove a missing initial capture. No preservation of caller-entry errno is promised.

Keep native entered/returned and error origin distinct from callback prepared-return, actual ordinary callback return, private engine raw capture, authoritative receipt and public completion. Only existing public observers are used. A prepared journal cannot establish that the outer callback returned or that the engine recorded it. Supplied refusals and invalid-value seams retain their actual native outcome separately and are never relabeled native errors.

Write occurs once without retry or completion fill-in. Returned failure/short-write still advances through sync and close. One successful sync is not file/directory crash durability. A close is marked entered before the call; returned0 is closed-returned, while returned-1 remains unknown regardless of platform-specific release behavior. Never intentionally inspect, operate on or retry the saved token afterward. This does not claim that the OS or transitive runtime never reuses its number. All explicit child report/output descriptors are prepared before entry; no further explicit child fd acquisition occurs afterward. Entered-without-return is also unknown and is not executed by these ordinary witnesses.

After the entry evidence is frozen, a separate child cleanup may close once only a descriptor still held for which no actual close entered. Its result is separately captured; failure remains unknown. Confirmed child termination precedes parent readback through the parent's directory capability, never the child's fd number. The read-only observer open uses no-follow and close-on-exec; check its descriptor identity before any read. Bound readback to65 bytes, check identity/type/link/size, and retain its own raw outcomes. Observed bytes prove selected post-exit content only. Cleanup unlinks only the fixed verified owned name and removes the exact owned empty directory; unexpected entries/errors remain explicit outstanding custody. No recursive deletion or repair retry is planned.

## Cases and controls

The eight ordinary selectors cover direct/generated authored ownership with real file work, exclusive-open collision, a readonly write error followed by sync/close, separately supplied pre-call close refusal and invalid write-result seams with external cleanup, opaque no-owner with no provider file work, and a post-completion suppressed repeat after actual caller-owned input mutation.

The six exact controls respectively consume ambient errno after saved capture, corrupt only the reported token, write same-length decoy bytes, omit native sync, omit native close and make one extra bounded owned write. Each has one designated composite oracle owning its affected downstream fields. Other assertions must not duplicate those expectations. The token control owns its resulting empty-file content/size difference as well as downstream receipts and cleanup. Safety predicates remain active, raw and supplied outcomes stay separate, and no control uses a stale/foreign descriptor or double close. The extra-write control permits exactly one extra owned call, not a retry loop.

## Review, retention and limits

Independent source/control review precedes the first retention. The new helper only copies pinned local source/evidence and line-byte anchors to an exclusive private packet. A separate frozen auditor checks that packet without invoking the helper, compiling or running a matrix. Documentation retention is ordinary local artifact handling, not execution of the planned native-provider boundary.

Future implementation must first freeze its complete source ledger, exact reader/header custody, checked conversions, provider/caller/context lifetime, callback/method ordering, isolated target ABI and libc bindings, finite journal capacities, exact singleton failure partitions and independent cleanup ownership. Unexpected native outcomes remain retained failures rather than being normalized to the planned expected case.

All previous bytes and failure packets remain unchanged. No successful retention, audit, capture or matrix is replayed. LL/SC admission retry and libc/canary/sanitizer limits persist. No general callback lifetime/quiescence, concurrency, signal/nonlocal exit, arbitrary DSO/interposition, native frame visibility, nonparticipating-writer exclusion or recovery is proved. No firmware, emulator, device, production or live composition is authorized in this slice. Quarantine and official protections remain unchanged; ownership is not fault causation.

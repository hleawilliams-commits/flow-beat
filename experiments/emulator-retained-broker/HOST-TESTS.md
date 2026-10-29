# Focused retained broker lifecycle evidence

Two fresh matrices pass: primary and one independent invocation. Each runs
14 fixture selectors and one separately linked production pthread-wrapper
smoke in each plain, ASan/UBSan and TSan mode. Each selector/smoke has one final
compound functional check. Twelve strict compiling controls per mode fail
exactly their selected functional oracle. Both matrices and the retained-file
audit passed on their first invocations. No unchanged parent suite was repeated.

[Primary](host-result.json) and [independent](host-result-independent.json)
results are byte-identical. The [comparison](comparison.json) records their
retained-source/log audit. Both result files have SHA256:
661ad9dff15b10d2802bdef83792d79c637ace87f0eed4db2f57e1ecc6da9246.

Compiler: Apple clang 16.0.0 (clang-1600.0.26.6), arm64-apple-darwin25.2.0.
Each invocation records 54 clean compiles, 42 clean links and 81 executions.
All translation units use -Wall -Wextra -Werror without warning suppression.
Controls require exit 1, their exact named failure and exact check summary.
Compiler/linker diagnostics, setup assertions, sanitizer reports, timeouts and
extra output cannot satisfy a control. No Linux ARM64 build is claimed.

## New cases

| Selector | Focused observation |
|---|---|
| allocated | Copied backend/expected values survive caller mutations; owner is returned before any sync or backend call; validation/export refuse before initialization. |
| init-arm | Same-thread wrapper-entry inspection sees the published owner, exact staged intent and prior acquired resources; successful calls execute actual pthread methods. |
| init-failures | Pre-call scripted attr-init, settype and mutex-init errors retain raw results and earlier initialized resources, with no implicit cleanup or backend call. |
| validate-arm | Snapshot entry sees retained owner, initialized mutex/attr and armed validation; callback output begins zero. |
| validation-failure | Partial 83-byte or full semantic output followed by scripted EIO remains captured, with resources retained and no export. |
| mismatch | Changed final frame byte, second instance half, selected mode or invalid CRC length prevents Ready while retaining returned values. |
| ready-repeat | Repeated initialization/validation makes no calls and leaves the journal unchanged; repeated export borrows the same Ready core. |
| dispatcher | Actual frozen wire/dispatcher and broker operations perform adoption and FRAME over synthetic backend state; lifecycle evidence stays unchanged. |
| dispose-empty | Empty/invalid-report handling makes no synchronization calls; allocated-only disposal consumes storage without destroying uninitialized resources. |
| dispose-init-failure | Disposal destroys only resources actually initialized before each scripted setup failure. |
| dispose-ready | Disposal publishes intent before mutex/attr destruction, preserves mutex-before-attr order and consumes the owner only after success. |
| mutex-failure | Pre-call scripted EBUSY keeps owner, mutex and attr, reports raw failure and permanently closes setup/export. |
| attr-failure | Pre-call scripted EINVAL after actual mutex destruction retains owner/attr; a separately justified fixture retry skips the destroyed mutex. |
| disposal-history | Explicit scripted EBUSY then EINVAL mutex refusals preserve the first report and replace the latest; initialization/validation failure evidence also survives. |
| real-sync | Separate executable links production sync.c and exercises actual pthread initialization, ERRORCHECK setup, broker lock/unlock and destruction without injected results. |

The fixture replaces only new synchronization wrappers. Successful paths call
actual pthread; refusal scripts return before calling pthread. They do not
reproduce genuine busy-thread destruction, interrupted outcomes or contention.
The actual frozen broker operation lock/unlock calls are never replaced.
Backend snapshots/submissions use synthetic memory, with no descriptor or native
driver. Dispatcher construction makes no extra snapshot; the fixture separately
observes lifecycle validation, adoption and one submitted frame.

Inspection during wrapper/callback execution is same-thread synchronous evidence,
not proof of concurrent access or quiescence. Fixtures retire borrowed uses,
copy observations, clear known pre-call scripts and explicitly dispose remaining
resources under test-owned quiescence. This cleanup is not real recovery authority.
The production smoke refuses an automatic second disposal when an owner remains.

No allocator-failure injection or private counter mutation is used. The
UINT64_MAX disposal-attempt branch is source-reviewed only. One exact-tail
control discriminates full-frame comparison; the suite does not claim exhaustive
mutation coverage of every inherited state predicate.

## Discriminating controls

Every control alters only new broker_owner.c in a private variant directory.

| Control | Oracle selector |
|---|---|
| initialization-arm-is-late | init-arm |
| discard-settype-error | init-failures |
| validation-arm-is-late | validate-arm |
| discard-returned-state | validation-failure |
| mask-final-snapshot-byte | mismatch |
| export-before-ready | allocated |
| omit-destroy-arm-publication | dispose-ready |
| discard-destroy-returned-flag | mutex-failure |
| replace-first-disposal-report | disposal-history |
| omit-owner-slot-consume | dispose-ready |
| omit-permanent-disposal-closure | mutex-failure |
| validate-ready-again | ready-repeat |

[Controls](controls.json) preserve exact source changes and failure labels;
[inventory](test-inventory.json) binds selectors and counts. The Ready-repeat
control actually calls snapshot twice. The final-byte control masks only byte151
for equality after raw evidence capture. The slot-consumption control frees
normally and fails the consumption oracle; its chosen selector clears its local
dangling slot based on the release report before fixture cleanup. It is never
run through the selector that immediately repeats disposal. Controls keep actual
initialized-resource destruction and held-resource transitions safe.

## Source provenance and retained audit

Each fresh private run captures ten new inputs and six frozen parent inputs.
The generated broker core is the exact prefix before mb_create plus the exact
suffix from mb_adopt, excluding only old create/destroy bodies: 4,936 and 7,117
bytes respectively. Combined SHA256:
4bfd81f15e656db6633c938556b013fdb55f27499960e6e26ceed8fd3cf3a2c3.
Ten generated inputs include that core, frozen headers/wire/dispatcher and the
new owner/wrapper sources. Frozen parents are never edited.

The private retained-file audit independently reconstructs the core by line
boundaries, copied files and all 12 variants. It checks captured/frozen/generated
inventories, exact bytes, every compile/link/execution log and public result
copies without rerunning tests. Its SHA256 is 8e37175db3d253c03a2ccd2bef06b85d6170a55638b8d28648be6a0753cefdc7.
Comparison counts include all matrix and audit attempts.

Before either matrix, independent review checked safe control cleanup, narrowed
resource-destruction wording and made the production smoke avoid an automatic
retry after retained disposal. AST and optimized-Python refusal checks preceded
run-directory creation; they were not failed matrix attempts. Historical parent
failures remain unchanged.

Raw builds, logs and the audit script remain ignored under private/. This
checkpoint supplies an observable local broker-resource result under external
serialization/quiescence preconditions; it does not establish those preconditions
or own backend context. Earlier borrows are permanently invalid after disposal
starts, even if attr destruction fails after mutex success. No prior broker alias
may then be reused. A later explicit attempt needs renewed independent
quiescence and outcome resolution.

The full CLAIM/adapter/broker/dispatcher graph, owned transport/supervisor binding,
external writer exclusion, native baseline/stop authority, callback deadlines,
complete overlay/both-file recovery and durable/process-death custody remain
open. No endpoint, emulator restart, physical control or installation occurred.
No core, package, catalog or release changed; all existing runtime gates remain
CLOSED.

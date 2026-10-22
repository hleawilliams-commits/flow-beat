# Fresh action and host recovery preparation

The task observer remains **uninstalled**. This follow-up adds a pure full-frame
policy and private preflight/host-recovery components, closes two installation
checks, and removes the obsolete input adapter. It made no emulator or physical
connection, sent no input, changed no controller file outside temporary test
fixtures, and changed no service, core, package or catalog. Native task
observations and event ownership remain absent.

## Full-frame policy

The [pure policy](action-policy/README.md) passes **245 checks including 222
strict rejection controls**, independently reproduced with identical result
JSON. Two 152-byte frames must each have a valid prefix CRC and the same
bounded counter. All other bytes must match the reviewed neutral plan except
the two CRC bytes. This includes the 24 bytes omitted from the earlier
128-byte read-only diagnostic. Strict host settings parsing permits only the
pinned driver's mixer fields and rejects held controls and arbitrary offsets.
The kernel's initial placeholder is different and is rejected, not normalized
by an input write. These rules prove neither freshness nor native consumption.

## Host counter ownership and recovery

The private helper supports one host-only alignment, up to two individually
completed BROWSE actions, one CW detent and one CCW detent. Each new action
requires fresh run/process/action-bound evidence and its expected starting
counter. Knob targets are atomically written before the first frame. Snapshot
bytes and mode are preserved independently from guest recovery, including a
run that never aligns or sends input. The helper performs no guest calls and
cannot itself send a controller frame.

The final actual-helper matrix passes **959 total checks across 179 scenarios**:
176 candidate scenarios and three strict mutated-control scenarios with their
expected failures. Independent execution produces identical result
`480afa6569863a77629a435b08d2fbb733927d2ebc307a634ea26613a339f19c`.
An additional read-only peer review covers the final source hash in the
[sanitized result](action-preparation-result.json). Mocks use temporary host
files and supplied evidence; they do not prove acquisition or device behavior.

Review found and corrected restoration gaps in earlier candidate versions.
An action intent alone cannot authorize its target. A persisted, bound rename
attempt establishes only its two possible endpoints; a verified replacement
narrows ownership to the written target even if a higher-level receipt failed.
Historical alignment/action/original values are not all retained as acceptable
current states. An already completed restoration still verifies exact original
bytes and mode and refuses subsequent drift. Stored receipts use intrinsic
time ordering during restoration, so a reboot's different monotonic epoch does
not block recovery. Fresh action grants still require current age checks.
Earlier passing matrices and source snapshots are retained as superseded
candidates, not validation of these final rules.

The guarantee assumes one coordinator and no unobserved external writer.
Content/inode checks followed by rename are not a filesystem compare-and-swap;
a matching value alone cannot establish provenance against concurrent writes.
The future action coordinator must retain exclusive session ownership.

## Fresh acquisition component

The private collector obtains status, full frame, full frame, then status in
one attempt. It retains fresh driver identity checks around each frame, exact
raw bytes, original-core/empty-process checks and both completed status
receipts. The empty marker now requires its exact four-line shape, matching
current PID and publication age of 0–10 seconds against a later uptime read.
It is still a sampled loaded-track marker, not authoritative pause or accepted
LOAD intent for stems recovery.

Each permit exists only in the collecting process. An exclusive active receipt
prevents a second preflight from issuing another permit. Consumption is once
only, checks the two raw frames and four completion receipts for drift, and
requires the same run/process, unchanged host mixer file and counter age within
two seconds. The complete collection has a 15-second ceiling. One 600 ms wait
after the initial status permits a normal worker poll; there is no sampling
retry. Those are refusal bounds, not measured runtime timing guarantees.
Frame acquisition/finish timestamps must lie within their enclosing intervals.

The private mock review passes **660 checks including 77 rejection controls**,
independently reproduced with identical result
`59c909fa7a4b3ca5028cd1a0179f11b1a587a352a9149e36b2a7c47f31c3f991`.
It executes the actual coordinator functions, progress predicate and pure frame
policies with fake transport/clock and retained temporary receipts. The full
scalar sampler and driver identity collector are mocked here; their earlier
results remain separately scoped. An early mock lacked the new sleep operation; another used a colliding
result filename. Both failures are retained and the harness was corrected.
Review also detected that modifying a saved frame after validation did not
change the copied permit values but went unnoticed at consumption. The final
component rejects that evidence loss. This check covers the two raw frames and
completion receipts; it does not rehash every scalar/metadata file at use.
Sequential observations are not coherent, descriptor-bound snapshots or proof
that capture remains Ready until input. The component sends no input.

## Installation and coordinator gates

An actual pinned approval-table extraction passes **136 host checks**: adding
an observer after 63 approvals preserves them, while adding it after 64 evicts
the oldest. The new conservative preflight requires space without rewriting
any prior approvals. It also requires exact success lines for each intended
hook, once and in claim order, plus the exact initialization/load evidence.
The previous refusal substring missed the core's actual `claim_hook` format.
Loading evidence remains distinct from capture readiness.

The two private helpers pass **43 checks, 29 rejection cases and two strict
controls**, independently repeated with identical extracted functions and
assertion results. The full-run source hash differs because the second review
also contains the subsequent runtime gate. The independent result therefore
has a different hash; this is not described as byte-identical full evidence.
A separate **35-check** review executes the closed gate and exact empty-marker
caller, including stale/PID-recycled/loaded/unknown controls.

Installation and input are now hard-disabled before connection. A marker file
cannot enable them. The obsolete combined-BROWSE/two-detent/RAW alignment calls
are removed; diagnostics and selective restoration remain separate. The final
coordinator must still compose permit consumption, host ownership and exact
first/release payloads, serialize the entire action, and exercise transport and
receipt failures. A planned neutral release needs one attempt even after an
uncertain press; it must retain same-process and host/mixer ownership checks
without imposing continuing Ready or permit age. No press/release retry,
capture rearm or input to fill missing records is allowed. Component passes do
not establish this composition.

Only after that work passes should a fresh selective baseline and bounded
passive runtime observation proceed. Preserve the earlier read-only diagnostic
and all completed emulator restorations. Stems staging remains closed, MODS
0.1.3 remains unpublished, and USB activation and physical validation remain
unresolved. This checkpoint is not a release or a stems recovery fix.

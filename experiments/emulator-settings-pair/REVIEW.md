# Independent settings-pair review

No blocking issue was found within the component's explicit external-exclusion
and ACK-authority assumptions. This review covers the inactive Unix byte-pair
engine, its source preparation, focused filesystem fixtures and eight strict
controls. It does not approve a live settings factory, mapper, `SettingsSink`,
restoration coordinator or native teardown.

## Reviewed source and contract

The frozen production source is
`8246b4782c1108d8fc2821abe6f2ad01a57350b41b85d741859b60f9c10a9d2d`.
The final fixture is
`a7de908b8b654dc6821cc8d9bf6de028ceacbdba352e42ef78d58a99034a361f`;
[controls.json](controls.json) is
`5b62466ac987eacaa9ce7c1ec9b2d79689bdcda373832ecbec8651f17121bbd0`.

The source is consistent with [CONTRACT.md](CONTRACT.md):

- A consumed directory descriptor binds relative operations to that directory
  object. All target basenames are fixed. Temporary names use the supplied
  namespace/request identity and exclusive creation; collisions are not
  overwritten or implicitly removed.
- The read path checks non-followed name metadata, opens with nofollow and
  nonblocking flags, validates the opened regular single-link file, bounds its
  contents, and checks fd/name metadata again. Existing ordinary owner/group
  and permission requirements are preserved or refused, rather than silently
  changed to fit a replacement.
- Both target byte strings and the complete ACK are retained before mutation.
  Per-file armed stages precede potentially visible operations. A later error
  preserves the transaction; a panic can leave `Pending` with no terminal error
  recorded yet, but a subsequent commit refuses that pending state before
  another mutation. Pending is not a completed or reopened transaction.
- Original images remain separate from the most recently verified images and
  latest transaction. The current pair may be mixed after a partial commit.
  Rename visibility, directory synchronization and final readback are separate
  observations. Verified replacement identity is the new inode, not a claim to
  have restored the original inode.
- Exact completed duplicates perform read-only current-pair verification;
  conflicts preserve the prior transaction, and incomplete duplicates cannot
  retry the interrupted filesystem work. Poisoned evidence stays diagnostic.
  The independently retained evidence handle keeps the same directory/journal
  after the writer is dropped; final handle loss is not recovery.

These checks do not eliminate check/open/rename races against excluded writers,
make the two renames atomic, or prove that an external pathname still names the
retained directory. The actual directory-renaming test demonstrates the last
limit: operations continue on the retained object while the replacement
pathname remains untouched. The engine's caller must establish ownership and
exclude readers requiring a coherent pair as well as competing writers.

[WRITER-BOUNDARIES.md](WRITER-BOUNDARIES.md) separately pins the original preview,
driver, boot and test writers. The newer optional owned path retains its existing
legacy-write guards; main remains inactive. Kernel overlay ownership, an owned
worker join and a local file mutex do not exclude independent host-file writers.

## Failure history and final fixture scope

This was **not** a first-pass full-matrix success. The first primary run passed
22 tests and failed one invalid-file fixture; its requested setuid mode was not
present in the retained file. A strengthened second primary run again passed
22 tests and failed the explicit mode precondition before the special-file
production attachment. Neither attempt reached the eight controls.

The retained [initial failure](initial-failure.json) and
[permission investigation](permission-review.json) distinguish those attempts
from later narrow probes. The same Rust probe binary masked setuid/setgid under
the primary launch context while a separate context retained them. This does
not establish the underlying mechanism or a general macOS rule. Passing probes
do not erase the failed attempts. Production source and its mode-refusal policy
were unchanged.

The final safe fixture has 24 selected tests. Actual filesystem mode coverage
uses sticky mode with exact pre/post checks; no setuid/setgid file is created.
A separate test passes copied `FileIdentity` values carrying each of the three
special bits to the actual `validate_identity` function and confirms rejection,
while the real file stays ordinary. This proves that validation branch, not
real attachment of setuid/setgid files. No alternate execution context was used
to obtain a passing version of the old privilege-dependent matrix.

The other fixtures meaningfully cover original bytes/modes/absence, sequential
ACKs and duplicate/conflict behavior, symlink/hardlink/type/size refusal, observed
file drift, exclusive-temp collision, per-operation evidence, partial pair,
post-rename error/panic, final drift, evidence lifetime/poison and directory-fd
binding. Capacity cases include oversize inputs, sequence-MAX attachment refusal,
the final sequence-MAX commit and cached duplicate, and a privately seeded
request-MAX refusal. The last allowed request ID MAX−1 is not executed.

Fault hooks deliberately fail or panic before/after real operations. They do not
force partial kernel writes, actual ENOSPC/fsync failures or power loss. Tests
are serial; they neither implement nor prove cross-process writer exclusion.
Cross-UID ownership and ACL/xattr behavior are also outside executed coverage.

## Independent reproduction and artifact audit

After the final primary passed, the reviewer ran the unchanged public runner
once using a new private export and separate Cargo target directory. Both runs
used rustc 1.98.1 / Cargo 1.98.1 on `aarch64-apple-darwin`, with locked offline
Cargo. Production configuration compiled. **24 selected tests passed; 138
inherited tests were compiled/listed and filtered. All eight controls compiled
and failed exactly one selected functional assertion with exit 101.** No
application main, UI, native backend, live settings path or endpoint ran.

The [primary result](host-result.json) has SHA-256
`7d27671d43767c86b0e5db85768d567fdc025dcef645785a6f7112d8828be6c0`.
The [independent result](host-result-independent.json) is
`3b93c53c5686e27d5028016a844717fc57f471323077bc40a5f400e26b1eb6ec`.
[comparison.json](comparison.json) compares every source, compiler, inventory
and outcome field. It excludes only 19 exact raw-log hash fields: one production
check log and a compile/test log for each of nine variants. All 19 differ. That comparison rewrites no source or semantic result fields;
raw output is retained unchanged. Whole-result JSON is not byte-identical.

The read-only audit verified the retained copies of all eight runner inputs and
20 candidate files in both runs. It independently reconstructed each mutant
from the pinned base, compared actual variant bytes and source hashes, checked
raw log hashes and the complete listed test inventory, then calculated the
expected assertion location from the selected test's unique assertion anchor.
Each control had exactly one panic at its designated location:

| Control | `settings_pair_tests.rs` line:column |
|---|---|
| Completed duplicate rechecks current pair | 149:5 |
| Full ACK kernel sequence required | 196:9 |
| Duplicate target bytes bound | 169:9 |
| Per-file mode preserved | 85:5 |
| Temporary create exclusive | 325:5 |
| Replacement readback matches | 420:5 |
| Final pair recheck required | 438:5 |
| Terminal component stays closed | 217:5 |

The controls distinguish the intended defects: omitting exclusive creation
modifies a retained collision before refusal; omitting readback matching accepts
external content as current; omitting final verification misses later drift;
and omitting terminal closure allows a valid next ACK after an earlier refusal.
Compilation errors, an unrelated panic and a timeout cannot count as successes.

Each run retained 165 regular synthetic fixture files, checked without following
fixture symlinks; none had setuid/setgid bits. This is a retained-artifact check,
not a trace proving every filesystem instruction. Production compilation
reported 65 dead-code warnings from inactive paths. Each test variant had one
dead-code warning; the ACK-sequence mutant additionally had one unused-variable
warning because it deliberately removes the sequence comparison. Warnings were
not suppressed, and production compilation was not a warnings-as-errors claim.

The private audit source hash is
`5ecd608fca33f03184406c629672554ab4114b72e7a01c491eb642529c433dbc`;
its retained receipt hash is
`7f8477c10fbf9cd07f9ded034dabceff85b411546d7d2af07fc6835989f841c5`.
Raw sources, binaries, logs, fixture files and local paths remain private.

After the interruption, the root completed a separate read-only comparison of
all nine paired test logs. They agree after replacing only the exact private run
root prefix, numeric panic-thread ID and test-summary elapsed seconds. No other
functional text is normalized. The rules and normalized hashes are included in
comparison.json; the tool-submitted script was retained afterward, with that
retention order explicit. This does not claim equal compile logs or binaries.
The earlier independent audit receipt still binds its original base comparison
hash; the added metadata preserves that hash instead of rewriting the receipt.


No sanitizer, crash-recovery, durability-under-power-loss, native-consumption,
physical-timing or deployment conclusion follows. A real ACK-derived mapper,
ordered `SettingsSink`, enforced external exclusion, lifecycle custody and
stopped restoration of both host files plus the original overlay remain separate
integration work. The original runtime gates stay closed.

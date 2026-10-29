# ACK-bound settings-pair experiment

This inactive Unix component records and applies two explicit byte images within
one retained directory: `knob.ctr` and `mixer.state`. It uses the existing
preview's complete `FrameAck` type, preserves original file evidence, and keeps
partial replacement outcomes available for inspection. It constructs no live
backend, selects no settings directory, and changes no legacy settings helper.

The detailed rules are in [CONTRACT.md](CONTRACT.md). The source audit in
[WRITER-BOUNDARIES.md](WRITER-BOUNDARIES.md) identifies existing preview, driver
and boot writers that a future integration must exclude.

## API and retained custody

[settings_pair.rs](settings_pair.rs) exposes:

- `SettingsPair::attach(File, BaselineReceipt, namespace, absent_mode)`: consumes
  an explicitly supplied directory file, validates one baseline key, and captures
  both originals without changing them. Missing and present-empty files remain
  distinct. The namespace selects exclusive temporary names; it is not evidence
  of external ownership.
- `commit(FrameAck, PairTargets)`: checks the complete key, request ID and kernel
  sequence, then applies the supplied bytes in knob-then-mixer order. The first
  request ID is 1; kernel sequence starts immediately after the baseline. This
  instance cannot adopt a new generation or skip an ACK.
- `EvidenceHandle::snapshot()`: copies diagnostic evidence without filesystem
  operations. The handle retains the same directory descriptor and journal after
  the writer is dropped. Poison is visible and never cleared to permit writes.

ACK authenticity and target-byte derivation remain caller responsibilities.
There is no parser, frame-to-settings mapper or `SettingsSink` implementation.
In particular, accepting arbitrary byte images does not promise that a new
mixer map preserves unknown keys. Exact original bytes are retained independently
of any future serialization policy.

Evidence contains three different records:

- **Original:** initial absence or exact bytes, permission mode and file identity;
  later commits never replace this record.
- **Current:** the most recent individually verified images. After a partial
  transaction this can contain one new file and one old file.
- **Last transaction:** exact ACK, both target images, prior pair, per-file
  temporary identity/progress, and pending, committed or failed disposition.

This is bounded in-process evidence, not a complete history or durable journal.
Each newly admitted transaction replaces the prior transaction record, even if
the new attempt later fails. Dropping all handles or terminating the process can
lose custody; neither performs restoration or grants permission to shut down a
native application.

## Replacement and failure boundary

Reads and replacements use fixed names relative to the retained directory fd.
The component refuses symlinks, hardlinks, nonregular or oversized files,
unsupported ownership, and special permission modes. Each file image is bounded
to 65,536 bytes. Existing ordinary permission bits are preserved; missing-file
mode is explicit. ACL/xattr preservation is outside this contract.

Each replacement creates an exclusive temporary, writes the bytes, applies the
mode, synchronizes the file, checks observed identities, renames, synchronizes
the directory, and verifies the result. Armed and completed stages distinguish
uncertain outcomes from returned operations. A directory-sync failure can occur
after a visible rename; a failed second file does not roll back the first.
Standard reads and writes may involve multiple syscalls.

Completed exact duplicates verify the current pair read-only before returning
the cached result. Failed or interrupted duplicates return retained failure
without attempting completion. Conflicting duplicates preserve the original
transaction. Partial results, drift, order errors, exhaustion and interrupted
operations block later writes. There is no automatic retry, restoration,
temporary-file cleanup or mutation on Drop.

Both files are **not** replaced atomically. The caller must exclude competing
writers and readers that require a consistent pair for the entire operation.
A mutex, nofollow open, identity check or directory fd does not establish that
exclusion. Check-then-rename is not conditional inode replacement, and a retained
directory does not prove that its former pathname still names the same object.

## Pinned source preparation and validation

[prepare-candidate.py](prepare-candidate.py) reconstructs the frozen
[owned-exit](../emulator-owned-exit/) parent through its preview-gate,
preview-transport and joined-session chain. It verifies all 18 parent files and
produces a fresh private tree with 20 pinned files. Only three inherited files
change: `main.rs` gains an inactive Unix module declaration, `Cargo.toml` gains
the direct Unix dependency `libc = "=0.2.190"`, and `Cargo.lock` adds that already
pinned dependency to the preview package. The two added files are the component
and [its focused fixtures](settings_pair_tests.rs). No live construction is added.
[Parent inputs](parent-inputs.json) and [candidate pins](candidate-pins.json)
bind the exact transformations.

[run-tests.py](run-tests.py) uses locked, offline Cargo builds and fresh explicit
private test directories. It compiles the production configuration and inherited
crate, then selects the new settings-pair fixtures and strict mutation controls;
it does not execute application main or inherited suites. Execution results and
limits belong in [HOST-TESTS.md](HOST-TESTS.md), not in API or source claims.

Remaining gates include a reviewed ACK-derived mapper, a same-session
`SettingsSink` bridge, enforceable external writer/reader exclusion, retained
lifecycle custody, durable recovery evidence, and separately reviewed restoration
of both host files and the original overlay. No native-stop, crash-recovery,
physical-timing or runtime-deployment conclusion follows from this component.

# ACK-bound file-pair persistence contract

This component is an inactive Unix filesystem primitive for the pinned preview
source. Its scope is the two fixed names `knob.ctr` and `mixer.state` within an
explicitly supplied, retained directory descriptor. It has no ambient socket
folder, backend factory, live construction, legacy helper call or restore API.
It takes complete `FrameAck` values from the existing producer types and explicit
caller-supplied target bytes. It does not yet implement `SettingsSink` or derive
file contents from a frame. Those integrations require separate review.

## External authority and exclusion

The caller must establish that the directory, initial baseline and ACK source
belong to the intended session. A typed ACK is not self-authenticating. The
component's checks do not authenticate the C broker, driver or native process.
The directory fd binds operations to that directory inode; it does not prove
that a future process will still resolve an external pathname to the same inode.

All competing writers and readers needing a consistent pair must be excluded
throughout capture, application and verification. A local mutex or retained fd
does not establish this. In particular, ordinary POSIX rename is not conditional
on the destination still having its observed inode. Another writer can change a
file between the last identity check and rename. No read/check/rename sequence
here removes that race. Preview, az-drive, boot and test helpers remain external
writers until a later integration explicitly excludes them.

The two replacements are not an atomic pair. A reader can observe the first
new file beside the second old file. A failed second replacement does not undo
the first. No automatic rollback, replay, lifecycle transition, service restart
or producer rearming follows from either success or failure.

## Retained original and transaction evidence

Attachment captures each original as absent or as an exact regular-file image,
including bytes, permission mode and file identity. Absence and an empty file are
different states. The original images are never replaced by later successful
updates. Directory and file identity are provenance and eligibility checks;
restoring bytes through a future rename would create another inode and would
not restore the original inode identity.

A transaction binds the complete ACK and both target byte strings before the
first mutation. The engine is restricted to the attached generation, starting
at request ID 1 and the baseline's next kernel sequence. It does not adopt a new
generation or infer a skipped ACK. Arithmetic exhaustion, stale/gapped identity,
conflicting duplicate, filesystem drift and partial/uncertain operations close
further admission. A same-identity conflict preserves the original transaction.

Completed exact duplicates verify the current pair read-only and return the
cached outcome without rewriting it. A duplicate of an incomplete transaction
cannot complete it by retrying a syscall. Older superseded transactions cannot
be replayed. This is bounded evidence: original pair, latest verified pair and
one latest transaction, not a complete history or durable crash journal.
The `current` images mean each file's last successful verification; after a
partial transaction they can describe different transaction stages. They are
not a fresh observation or a promise that the complete pair committed. An exact
completed duplicate can still return its historical receipt after a separate
terminal refusal, provided the pair verifies; this never reopens admission.

## Filesystem operation boundary

Fixed names are resolved relative to the retained directory fd. Reads refuse
symlinks, nonregular files, hardlinks, excessive size and unsupported metadata.
Temporary files are created exclusively with unique owned names. Failure to
create an exclusive temporary file is a refusal, not permission to overwrite an
existing path. Existing permission/owner requirements must be satisfied before
a replacement; absent-file mode is explicit. ACL/xattr/ownership restoration
outside the supported checks is not promised.

Each file has its own preparation, temporary-content synchronization, armed
rename, visible rename, directory synchronization and verification evidence.
The completion state is recorded only after that operation returns and its
required checks pass. If a rename may have happened before an error or panic,
the retained armed state remains uncertain. A directory-fsync failure does not
pretend a visible rename was undone. An exact readback alone is not evidence
that the directory entry became durable.

Files are bounded to 65,536 bytes each. Attachment requires current effective
UID ownership of the directory and effective UID/GID ownership of each existing
file; unsupported ownership is refused, not silently changed. The explicit
absent-file mode contains only ordinary permission bits. Standard library
`read_to_end` and `write_all` may perform multiple underlying I/O calls; refusal
after an error means no transaction replay, not a claim of one syscall per file.

A partial error or panic blocks later writes. It retains original images,
current transaction and per-file progress for inspection. No implicit cleanup
unlinks temporary files, retries an uncertain operation or restores originals.
The independently held evidence handle retains the directory and journal after
the writer is dropped. Dropping the last in-process owner or process death can
still discard this custody; that does not complete recovery.
The test-only before/after fault hooks exercise publication and uncertainty
accounting around real operations. They do not substitute a kernel's partial
write/fsync return or emulate a power failure. Host success is not physical
storage durability or whole-process lifetime proof.

## Required next composition

A later mapper must derive the intended knob and mixer contents from a fully
validated ACK while preserving supported unrelated settings. The current
preview's permissive parse/merge routines are not adopted as proof of that
mapping. A real `SettingsSink` bridge must retain this same engine and expose
partial progress after callback error or panic, without converting the known
native commit back into replayable work.

Stopped restoration additionally needs authoritative original overlay and host
baselines, closed producer admission, scoped native-stop identity, exact pending
operation reconciliation, externally enforced writer exclusion, and separately
guarded restart. This component has no restore operation or permission token.
No combination of cached receipts, directory fd ownership, file readback or
successful synchronization opens those gates. All runtime, staging and product
gates remain closed.

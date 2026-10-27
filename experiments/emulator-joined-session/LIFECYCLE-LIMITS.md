# Retained accounting is not a recovery permit

This successor keeps the original in-memory route/backend alive after its local
worker joins. It does not connect an owned backend, coordinate application exit,
restore the controller overlay or implement a host-file settings sink. The old
runtime runner stays closed.

## Application and supervisor ordering

The prepared App still requests terminal stop and then follows its existing
guest Quit / `boot::stop` path without holding or joining a worker. Main supplies
`None`, so no owned mixer worker currently uses that path. Retaining a session
is a prerequisite for a later lifecycle owner, not a repair of App exit.

On pinned emulator commit `06fc981707e69982c0554bb508e0c646c005a979`:

- `az-preview/src/main.rs` starts QEMU before `run_native` and moves the launched
  supervisor handle into the App-creator closure. Failure before App creation
  can drop that handle without calling App `on_exit`.
- Unix `boot.rs` treats supervisor-input EOF/disconnection as an instruction to
  stop its owned child, escalating from SIGTERM to a forced kill after its grace
  interval. Process death can reach this path without a joined-session receipt.
- Windows attempts to bind a job with `KILL_ON_JOB_CLOSE`. When that binding
  succeeds, its lifetime policy operates independently of App cleanup. Existing
  job creation/assignment failures can instead let QEMU outlive the app.
- App queues `EmuCmd::Quit`; the screen worker writes QMP and ignores its
  write/reply result. Unix `boot::stop` also discards its final wait result.
  Neither is a verified stopped-native-process receipt.

The pinned eframe 0.29.1 dependency also handles macOS Cmd-Q through its save/destroy
path because its event loop may not return. A main-only fallback would therefore
be insufficient. The next separately reviewed owner must retain both the worker
and supervisor resource, use an idempotent shutdown path from App exit and the
returning/startup-error path, and keep the supervisor pipe alive through an
actual join. Stop errors must not skip joining. A join with an unknown request
still cannot authorize overlay handback, guest teardown or restart. Abnormal
process death remains a separate problem; this checkpoint does not weaken the
existing supervisor or job policies.

## Host files have independent writers

The unchanged preview `config.rs` persists `mixer.state` through a fixed temporary
path and rename, and writes `knob.ctr` directly. It ignores write/rename failures,
does not exclude concurrent writers and does not preserve replacement-file
modes. It has no symlink-refusal contract. Its permissive parser preserves
duplicate keys and updates the first match, whereas Python JSON decoding uses
the last. These are static source findings, not executed filesystem tests here.

The pinned `tools/az-drive.py` independently reads and writes both files, and
the boot scripts remove both. Kernel overlay ownership does not fence these
host operations. A new advisory lock alone would not exclude those existing
uncooperative paths.

The new callback disposition means only that no callback was requested, settings
were skipped, armed with no recorded return, returned success or returned an error.
It does not establish
file contents, durability, restoration or writer exclusion. Returned error may
follow partial side effects. Returned success is only the injected sink's claim.
Neither authorizes replaying a frame whose native commit is already known.

The earlier [recovery contract](../emulator-overlay-handback/RECOVERY-CONTRACT.md)
currently specifies exact original counter-file restoration. A real settings
sink must additionally retain original `mixer.state` bytes, absence, mode and
identity, define supported/unrelated-key handling, exclude all competing writers,
and account for partial progress across both files. Two renames are not one
transaction with each other or with the kernel. No such writer or two-file
recovery protocol is implemented here.

## Lifetime and trust limits

The joined session has private fields, no constructor, no Clone implementation
and no backend accessor. Its copied diagnostics may be freely copied; they are
not capabilities. A poisoned snapshot remains poisoned, even if an older raw
field says admission was open. Diagnostic salvage never clears poison.

Exclusive use of one route by one port family is still a trusted construction
precondition. The existing `Arc<MixerRoute>` API cannot prevent an outside clone
from bypassing the port. The joined snapshot does not freeze those external
callers or establish a cross-process ownership lease.

Dropping the final session/route reference may run a backend destructor and close
its resources. That is not neutralization or a restoration receipt. Retention
lasts only while the in-memory owner survives. There is no durable journal,
process-death reconciliation, authenticated wire adapter, callback deadline,
native-consumption/dwell guarantee or cancellation protocol in this patch.

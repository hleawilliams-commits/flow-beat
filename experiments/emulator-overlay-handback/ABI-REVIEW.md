# Native 64-bit instance and generation ABI review

This is an offline design review of a separate successor to the frozen
[phase-one ownership candidate](../emulator-overlay-ownership/README.md).
It proposes no change to that candidate or the original emulator checkout.
The ABI review used no module installation, device access or runtime observation.
A subsequent isolated build is recorded separately in [ARM64-BUILD.md](ARM64-BUILD.md).
Host tests and compilation do not establish producer resumption or stopped recovery.

## Selected boundary: one broker, explicit generation rotation

Keep one owning broker open file description. Add a per-module-load 128-bit
instance value to every tagged request and state snapshot. Reuse the existing
64-bit epoch as the producer generation. A new `ROTATE` operation must validate
the current owner, instance, run, epoch and exact next sequence, then atomically
replace all 152 opaque frame bytes, advance the epoch and reset sequence to zero.
It retains the same owner, run and permanent legacy mixer fence.

There is no separate producer-generation counter and no kernel inference of an
upstream producer's identity. `CLAIM` already advances the epoch when changing
owners after explicit release or owner loss. `ROTATE` provides a new generation
while the same broker remains owner. Epoch and sequence overflow must fail
before changing any frame or state. A successful rotation has no post-commit
usercopy; an uncertain result requires a state query on the same open description,
not a blind replay.

A direct recipient-file transfer is deliberately unimplemented. That alternative
would require a recipient-binding protocol, competing accept/recovery semantics
and additional VFS lifetime review. A public offer or run ID alone would not
authenticate an intended external producer. Keeping a single broker avoids
pretending that these requirements are already solved.

## Proposed fixed layouts

All fields use fixed-width UAPI types. The instance consists of two
`__aligned_u64` words; all integer fields use native byte order. This is a native
64-bit ioctl contract, not a portable network encoding or a compat handler.

| Structure | Fields and offsets | Size |
| --- | --- | --- |
| V2 command | `version` 0, `reserved` 4, `instance[2]` 8, `run_id` 24, `epoch` 32, `sequence` 40 | 48 |
| V2 frame / ROTATE | command 0, `frame[152]` 48 | 200 |
| V2 state | `version` 0, `flags` 4, `instance[2]` 8, `run_id` 24, `epoch` 32, `sequence` 40, signed `crc_len` 48, `reserved` 52, `frame[152]` 56 | 208 |

Require compile-time size/offset assertions, exact ioctl direction/size/number
matching and zero reserved fields. Changing the encoded sizes makes the V1
command encodings distinct; the new candidate must reject all V1 mutation
commands, with no fallback parsing. Every tagged mutation must still require a
writable file, `CAP_SYS_ADMIN` and explicit load-time opt-in. No compat ioctl
handler is implied by the retained kernel's `CONFIG_COMPAT=y`.

`ENABLED` can use flag 16 after phase one's four existing flags. When ownership
is disabled, state reports no enabled flag and an all-zero instance; mutation
remains refused. When enabled, instance must be nonzero before any controller
device can be reached. All state output, including reserved bytes, must be
initialized, and its values/frame must be captured under the mutation lock.
A state snapshot is not a permit or proof of native input consumption.

The proposed implementation must retain these phase-one properties:

- Input usercopy finishes before the final lock and may not establish ownership
  by itself. Owner, identity and exact next sequence are rechecked under the lock.
- Once fenced, legacy mixer writes and mixer clears remain refused even after
  rotation, release or final close. Clear-all remains an atomic refusal; other
  buses retain their separate behavior. No handback flag reopens legacy bus 2.
- The CRC-prefix setter cannot change the fenced mixer prefix from 96, and the
  read path snapshots frame and CRC length together.
- Final close only drops ownership; it does not alter frame, CRC, epoch or fence.
  `dup` and inherited descriptors share the same open description. No permanent
  extra file reference is added merely to retain an owner pointer.

## What excludes old queued commands

The final kernel lock can reject an old instance or epoch even if its frame was
copied from user memory before rotation. It cannot determine when a user-space
payload was authored. Therefore generation must be attached when input is
accepted into the producer queue, before enqueue, and remain immutable through
queueing, reconnect and transmission.

The broker must retire the old producer queue and any cached replay frame before
admitting the successor. It must never re-label an old payload with the current
epoch. An untagged upstream message is not made fresh by attaching a generation
at dequeue. Sequence numbers alone also do not establish freshness.

A narrow future transition is: stop admitting the old generation; retain and
validate a fresh full frame; issue one owned `ROTATE`; query the resulting exact
instance/run/new epoch/zero sequence/frame after uncertainty; and only then open
an initially empty successor queue with the new generation. Requests already in
flight with the old epoch must be refused at the final mutation boundary.
Broker reconnection alone is insufficient. These are adapter requirements,
not evidence that any current preview or forwarder implements them.

`ROTATE` installs its frame immediately. The kernel treats it as opaque; the
operation does not certify neutrality, application pause, native delivery or
host-counter consistency. None of those claims follows from an epoch increment.

## Exact cached Linux random API

This review read the retained Linux `v6.6.138` sources at commit
`3b9f64db049687c0d38b4b3ef2f297f0642179af`, using the configuration and export table
pinned by [ARM64-BUILD.md](../emulator-overlay-ownership/ARM64-BUILD.md).
No external source was fetched.

| Retained source | SHA-256 | Relevant definitions |
| --- | --- | --- |
| `include/linux/random.h` | `c9c5ed2a5545f9444988f73ed7485adb7579f0c09d59ce346e0753f6e268d1ed` | `get_random_bytes` line 39; wait API lines 120–131 |
| `drivers/char/random.c` | `9505682ce1ea6851d6193b57867b4e2b4ab6d26de9c723f25b2029c54ab3f934` | readiness/wait lines 103–143; byte API lines 400–411 |

Both `wait_for_random_bytes` and `get_random_bytes` are exported and appear in
the retained `vmlinux.symvers`. The wait may sleep until the random generator is
ready and may return an interrupt error. There is no bounded module-init timing
proof. Call it outside any spinlock and before publishing devices:

```c
ret = wait_for_random_bytes();
if (ret)
    return ret;
get_random_bytes(instance, sizeof(instance));
if (!(instance[0] | instance[1]))
    return -EIO;
```

Only the opted-in path needs this identity. The disabled path may keep the
instance zero without waiting for entropy. A failed wait or zero result must
abort opted-in initialization before device publication; do not substitute a
time, PID, address, constant or a reused identifier.

A relevant exact-source caveat: this kernel's inline `get_random_bytes_wait`
**unconditionally fills the output after the wait and then returns its status**.
It does not suppress the fill on wait failure. The explicit sequence above makes
the failure boundary clear. An earlier shorthand description of this wrapper
was corrected after reading the exact function body.

A successful nonzero 128-bit draw gives probabilistic separation between ordinary
module loads. It is not a mathematically unique identifier, a secret capability
or replay protection against restoring/cloning a VM snapshot containing the
module's state. Random-generator VM-fork support would not automatically rotate
an already-stored instance value. No VM-fork identity protocol is reviewed here.
A future adapter must separately bind the expected module build and guest/boot
lifecycle, refuse unreviewed restore/clone changes, and never resume an old
receipt merely because a saved run ID matches.

## Stopped recovery contract remains separate

The kernel cannot attest that the application is stopped or that host files have
been restored. A future coordinator needs a reviewed recovery transaction, with
separate receipts for file cleanup, overlay restoration, host restoration and
restart. An input error must not lead to an unconditional restart in `finally`.

Before the first test input, retain a fresh complete neutral baseline obtained
under the owned kernel fence, plus the original host counter bytes/mode and
relevant saved mixer state. Bind them to the module instance, owner run/epoch,
source/build identities and the test scope. A pre-claim frame observation alone
is insufficient because an old legacy write can commit before the claim. Require
original host-counter/overlay agreement; do not normalize a mismatch into a
passing baseline. Host-side competing writers and queues must also be excluded
by the reviewed broker/producer protocol, not inferred absent from quiet samples.

There is a mode-restoration limit: `FRAME` and `ROTATE` install an injected frame.
If the baseline had `INJECTED=false`, writing its idle bytes cannot restore that
original mode. The narrow input-bearing contract should require an already
injected baseline, or separately design a tagged restore-to-idle operation.
Do not use a legacy clear or call matching bytes exact state restoration.

For a run that changed input, use the separately scoped
[stopped recovery contract](RECOVERY-CONTRACT.md). Producer admission must close,
the intended application must be verifiably stopped, and the old generation must
be revoked through owned `ROTATE` while committing the saved neutral frame.
A plain `FRAME` or matching bytes alone does not revoke pending old-generation
commands. The resulting new-generation state must be verified with admission
still closed before host restoration, full pair verification and any guarded
restart. No kernel/host filesystem atomic transaction exists; retained ownership,
closed admission and the verified stop must span that interval.

An uncertain tagged syscall may be reconciled as positively committed only with
the required same live open description, one unresolved operation, exact retained
payload/identity and matching state, with no intervening admitted writer. A
lower sequence does **not** prove the operation was cancelled: it may still be
pending upstream or before the kernel lock. Failed/partial queries, conflicting
commits and unknown rotations remain unresolved, not silently retried or
renumbered. A recovery rotation can itself race a pending frame; the broker must
retain both possible orders and may not infer a new safe sequence merely from a
later snapshot.

A verified restoration phase is idempotent: repeat observations and receipt
checks, not completed writes or stop/start cycles. Failed restoration, uncertain
start, owner loss, sequence/epoch exhaustion or module replacement must not fall
through an automatic restart. The permanent fence prevents legacy resumption but
can retain held input; it is not a recovery action. Receipt-bound host-only
restoration stays independent and cannot claim overlay completion or authorize
restart. Native events sampled before rotation need separate quiescence/lifetime
evidence; the kernel frame mutation does not cancel them.

## Required discriminating checks

Future actual-source tests should force old-epoch frame/rotate/release commands
to wait after usercopy, rotate first, then require each stale commit to fail
without changing bytes. Cover competing rotations, exact sequence/epoch
boundaries, wrong instance including ordinary-reload replay, same-file queue
reentry, ownership loss and read snapshots concurrent with rotation. Reject the
old V1 encodings and any mismatched ioctl size or reserved field.

Initialization tests need disabled/no RNG, wait failure/no fill/no publication,
zero random instance/refusal, and successful pre-publication instance binding.
A deterministic fake random provider can test control flow; it cannot validate
entropy or uniqueness. Restore tests must retain held-frame and host-counter
mismatch controls, stop failure, host/overlay write uncertainty, absent injected
baseline and an attempted restart before verified restoration. A broker fixture
must demonstrate queue-time tagging and reject cached reconnect replay; kernel
checks cannot substitute for that evidence.

The selected ABI is a reviewable next implementation boundary. It supplies no
runtime installation approval, producer integration, stopped-recovery proof,
module loadability proof or hardware timing evidence.

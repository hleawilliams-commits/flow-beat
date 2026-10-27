# Emulator mixer-overlay ownership — offline phase 1

This separate emulator-kernel candidate addresses the final mutation boundary
identified by the [writer/recovery counterexamples](../mods-shortcut-delivery/task-sdk-observer/overlay-policy-v2/RUNTIME-GAPS.md).
It has **no runtime adapter, producer handback, stopped recovery or installation
approval**. It is not an AZ-MODS core patch or a community package.

The candidate is pinned to emulator commit
`06fc981707e69982c0554bb508e0c646c005a979`. Only `subucom_virt.c` and a new local
UAPI header change; the prepared output is a fresh private copy. The existing
emulator checkout, its installed module, cores, services and released state are
not changed by this experiment.

## Contract

Ownership is disabled by default. A future explicitly opted-in module load with
`allow_overlay_ownership=1` would allow a writable controller open description
and a caller with `CAP_SYS_ADMIN` to claim mixer bus 2. The opt-in parameter is
read-only after load. Every tagged mutation rechecks write mode and capability.
The feature has a native 64-bit ioctl ABI; there is no compat ioctl handler.

| Operation | Required identity | Committed change |
| --- | --- | --- |
| CLAIM | nonzero run ID, current module epoch, sequence 0, no active owner, CRC prefix 96 | increment epoch, bind the open file, set sequence 0 and permanent mixer fence |
| FRAME | owning open file, run ID, epoch, exact next sequence | replace all 152 mixer bytes and advance sequence together |
| RELEASE | same identity and exact next sequence | advance sequence and clear owner; preserve overlay, CRC and fence |
| STATE | no write authority required | copy a coherent snapshot; no mutation |
| final file close | kernel's final open-description release | clear owner only; preserve overlay, CRC, epoch, sequence and fence |

`dup` and inherited descriptors refer to the same open file description and
therefore share ownership. Independent opens do not. The kernel holds no extra
reference to the owner's file. The explicit RELEASE ioctl may race another
operation; final file release occurs only after in-flight file references finish.

Claim, tagged writes, explicit/final release, all legacy writes and CRC setting
commits share `miso_lock`. The legacy permission check occurs after usercopy and
under that lock, immediately before mutation. A writer paused after copying an
old frame cannot bypass a claim that commits first. Once fenced, all untagged
mixer writes and mixer clears are refused, including writes from the owner.
Clear-all is refused atomically before changing any bus. Explicit operations on
the other buses remain available. There is **no transition back to legacy mixer
writes during this module lifetime**.

The CRC setter parses the whole comma-separated request into temporary storage.
It commits only a valid list, under the same lock, and refuses the whole update
if it would change a fenced mixer prefix from 96. Valid short lists still replace
only their supplied prefix and update the displayed count. Readback copies the
values/count under the lock before formatting. `spi_read` captures the frame and
its CRC length together, then computes the checksum after unlocking. The bounds
check avoids signed overflow even when an extreme integer was configured.

The ioctl number, direction and encoded size must match exactly. Version and
reserved fields are checked, epochs/sequences never wrap, and all input is
copied before locking. CLAIM and RELEASE are input-only; no output-copy failure
can silently undo a successful claim. An uncertain caller must inspect STATE on
the same open description instead of blindly retrying. STATE output is fully
initialized, copied under the lock and returned outside it. Failed output copy
does not alter state. Its frame is **raw overlay/idle bytes before CRC restamping**;
it is not evidence that the native application consumed input.

## Preparation

From the repository root, use a **new private output directory**:

```sh
python3 experiments/emulator-overlay-ownership/prepare.py \
  --emu ../xdj-az-emu \
  --output experiments/emulator-overlay-ownership/private/candidate-NEW
```

Preparation verifies the exact base commit, three original module file hashes
and the patch hash before writing. It applies without fuzz and retains a receipt
and patch log. It does not build, install, connect to an emulator or remove files.
Raw full source copies and all build/test logs remain in ignored `private/`.
The patch preserves the source's GPL-2.0-only license and attribution.

## Host validation

The [actual-source fixture](HOST-TESTS.md) passes 146 candidate assertions in each
of plain, ASan/UBSan and TSan modes. Seven compiled source mutations per mode
produce their exact expected functional failures; sanitizer diagnostics are
treated as unexpected failures. Seventeen integration assertions check handler,
parameter and ABI wiring in the source. An independent full run matches the
[sanitized result](host-result.json) byte for byte. The [independent review](REVIEW.md)
records its source boundary and remaining kernel/parameter compatibility limits.

The suite forces writes/clears/CRC updates to wait before their final lock, then
acquires ownership before allowing them to continue. It also covers same-file
stale epochs after explicit release/reacquisition, simultaneous claims and
same-sequence writes, coherent delayed snapshots and permission loss by an
existing owner. A reader acknowledges an intermediate frame before the writer
may finish, so the concurrency check cannot pass on post-join reads alone.
These are host executions of the extracted methods with kernel-service shims.

The [strict ARM64 build](ARM64-BUILD.md) uses the retained Linux 6.6.138 kernel
sources/configuration and export table, with no remote build inputs. Compilation
and strict modpost pass without warnings; all 33 undefined symbols resolve in
the pinned kernel export table. Independent ELF parsing confirms the target ABI
and resolution. An initial build exposed missing parameter-type metadata, which
the final one-line correction restores. All five executable sections remain
byte-identical across that correction, and the original integer-array metadata
appears exactly once. Both builds are retained privately. Neither was installed.

## Gates and limits

- This phase provides a persistent fence, not a usable producer handback. Old
  preview and forwarder queues cannot resume bus-2 writes after fencing. A future
  handback needs producer generation rotation, fresh neutral state and a proved
  final-boundary protocol; merely reopening a socket is insufficient.
- Owner loss retains the last overlay, which may contain held input. This patch
  does not synthesize a release, restart the application or claim recovery. Owned
  recovery must restore the full original neutral overlay and exact host bytes
  while the application is stopped, verify both, and only then permit restart.
- Epochs are unique only within one module lifetime. Reload/reboot resets them.
  Future adapters must bind the instance/boot, not rely on a run ID alone.
- Exhausting the sequence prevents further tagged writes/releases; final close
  can orphan the lease and a new claim can rotate the epoch if it is not exhausted.
  Exhausting the epoch refuses new claims. No wrap/reset escape is supplied.
- Capability checks and file identity are an ownership protocol among cooperating
  privileged components, not a security boundary against arbitrary root, kernel
  modification, module removal or other input paths. GPIO/MOSI/native routing,
  full driver lifetime/races and audio/performance are outside this experiment.
- Host tests substitute kernel services, including spinlock implementation,
  usercopy and VFS objects. They cannot prove Linux lockdep, VFS reference lifetime,
  module loadability, native consumption or physical timing.

Keep the existing V2 runner inert and the closed run closed. No runtime identity,
enabling manifest, installation or new observation is justified by this partial
candidate. MODS publication, stems staging/activation, USB activation and physical
validation remain separately unresolved.

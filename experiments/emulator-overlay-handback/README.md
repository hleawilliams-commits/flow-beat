# Emulator instance and input-generation boundary

This offline successor adds module-instance binding and atomic generation rotation
to the frozen [phase-one ownership patch](../emulator-overlay-ownership/). It is a
kernel primitive for a future broker-mediated handback, **not an implemented
preview handback, runtime adapter or stopped recovery**. No module was installed;
the existing observer runner stays disabled. No community package changes.

The separate patch applies after phase one to emulator commit
`06fc981707e69982c0554bb508e0c646c005a979`. Both patches and all intermediate/final
source hashes are checked by `prepare.py`; only a fresh private copy is written.
Phase one, the original emulator checkout, native hooks and existing runtime
helpers remain unchanged.

## Contract implemented by the kernel candidate

When the existing default-off ownership option is enabled at module load, init
waits for the kernel random generator, creates a nonzero 128-bit instance, then
publishes devices. An interrupted wait or zero result fails initialization.
Disabled modules do not wait or generate an instance. This is probabilistic
per-load identity; a VM snapshot or clone can reproduce the entire identity and
state, which this protocol cannot detect.

Every mutating V2 ioctl requires that instance in addition to the existing
writable open description, current privilege, opt-in, run, epoch and sequence
checks. Exact ioctl sizes deliberately exclude V1 requests; there is no unbound
V1 mutation compatibility path. Native 64-bit ABI only.

| Operation | Commit under the shared mixer lock |
| --- | --- |
| CLAIM | Validate instance/current epoch, acquire the file, increment epoch, sequence 0; retain existing frame and permanent fence |
| FRAME | Validate file/instance/run/epoch/exact next sequence, replace all 152 bytes, advance sequence |
| ROTATE | Same checks plus epoch capacity; replace all 152 bytes, increment epoch and set sequence 0 **together**, retaining the same broker file and run |
| RELEASE / final close | Preserve the frame and fence; clear ownership as specified in phase one |
| STATE | Coherent historical snapshot with instance and ENABLED flag; no mutation or delivery acknowledgement |

The epoch is also the producer generation. A successful ROTATE prevents a later
commit of an old-epoch request, including one copied before rotation but waiting
for the final lock. Legacy mixer writes and clears remain permanently fenced;
other buses and CRC behavior retain phase-one semantics. No operation reenables
legacy mixer input. Generation/sequence exhaustion fails without wrap.

ROTATE's frame is opaque to the driver and becomes available immediately. The
driver does not know whether it is neutral, freshly sampled, or authorized by a
recovery plan. It does not identify an upstream producer. One trusted broker must
keep the owning descriptor and validate the creation generation of every input
before submitting it. Merely relabeling an old payload with the new epoch defeats
that upstream guarantee; the kernel cannot detect it. Direct ownership transfer
to another producer is not part of this patch.

A timeout plus STATE showing an old sequence is not cancellation: upstream or
in-flight bytes may still commit. Retain the same descriptor and reconcile exact
command identity; do not reconnect, close or blindly retry. Even a confirmed
rotation cannot retract native events sampled before it. The
[producer review](PRODUCER-REVIEW.md) distinguishes that native-event limit from
producer/transport request rejection. The [ABI review](ABI-REVIEW.md) records the
instance and initialization boundaries.

## Reproduce offline

From the repository root, with a new output path:

```sh
python3 experiments/emulator-overlay-handback/prepare.py \
  --emu ../xdj-az-emu \
  --output experiments/emulator-overlay-handback/private/candidate-NEW
```

The helper validates all base and both patch inputs before writing, applies the
captured bytes with zero fuzz, and checks the exact intermediate and final file
sets. Failed preparations remain for diagnosis. It has no network, installer,
device or cleanup path. Full source copies, build artifacts and raw test logs
stay private.

The [actual-source host suite](HOST-TESTS.md) passes 228 candidate assertions and
ten strict fault-injection controls in each of plain, ASan/UBSan and TSan modes,
with an independently byte-identical result. It extracts 14 functions and checks
22 source/ABI integration conditions. Deterministic schedules cover old copied
requests across rotation, competing frame/rotate operations, coherent historical
snapshots and instance mismatches. A low-sequence STATE followed by a successful
pending commit, and a freshly retagged old payload accepted by the kernel, are
explicit limitation controls.

[Strict ARM64 compilation/modpost](ARM64-BUILD.md) passes without warnings. All
35 actual undefined symbols match retained exports; independent ELF parsing
confirms the two expected random-API additions and unchanged parameter metadata.
The [code review](CODE-REVIEW.md) is scoped separately from executed evidence.
No build was installed. Passing source tests or compilation does not establish
loaded-kernel behavior, native consumption, timing, audio quality, or a usable
handback.

## Next implementation gates

The next work is the broker/producer admission protocol identified by the
producer review: tag at input creation, discard obsolete queued/cached work,
serialize role changes and uncertain requests, and require a verified full
baseline before a producer resumes. Existing preview and forwarder transports
have none of this wiring. Keep them unmodified until the complete source path
and failure semantics are implemented and tested together.

[Stopped recovery requirements](RECOVERY-CONTRACT.md) remain a separate,
unimplemented contract. No fresh run, enabling manifest, module installation,
service restart or input observation follows from this candidate. MODS release,
native queued-event ownership, stems staging/activation, USB activation and
physical validation retain their existing gates.

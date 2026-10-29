# Restore selected mixer mode and raw frame under one generation change

This offline successor applies after the frozen [instance/generation kernel
candidate](../emulator-overlay-handback/). It adds one input-only tagged ioctl,
`AZ_OVERLAY_RESTORE`, which commits a supplied mode and complete raw frame while
advancing the epoch and resetting sequence to zero. File/run ownership, module
instance, the permanent legacy fence and mixer CRC policy remain intact.

It is an uninstalled kernel primitive. No native stop, recovery coordinator,
broker adapter, preview factory or runtime permit is added. The existing V2
runner remains disabled. Native menu, stems, USB and physical requirements
remain unresolved; runtime and stems staging gates remain closed. The original emulator checkout and both frozen parent patches are
unchanged.

## Exact operation

The new native-64 request is 208 bytes: the existing 48-byte V2 header, a 32-bit
`injected` value, a zero 32-bit reserved field and all 152 raw frame bytes. Its
exact ioctl encoding is `0x40d05a55`. Existing command layouts and encodings,
including FRAME, ROTATE and STATE, remain unchanged. Older modules reject the
new command; no fallback to legacy clear or untagged writes is provided.

`injected` must be 0 or 1. Mode 1 accepts an opaque full payload. Mode 0 requires
every supplied byte to equal this pinned module's immutable mixer idle template,
including the raw CRC positions and tail. A mismatch is refused, never normalized.
This byte match does not prove that the template is neutral in a native context.

After a complete user copy and format validation, the existing final mutation
lock checks the module instance, owning file, run, epoch and exact next sequence.
Both sequence and epoch exhaustion refuse without changing any state. Success
copies the full frame into the overlay buffer, selects the requested mode,
advances epoch and sets sequence to zero within the same critical section.
There is no post-commit copyout. A repeated identical target still revokes the
old generation; repeating its original request then fails as stale. New requests
must not be automatically renumbered after an uncertain outcome.

The restoration target is the selected raw frame and mode. With injected mode
off, STATE and mixer SPI reads select the immutable idle template. The dormant
injection buffer is not exposed in that state; its previous bytes cannot be
recovered from STATE. This implementation writes the template into that dormant
buffer too, and does not claim to restore hidden internal-buffer history.

STATE copies the selected raw bytes. SPI reads copy the selected frame and CRC
prefix under the lock, then stamp CRC into their local copy afterward. A read
which sampled the old frame before RESTORE can still complete afterward. The
service-mode OR applies only to ErP bus 0, not the owned mixer bus 2. Neither a
successful ioctl nor a later STATE proves native consumption or cancels a task
already sampled/queued. Retagging old bytes with a fresh valid grant remains
possible; producer provenance and admission barriers remain external obligations.

## Source and validation

[prepare.py](prepare.py) exports the pinned emulator Git blobs at
`06fc981707e69982c0554bb508e0c646c005a979` and applies three captured patches in
a fresh ignored directory. Each intermediate inventory is checked against its
exact pins before proceeding. The new [patch](overlay-restore.patch) is
`fdf40df07b76e22f1684186e4d725ccc5d1cc56e24d5cb9a7f7813534a045352`.
Only `subucom_virt.c` and its ownership UAPI header change; Makefile and frame
header remain identical. See [candidate pins](candidate-pins.json) and
[parent inputs](parent-inputs.json).

```sh
python3 experiments/emulator-overlay-restore/run-tests.py \
  --emu ../xdj-az-emu \
  --result experiments/emulator-overlay-restore/test-result.json
```

The focused fixture extracts 14 exact candidate functions with pthread-lock,
owned-memory usercopy and kernel-service shims. Native-64 UAPI/frame headers are
included unchanged. It checks new RESTORE interactions with STATE, SPI, existing
mutators, CRC and legacy input; it does not repeat the frozen parents' full
suites. The 152 individually corrupted idle targets form one aggregate oracle,
not 152 reported assertions. Thread creation and join checks are included in the
reported count. Unused extracted functions are allowed at host compilation;
other warnings remain errors. Real Linux fault handling, VFS lifecycle, entropy,
module registration, kernel execution and native timing are outside this suite.

The [primary run](test-result.json) passes **148 checks per plain, ASan/UBSan
and TSan mode**, plus 26 static source/ABI integration checks. Its result SHA-256
is `9f66159d2acedc789efbe32513392748ff4dee4f5cf1b4e5cecfe2b8190662a4`.
A [fresh independent run](test-result-independent.json) matches the entire result
byte for byte; [comparison](comparison.json) and [review](REVIEW.md) also verify
source extraction and all generated controls. No output fields are excluded.
The [11 strict controls](controls.json) alter extracted production functions
and must compile, terminate, and fail only their named functional assertion.
A sanitizer diagnostic, unrelated failure, timeout or compilation failure cannot
satisfy a control. Each private run retains captured inputs, complete generated
variants, compiler output, raw logs and binaries. [Review](REVIEW.md) records
final results and retained draft-fixture failures. [ARM64 evidence](ARM64-BUILD.md)
is a separate compile/modpost check, not installation or execution. The strict
ARM64 build succeeds with 36 actual undefined symbols matched to the pinned
export table. The private 94,432-byte ELF64 AArch64 relocatable module hashes to
`ae6f19b677d0bfce1846863bd30d6e0f893181a7b7023e47a48db4a101c7aa75`;
its original CRC parameter metadata is retained.

## Recovery still needs an owned transaction

The [earlier recovery contract](../emulator-overlay-handback/RECOVERY-CONTRACT.md)
correctly describes the frozen handback candidate's inability to restore mode 0.
This successor supplies that missing primitive only. It does not update that
frozen protocol's broker or select an authoritative original baseline.

Before runtime use, a separate adapter must bind the retained original full
frame/mode/CRC to the exact module, file, native process/start identity and run;
close all producers; classify unknown requests; and obtain a verified scoped
native stop. It must use the exact retained request identity, reconcile uncertain
RESTORE outcomes without blind retries, and keep admission closed while verifying
the resulting mode/frame, instance/run, new epoch and sequence zero.

Both original host files (`mixer.state` and `knob.ctr`) also need exact originals,
absence/mode/identity, writer exclusion and partial-progress accounting. The
kernel and filesystem have no shared transaction. Only verified overlay and
host restoration under retained stop could precede a separately guarded restart
and baseline adoption. Joined worker accounting, a copied status or this new
ioctl cannot authorize any of those steps. Final owner Drop, callback deadlines,
platform termination and native dwell/ownership remain unresolved as documented
by the [inactive exit owner](../emulator-owned-exit/).

No device endpoint, service action, old cleanup, snapshot restore, package,
core or catalog change occurred in this checkpoint. Raw artifacts remain
private. No physical deck access or control occurred.

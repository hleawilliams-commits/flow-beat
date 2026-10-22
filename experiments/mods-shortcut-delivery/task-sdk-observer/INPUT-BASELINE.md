# Emulator-owned mixer counter baseline

This is an offline source/binary audit and a pure receipt/frame validator. It does not read a device, contact the emulator, install the task observer, or change input. The proposed acquisition is one bounded read-only command for 128 bytes from the emulator's mixer SPI node. A valid result would describe the emulator's MISO frame at the time of its locked copy. It would not establish that EP147 read that frame, that the core forwarded it unchanged, or that a native task executed.

## Exact artifacts

The emulator repository is pinned to `06fc981707e69982c0554bb508e0c646c005a979`. The verifier compares both Git objects and working source bytes for the module source, idle-frame header, module Makefile, Dockerfile and build script. Their complete hashes are in [input-baseline-result.json](input-baseline-result.json).

| Artifact | SHA-256 / identifier |
|---|---|
| `guest/modules/subucom_virt/subucom_virt.c` | `9631ca05810408eb9cd40cb38fa3f90d8dd2ac1d4d09a2e73d084ea0dbe03654` |
| `guest/modules/subucom_virt/subucom_frames.h` | `4d9d833e583b7c6a5aa3f12de0fea2b7245e9dac915477ab96b9cfdfa75c2c0b` |
| `build/docker-out/modules/subucom_virt.ko` | `c97d19459a3e99116de70c0f3f781527baf710846a158a9ba051c36537fc338d` |
| Module GNU build ID | `d8e5bc2ea181713ce4d69f8baebec6b23362de2e` |
| `build/Image` and `build/docker-out/Image` | `4aa9e89f0c6f7c0869c94be6ea556e75d4a1093b5dc2cd129c194ff63c2caf08` |
| `build/initramfs-patched.cpio.gz` | `fe98cf4a36c97a080afbec868189982d487318428b69d14b4542750531531eba` |

The module is little-endian ELF64 AArch64 `ET_REL`, with name `subucom_virt` and vermagic `6.6.138-dirty SMP preempt mod_unload aarch64`. Its 36-byte GNU note has `namesz=4`, `descsz=20`, `type=3`, owner `GNU\0`, followed by the exact 20-byte ID. The validator checks the complete note, not a substring.

The pinned build recipe compiles the module against its kernel and the initramfs fixup installs it at `/lib/modules/subucom_virt.ko`; the pinned startup fixup invokes that path. Local image/module/source hashes bind these inspected artifacts. They are not a reproducible-build attestation or proof that the running guest loaded them. The separate guest source stamp was inspected and matched the local source-tree hash, but is only build bookkeeping. No binary, image, raw disassembly, frame, guest receipt or private acquisition script is published here.

## Read-path facts

Source citations below are to `guest/modules/subucom_virt/subucom_virt.c` at the pinned commit.

- Lines 65–72 map SPI names to minors `0,1,2,3`; mixer `subucom_spi3.0` is **minor 2**, with CRC prefix length 96. The current implementation allocates the major dynamically at lines 494–500. The introductory fixed-major/class comments are stale and are not used.
- `spi_open`, lines 216–220, copies the inode minor to that file's private data. `spi_release`, lines 223–225, returns zero. The SPI operations table at lines 357–364 has no write handler. Its ioctl handler does contain mutation paths, so the proposed acquisition invokes no ioctl.
- `spi_read`, lines 228–279, first sleeps in the caller's context. It then holds `miso_lock` while copying 152 bytes from the selected persistent injection overlay or idle template into a stack frame. It leaves `has_inject` and the overlay intact. The service-mode bit adjustment applies only to bus 0 and to the local frame.
- After unlocking, it reads the selected CRC parameter, calculates CRC-16/X25, and stamps the local frame. `min(count,152)` bytes are copied to the caller. A 128-byte request includes byte 34 and CRC bytes 96–97. It does not update a stream position or consume a queued input event.
- `ctrl_read`, lines 436–474, is a different path: it consumes LED/MOSI freshness and advances its selection cursor. It must not be used as a mixer counter baseline.

These source facts are supplemented by the exact module's relocations and decoded function bodies:

| Function | `.text` offset | Size | Binding |
|---|---:|---:|---|
| `spi_open` | `0x0` | 24 | Minor mask and single store to file-private data |
| `spi_release` | `0x18` | 8 | Zero return |
| `spi_poll` | `0x20` | 8 | Constant readable mask |
| `spi_read` | `0x26c` | 612 | Full body decoded; all explicit store instructions target its stack |
| `ctrl_read` | `0x38` | 564 | Separately hashed; excluded as acquisition handler |

The `spi_fops` object is 264 bytes in `.rodata`. Its exact relocations bind owner, read, poll, ioctl, open and release; the read relocation at table offset `0x10` targets `.text+0x26c`, and the write slot at `0x18` is zero with no relocation. Function extents, body hashes and all table relocations are retained in the result. They are module-relative static offsets, not sampled runtime addresses.

`spi_read` has only seven bound external call sites: `memset`, `__usecs_to_jiffies`, `schedule_timeout_interruptible`, `_raw_spin_lock_irqsave`, `_raw_spin_unlock_irqrestore`, `__arch_copy_to_user`, and `__stack_chk_fail`. Its other decoded branches remain within its extent, and there is no indirect branch. Its full decoded body has stack-only explicit stores, including the local copy and CRC. This narrows the binary claim to this body and its call edges. Kernel scheduling, spinlock, VFS open/close/module references, user-copy and failure handlers have their own effects; they were not proved side-effect-free.

An extra acquisition therefore has a real caller sleep, lock acquisition and copy cost. It does not consume the persistent overlay, but is not a zero-impact operation or a hardware timing guarantee. No claim follows about physical-device behavior.

## Pure identity and frame policy

[verify-input-baseline.py](verify-input-baseline.py) exports two import-safe functions. Import performs no I/O; ELF dependencies are loaded only by its offline CLI.

`identity_errors(receipt)` returns failed rule names. A collector must supply:

| Field | Required value |
|---|---|
| `disk_sha256` | Exact module hash above, from `/lib/modules/subucom_virt.ko` |
| `loaded_build_id` | Exact 20-byte ID in lowercase hex |
| `loaded_note_hex` | Entire exact 36-byte GNU note in lowercase hex |
| `module_state` | `live` |
| `node_mode` | Integer raw `stat.st_mode`, character-device type |
| `node_rdev_major`, `sysfs_dev_major`, `proc_devices_major` | Equal integer dynamic majors, `0 < major < 4096` |
| `node_rdev_minor`, `sysfs_dev_minor` | Integer 2 |
| `sysfs_name`, `sysfs_class`, `proc_devices_name` | `subucom_spi3.0`, `subucom_virt_class`, `subucom_virt` |
| `sysfs_class_realpath`, `sysfs_char_realpath` | Both `/sys/devices/virtual/subucom_virt_class/subucom_spi3.0` |
| `crc_lengths` | Four integer parameter values in 0–150, mixer index 2 exactly 96 |

The node is `/dev/subucom_spi3.0`. The collector must resolve both `/sys/class/subucom_virt_class/subucom_spi3.0` and `/sys/dev/char/<observed-major>:2`, and match the character-device row in `/proc/devices`. The loaded note is obtained from `/sys/module/subucom_virt/notes/.note.gnu.build-id`; module state and CRC parameters come from that module's sysfs directory. Absence or mismatch fails closed; there is no reload, node replacement, guessed major or note-free fallback.

`validate_frame(bytes)` requires exactly 128 immutable copied bytes and checks CRC-16/X25 over bytes `[0,96)` against the little-endian word at 96. It raises `ValueError` on rejection. On success it returns only copied integer facts: `length`, `crc_prefix`, `crc16_x25`, `byte6`, and `byte31` through `byte35`. Bytes 98–127 are outside that CRC; the validator explicitly tests that limit.

Acquisition is separate and private. It must check identity before and after one bounded read-only acquisition command, require identical receipts and the same empty native process/original cores, retain any failure, and make no control writes or caller retry. A single `dd` acquisition command is not proof of exactly one kernel read in the presence of signals or internal retries. Path/stat/sysfs receipts are sequential and are not `fstat` evidence bound to the command's opened descriptor. These gates assume exclusive control of the emulator while collecting and do not rule out every intervening mutation. The policy cannot verify the origin or honesty of supplied receipt values.

A single diagnostic acquisition at this checkpoint cannot satisfy a future fresh/repeated input-counter alignment requirement. Its counter must not be carried forward as a later usable baseline; any later input plan needs its own reviewed acquisition and alignment evidence.

The sample is upstream of native/core/community input processing. Concurrent host overlay changes can also replace it immediately after the locked copy. A matching host counter and CRC cannot prove native consumption, current dispatcher counter, task ordering, ownership or final delivery. Missing later task output remains unknown.

## Verification and limits

The first offline run passes **99 checks**, including **51 rejection controls**. Four controls alter extracted table/decoded-body facts to reject a `ctrl_read` substitution, an added write handler, a non-stack store, and an unexpected helper call. The remaining controls cover missing/mismatched identity fields, malformed full GNU notes, device-number type/range/mapping failures, frame length/mutability and CRC corruption. The X25 known-answer vector `123456789 → 0x906e` is checked independently of the synthetic frame. These are static and synthetic checks, not observed guest results or whole-kernel tests.

Run with Python providing `pyelftools` and `capstone`:

```sh
python verify-input-baseline.py --emulator /path/to/xdj-az-emu --result /path/to/new-result.json
```

The result path must not exist; earlier evidence is never overwritten. The verifier reads local pinned sources, Git objects, module and images only. Its result contains source/verifier hashes and explicit proof limits. The first successful result and stdout are retained privately; the public JSON is the same result. An independent root-agent execution of the frozen verifier also passed and produced byte-identical result JSON (SHA-256 `1be70df7ada1c2780a17394799427f6e1551d257faf01a09218d2a528274c113`). Earlier interactive inspection encountered an incomplete DWARF type declaration and a non-note ELF section; those read-only inspection errors supplied no proof and did not alter artifacts. No failed verifier run occurred.

The task observer, its existing tests/build, and all runtime gates remain unchanged. No observer readiness, native task lifetime, new release or completed fix follows from this baseline audit.

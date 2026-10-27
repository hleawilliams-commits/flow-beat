# Independent source review

The reviewed phase-1 candidate has no blocking defect identified within the source scope below. This is an offline review of the patch, UAPI header, complete candidate mutation references and initialization/teardown code. It is not approval to install the module. The reviewer did not execute the candidate, rerun the host fixture, access a device or change candidate sources.

| Reviewed input | SHA-256 |
| --- | --- |
| Pinned original `subucom_virt.c` | `9631ca05810408eb9cd40cb38fa3f90d8dd2ac1d4d09a2e73d084ea0dbe03654` |
| `emulator-overlay-ownership.patch` | `34d870df06f902c04eb0c2029a1c0411e647519ff4a8fabdc17b16ffe8befb3f` |
| Patched `subucom_virt.c` | `09b73c132a245a46e9855888c8cb1ea0fac5e9adba195ed848983b71262a2180` |
| Added `subucom_ownership.h` | `77d0cb71414ec49750040cc22dd93f6553c418cee54f091aa6e4d23686ee43cc` |

## Checked final mutation boundaries

`ctrl_ioctl` copies requests before locking, then validates owner/run/epoch/exact-next-sequence and commits the whole frame under `miso_lock`. Legacy mixer writes recheck the permanent fence after their usercopy. Both single-mixer clear and clear-all check the fence at their final mutation boundary; clear-all refuses before modifying another bus. The other three buses retain their separate legacy write/clear behavior. No public operation clears `mixer_fenced`.

This ordering addresses requests paused before the final lock: a legacy request cannot overwrite a later acquired mixer state; a tagged request cannot survive release/reacquisition with an obsolete epoch; two same-sequence requests cannot both commit. These are source conclusions. Deterministic host schedules and real kernel behavior require their separately scoped evidence.

Ownership uses the open-file description (`struct file *`), not a process identifier. Mutating ioctls require `FMODE_WRITE`, current `CAP_SYS_ADMIN` and the default-off, read-only module option. Duplicated/inherited descriptions share authority; they are not distinct owners. Final `.release` clears only a matching owner and keeps the overlay, CRC setting, epoch, sequence and fence. It takes no extra file reference that would prevent final release. Actual VFS reference lifetime and close behavior are not exercised by this review or a synthetic same-pointer alias.

## CRC, initialization and ABI

The custom setter parses into private allocated storage before locking. Invalid or overlong lists cannot partially modify the live array/count. Under the lock it rejects a supplied mixer prefix other than 96 once fenced. Valid short lists still update their supplied prefix and `n_crc`, leaving the omitted mixer value intact. Acquisition requires the existing mixer prefix 96 instead of changing it. The getter snapshots array/count together; `spi_read` snapshots frame and CRC length under the same lock and restamps its private frame afterward. The revised bounds expression also avoids the original signed `len + 1` overflow for extreme parsed values.

The spinlock has a static initializer, and the CRC callback does not depend on device creation or module-init work. Allocation, parsing, usercopy and formatting occur outside the spinlock. These source properties avoid an obvious early module-parameter initialization dependency or sleeping-under-spinlock path. This reviewer did not build the module or inspect the kernel parameter parser/VFS implementations; the cached header macros were examined for the metadata addendum below. Replacing `module_param_array` with `module_param_cb` changes malformed-update behavior intentionally. Restoring the type-discovery string does not establish complete lexical/format compatibility. Successful decimal values, comma separation and partial-prefix semantics are visible in the candidate.

The header uses explicit fixed-width fields and aligned 64-bit fields. Candidate static assertions cover command/frame/state sizes 32/184/192 and selected offsets. Exact ioctl command matching includes size and direction. Unknown encodings are rejected. No compat handler is supplied; the declared scope is the native 64-bit ABI. CLAIM and RELEASE are input-only, avoiding a post-commit response-copy failure. STATE copies a zero-initialized locked snapshot after unlocking; a failed copyout does not mutate ownership.

## Metadata-only follow-up

The initial review covered patch `2da71a718cd0ffc7fc192ad2281fae52afc954d167a3a5d7e4a578968b1c0919` and candidate C source `28f15393e4f2e9ce2babfbc275b954fc5c7900f08e5edb926f70180938865dbb`. Those preparation/build/test snapshots remain retained privately. A later binary comparison identified the missing original `parmtype=spi_crc_len:array of int` discovery entry.

Independent byte comparison against that retained C source confirms that the final candidate differs only by adding `__MODULE_PARM_TYPE(spi_crc_len, "array of int");` after callback registration. The UAPI header is unchanged. The exact cached `include/linux/moduleparam.h` has SHA-256 `688c307bb7cdec995e47eff4ba4d64aeb78ec7f097383d6c6b1daa1650fd08dd`. Its definitions at lines 23–29 expand this macro into a used, byte-aligned static character array in `.modinfo`; the module prefix is empty. Its original array-registration macro at lines 513–523 emits the same type-discovery entry. This addition does not select `param_array_ops` or change the custom setter/getter.

At this review update, the final ELF comparison and exactly-one-entry check are pending separate build evidence. Source-only reasoning does not assert executable-section identity. The parser lexical/format caveat remains, and no runtime gate changes.

Subsequent [build evidence](ARM64-BUILD.md), including an independent byte parser,
records identical executable sections and exactly one restored type entry. That
is separate executed validation, not a claim made by this source review.

## Limits that remain closed

- STATE returns raw stored overlay or idle-template bytes and a separately captured CRC length. Its bytes are not the CRC-restamped `spi_read` wire frame. A successful FRAME means overlay commitment, not native consumption; STATE can already be stale when copied out.
- Epoch and sequence additions are guarded against overflow. A sequence of `U64_MAX` prevents further tagged operations, including explicit RELEASE; final close can still leave the mixer fenced and unowned. An exhausted epoch prevents later acquisition for that module lifetime. Neither counter is globally unique across module reload or reboot.
- A successful new owner can submit any 152 bytes. This kernel layer does not validate neutral controls, preserve a prior baseline, provide producer handback, drain old preview queues or repair uncertain press/release state.
- Ownership loss retains the last overlay, which may contain a held control. The permanent fence is an exclusion mechanism, not recovery or liveness proof. GPIO/QMP paths and unrelated devices are outside this mixer-overlay boundary.
- The host extraction substitutes kernel services and locks. Its passing checks would not prove kernel compilation, IRQ behavior, usercopy semantics, VFS lifetime, module teardown, native input receipt or hardware timing.

The separate producer handback, module-instance identity and stopped recovery contracts remain prerequisites to installation. No runtime gate is opened by this review.

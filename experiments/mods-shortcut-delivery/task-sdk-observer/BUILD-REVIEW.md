# Independent ARM64 build review — 2026-10-22

Static inspection found no blocker in the compiled task-copy, wrapper-forwarding,
and unwind paths described below. This review read the private ELF and retained
build receipts; it did not execute the module, claim either hook, contact a
device, or rerun the host/ABI suites. It adds evidence about this exact compiled
artifact, not native admission, task lifetime, completion, or physical timing.

## Artifact and evidence provenance

The ELF is a 64-bit little-endian AArch64 shared object with SHA-256
`71d7fc088f4def67d1307e4faeef301020aed04bfa3da7ae4bc6a422ece3e255`.
Its separate validator-input copy has the same hash. All **13** snapshotted
source files and their current counterparts match the source manifest and
[build-result.json](build-result.json), including `observer.cpp`, both assembly
wrappers and the three frozen dependency headers. The copied SDK header matches
the pinned SDK hash there. The retained build and SDK-validator exit receipts
both report zero; the validation output reports success.

The build receipt identifies AArch64 GCC/G++ 7.5.0, the recorded SDK image, `-O2`,
exceptions/unwind tables, and the linked snapshot. The build driver specifies a
networkless container. This is receipt and artifact verification, not an
independent rebuild or proof of the build host's integrity. The existing public
[build-log.txt](build-log.txt) hash matches the build report. Its sanitized hash
is distinct from the private raw build-output hash.

The instruction findings were checked against both the retained disassembly and
a fresh Capstone decode of the ELF bytes. The wrapper FDEs were independently
decoded from the ELF with pyelftools. No local or target runtime addresses are
published here: code offsets below are offsets in this exact module ELF.

Useful private-receipt fingerprints, without publishing their contents:

| Receipt | SHA-256 |
| --- | --- |
| Source manifest | `843d91a5cf83dec832e96e9a95172baa41f9eb460d5b8d07741c4424706a621e` |
| Raw build output | `5439e3b6576866fbde8fef60047efd1d95f4b6d818c9bfadd585bd1b8cbdf06d` |
| Retained disassembly | `8f64124cb49d9d44b0495bb4029f854d837b7d3a9299d5898af8a9eb4d9fa12a` |
| Retained CFI dump | `bc1e40c3e5e42230dd1a3769622f4716ba0a09db0cd13501a11e90a8fd05924c` |
| Retained dynamic-symbol dump | `c35b1521647d4887890768dc1cec379d7a1bcd6c9bd730b401f2c0bd41f3808a` |

## Actual task read ranges

The production `memcpy` default in the frozen capture header is optimized into
four load instructions in `eto::capture`. At this point `x2` is the native task
argument retained across the thread-ID call:

| Module offset | Instruction | Native bytes read |
| --- | --- | --- |
| `0xd1c` | `ldr x3, [x2]` | `[0x00, 0x08)` vptr |
| `0xd3c` | `ldp w3, w1, [x2, #0x28]` | `[0x28, 0x30)` delta and cumulative values |
| `0xd44` | `ldr x5, [x2, #0x10]` | `[0x10, 0x18)` TaskNo |
| `0xd48` | `ldr x4, [x2, #0x20]` | `[0x20, 0x28)` ID |

The vptr comparison precedes the payload branch. Null, alignment and address
range checks precede the vptr load; adapter state/caller/TID and transport
admission checks also precede it. The compiler does **not** widen a load across
the captured dispatcher at `[0x18, 0x20)`, the tag at `[0x08, 0x10)`, or the
object tail. The requested total remains **32 bytes**, comprising an 8-byte
vptr and 24 bytes of disjoint payload. There is no imported `memcpy` call.

The compiler reads delta/cumulative before TaskNo and ID. Neither the source
copy nor these instructions establish a coherent snapshot of concurrent native
writes. The native task must remain valid and suitably immutable throughout the
entry copy; alignment, vptr and caller filters do not establish those lifetime
conditions. This review concerns architectural load ranges, not speculative or
cache-line activity.

`az_task_call_after` reads its 64-byte wrapper-owned value context and the
permanent transport. The full native `x0` return value is moved into helper
argument `x1` without truncation. No native task address is passed to that
helper, recovered from the value context, or dereferenced after return.

## Linked forwarding and CFI

The submission wrapper occupies `[0x1c40, 0x1dec)` (428 bytes). Its native call
sequence loads the address of the permanent `az_task_call_original` slot through
the relocated GOT, performs `ldar x16, [x16]`, then `blr x16`. The separate slot
relocation targets the module's actual original-storage object. The wrapper
retains its 416-byte stack frame during the native call, preserves its 64-byte
value context there, and calls the after-helper only on normal return. At the
epilogue it restores original LR and SP before returning.

The executor wrapper occupies `[0x1dec, 0x1ea4)` (184 bytes). It passes original
LR alongside the incoming task to the observation helper, restores input
registers, status registers, LR and its 320-byte stack allocation, then loads its
own permanent original slot with `ldar` and uses `br x16`. It has no after-helper
and does not assign a meaning to the native result. The linked C helpers retain
the errno save/restore operations. Submission also retains its wrapper-owned
errno saves around before/after observation.

The actual `.eh_frame` entries cover those complete wrapper extents:

| Wrapper | Active CFA | Saved original LR | Epilogue |
| --- | --- | --- | --- |
| Submission | `SP + 416` | `CFA - 264` after the save | LR rule restored, then CFA returns to `SP + 0` |
| Executor | `SP + 320` | `CFA - 168` after the save | LR rule restored, then CFA returns to `SP + 0` before the tail branch |

This confirms linked metadata agrees with these assembly stack operations. It
is not another exception/backtrace execution result. The existing ABI fixtures
and the separate [hook-lifetime audit](../task-hook-lifetime/README.md) retain
their own scopes: this module review does not cover dynamically allocated hook
islands/trampolines, asynchronous interruption there, or every installed-path
register effect. Wrapper transfer uses `x16`; the core island/trampoline also
uses `x17`. Original-slot acquire loads do not replace the adapter's separate
configuration/READY publication protocol.

## Atomics and remaining limits

Neither the dynamic nor full symbol table contains `__atomic_*`, `__sync_*` or
`__aarch64_*` helper symbols. Reviewed atomics compile to inline load/store
exclusive operations and acquire/release loads/stores. The strong CAS and
counter operations include machine-level exclusive-store retry branches. One
source-level reservation attempt and lock-free types therefore do not prove
wait-free execution or a time bound under contention.

The module still imports libc/TLS/syscall services, and uses SDK logging and
thread services indirectly. Absence of an atomic helper import is not proof of
allocation-free first use, nonblocking logging, a functioning worker, or safe
unload. [LIFECYCLE.md](LIFECYCLE.md) remains unchanged and governs those separate
source-level observations. No menu behavior, recovery activation, runtime
installation, whole-engine lifetime result, or release follows from this static
review.

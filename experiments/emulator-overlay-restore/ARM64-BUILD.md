# Offline ARM64 build of the restore primitive

The separate restore candidate compiles and links with **strict modpost and no
compiler warnings** against the retained ARM64 Linux 6.6.138 configuration. The
private module is 94,432 bytes, SHA-256
`ae6f19b677d0bfce1846863bd30d6e0f893181a7b7023e47a48db4a101c7aa75`.
All 36 actual undefined symbols match names in the pinned 14,223-entry kernel
export table. Compared with the frozen handback module, only `memcmp` is added;
no symbol is removed.

**No module was installed or loaded. No emulator or physical deck was contacted.**
The runtime gate remains closed. [build-result.json](build-result.json) records
source, artifact, compiler and evidence hashes. Raw archives, prepared sources,
receipts, scripts, logs and module bytes remain private.

## Inputs and isolation

A fresh source tree exported the pinned emulator Git blobs, then applied all
three captured patches with zero fuzz and checked the exact four-file inventory
after each stage. Neither the original emulator nor the frozen parent experiments
was edited.

| Input | SHA-256 or identity |
| --- | --- |
| Emulator base | `06fc981707e69982c0554bb508e0c646c005a979` |
| Restore patch | `fdf40df07b76e22f1684186e4d725ccc5d1cc56e24d5cb9a7f7813534a045352` |
| Candidate C | `5ea2023ccb8e058fad66b7317d545fc7a0328bcc5aae624cfc18c9769cf87124` |
| Candidate UAPI header | `a9f2cfa4d007053806ca97f3058c72bffd43756995cb23235c415358a3c7e990` |
| Linux tag / commit | `v6.6.138` / `3b9f64db049687c0d38b4b3ef2f297f0642179af` |
| Prepared configuration | `93b0bcf3419d18cd5fbc86eed505252ee6c7207cf58241fa0d0a79d02d5d1dac` |
| Retained export table | `dd157a5cd924cba495402cccb326d169e5c63f9252c13b5ca6d9535e0c08c329` |
| Local kernel/toolchain archive | `898432b5244d53de1b28943b829859d249241bf88b750337c2a9871d138ab54c` |

The complete archive and eight selected kernel/toolchain members were verified,
including the GCC hard-link target. The compiler reports
`gcc (Alpine 13.2.1_git20231014) 13.2.1 20231014`.

The unchanged reviewed recipe uses two `FROM scratch` stages, no remote frontend
or base image, and no downloads. Execution used `--network none`, `--pull=false`
and `--platform linux/arm64`. The archive layer was reused from the local cache;
the new candidate compilation, modpost and link executed in a fresh layer. The
retained `vmlinux.symvers` was copied to `Module.symvers` only inside that layer.
`KBUILD_MODPOST_WARN` was not used. Inputs matched before and after the single
successful build.

## ABI and byte inspection

The output is ELF64 little-endian AArch64 `ET_REL`, with vermagic
`6.6.138-dirty SMP preempt mod_unload aarch64`. Compile-time assertions retain the
48-byte V2 command, 200-byte frame and 208-byte state, and enforce the new
208-byte restore request with mode/reserved/frame at offsets 48/52/56. The new
operation has its own number, `0x55`; the prior operation definitions remain.
There is no compat ioctl handler.

A separate standard-library ELF parser confirmed the actual symbol table against
both the build verifier and the retained exports. Parameter descriptions and
types are unchanged from handback, including exactly one
`parmtype=spi_crc_len:array of int`. An independent parent review separately
confirmed the binary hash/size, ELF identity, CRC parameter type and vermagic.
That parent inspection did not repeat export matching or the build.

## Retained preparation failure

The initial archive inspector stopped before source preparation or compilation
because it incorrectly required `usr/bin/gcc` to be a regular tar member. The
archive represents it as a hard link to the pinned compiler executable. The
initial script and explicitly labeled observed-error excerpt are retained;
a separate complete stderr file was not captured. The corrected inspector
verified the exact link and target hash in a new context. No candidate code was
changed, and the subsequent build passed on its first attempt.

## Limits

Compilation and export-name matching do not execute module initialization, an
ioctl, Linux locking, VFS lifetime or native input. They do not prove module
loadability or identity with a running kernel. The restore primitive does not
attest a stopped application, drained producers, authentic original baseline,
host-file restoration or safe restart. These remain separate recovery and
authority requirements; this build supplies no enablement or teardown permit.

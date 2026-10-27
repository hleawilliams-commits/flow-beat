# Offline ARM64 build of the instance/generation successor

The separate V2 candidate passes compilation and **strict modpost without
warnings** against the retained ARM64 Linux 6.6.138 kernel configuration. Its
private module is 93,424 bytes, SHA-256
`bf8293addeb6570498965d4958db849c5e79b7e40f8f600772ed825f878a3184`.
All 35 actual undefined module symbols have named entries in the retained
14,223-entry kernel export table.

**The module was not installed or loaded. No emulator or physical deck was
contacted. Runtime remains closed.** The [machine-readable result](arm64-result.json)
contains the complete symbol list and exact source, config, compiler and evidence
hashes. Kernel source copies, rootfs archive, build scripts, raw receipts/logs and
binary artifacts remain private.

## Inputs and isolation

A fresh private source copy applied the frozen phase-one patch and then the
successor patch, with exact input/output fingerprints and no fuzz. The original
emulator checkout and phase-one source/build artifacts remain unchanged.

| Input | Identity |
| --- | --- |
| Emulator base | `06fc981707e69982c0554bb508e0c646c005a979` |
| Phase-one patch SHA-256 | `34d870df06f902c04eb0c2029a1c0411e647519ff4a8fabdc17b16ffe8befb3f` |
| Successor patch SHA-256 | `fa800f32c5e9227cc1953028d35b896e711a40b0d4fd6f2e03dcdf2ac6eaf0dd` |
| Successor C source SHA-256 | `bab273a308dde0ba696394d2383461af10413f1975e836b2058456dba2833717` |
| Successor UAPI header SHA-256 | `355316f5047509ae4c787210fddf0bd8b659c600ac1e153adf0543e28cf35aec` |
| Linux tag / commit | `v6.6.138` / `3b9f64db049687c0d38b4b3ef2f297f0642179af` |
| Prepared kernel config SHA-256 | `93b0bcf3419d18cd5fbc86eed505252ee6c7207cf58241fa0d0a79d02d5d1dac` |
| Retained export table SHA-256 | `dd157a5cd924cba495402cccb326d169e5c63f9252c13b5ca6d9535e0c08c329` |
| Compiler | `gcc (Alpine 13.2.1_git20231014) 13.2.1 20231014` |
| Compiler executable SHA-256 | `d2fcfbe5ea467cbb15e6e4b0d1a0ee929575de38f5e9db5313def1b06c8b9fca` |
| Linker | `GNU ld (GNU Binutils) 2.41` |

The build reused the exact private local kernel/toolchain archive recorded in
[phase one's build evidence](../emulator-overlay-ownership/ARM64-BUILD.md).
Its two-stage recipe uses `FROM scratch`, no remote frontend or base image, no
URL inputs and no downloads. Build execution used `--network none`,
`--pull=false` and `--platform linux/arm64`. The original archive was not changed.
All selected source/archive hashes remained identical before and after the build.

The retained `vmlinux.symvers` was copied to `Module.symvers` only inside the new
isolated build layer. Kernel compilation used `ARCH=arm64`, `-j2` and `M` pointing
to the private candidate. `KBUILD_MODPOST_WARN` was not used. Compilation,
modpost and linking completed with zero warnings.

## ABI and export evidence

The output is ELF64 little-endian AArch64 `ET_REL` (machine 183), with vermagic
`6.6.138-dirty SMP preempt mod_unload aarch64`. The source's compile-time
assertions enforce the V2 48-byte command, 200-byte frame and 208-byte state,
including the reviewed offsets. This is the native 64-bit ABI; the module has no
compat ioctl handler.

Compared with the frozen phase-one module's 33 undefined symbols, exactly two
are added: `get_random_bytes` and `wait_for_random_bytes`. None is removed.
Both are exported by the pinned kernel. Compared with the original emulator
module's 26 symbols, the complete difference is ten additions and the removal
of `param_array_ops`; those names are recorded in [arm64-result.json](arm64-result.json).
No actual undefined symbol is absent from the retained table. These are export
name matches, not runtime symbol resolution.

The CRC array type metadata preserved in phase one appears exactly once as
`parmtype=spi_crc_len:array of int`. All parameter descriptions/type entries
remain unchanged from phase one, including the default-off ownership parameter.

A separate independent standard-library ELF parser reproduced the final hash,
size, architecture/type, actual symbol-table list, export matches, two-symbol
delta, parameter metadata and vermagic. This was a byte inspection, not module
execution. No second unchanged build was needed for that check.

## Limits

The [ABI review](ABI-REVIEW.md) separately checks the explicit random-readiness
wait and failure boundary before device publication, instance comparison under
the mutation lock and same-owner generation rotation. Neither source review nor
this build executes kernel initialization, the RNG wait or any ioctl. It does
not demonstrate entropy quality, unique identities, snapshot/clone protection,
module loadability, VFS lifetime, lock behavior or native input consumption.

`ROTATE` installs an opaque frame and revokes old tagged generations. It does not
certify frame neutrality, producer queue freshness, cancellation or a stopped
application. Producer tagging and [stopped recovery](RECOVERY-CONTRACT.md) still
need separate integration and evidence. This build creates no runtime identity,
enabling manifest, installation authorization or reason to enable the closed V2
runner. It is not an AZ-MODS core or community release.

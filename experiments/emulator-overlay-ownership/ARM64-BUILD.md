# Isolated ARM64 kernel-module build

The final ownership candidate compiles for the retained ARM64 Linux 6.6.138
configuration with **strict modpost and no compiler warnings**. Its 33 undefined
module symbols all have named entries in the pinned kernel export table. The
final private `subucom_virt.ko` is 92,224 bytes, SHA-256
`bfc33fb721cf292b1472ac4c9cca6687a818624f5147f31f18b17a3c4503aaa2`.

This is offline build and ELF evidence. **No module was installed or loaded, no
emulator or physical deck was contacted, and the runtime gate remains closed.**
The machine-readable [result](arm64-result.json) records the complete symbol list,
input fingerprints, independent review and limitations. Raw kernel sources,
rootfs archive, build scripts, command receipts, logs and module binaries remain
private.

## Exact source and toolchain

| Input | Pinned identity |
| --- | --- |
| Emulator base commit | `06fc981707e69982c0554bb508e0c646c005a979` |
| Original module source SHA-256 | `9631ca05810408eb9cd40cb38fa3f90d8dd2ac1d4d09a2e73d084ea0dbe03654` |
| Final patch SHA-256 | `34d870df06f902c04eb0c2029a1c0411e647519ff4a8fabdc17b16ffe8befb3f` |
| Final module source SHA-256 | `09b73c132a245a46e9855888c8cb1ea0fac5e9adba195ed848983b71262a2180` |
| UAPI header SHA-256 | `77d0cb71414ec49750040cc22dd93f6553c418cee54f091aa6e4d23686ee43cc` |
| Linux tag / commit | `v6.6.138` / `3b9f64db049687c0d38b4b3ef2f297f0642179af` |
| Prepared kernel config SHA-256 | `93b0bcf3419d18cd5fbc86eed505252ee6c7207cf58241fa0d0a79d02d5d1dac` |
| Retained kernel export table SHA-256 | `dd157a5cd924cba495402cccb326d169e5c63f9252c13b5ca6d9535e0c08c329` |
| Compiler | `gcc (Alpine 13.2.1_git20231014) 13.2.1 20231014` |
| Compiler executable SHA-256 | `d2fcfbe5ea467cbb15e6e4b0d1a0ee929575de38f5e9db5313def1b06c8b9fca` |
| Linker | `GNU ld (GNU Binutils) 2.41` |

The prepared configuration differs from the original seed after kernel
configuration processing; both hashes are retained in the result. It enables
module unloading and ARM64 compat support, while module versions and module
signatures are disabled. The candidate supplies no compat ioctl handler.
Its vermagic is `6.6.138-dirty SMP preempt mod_unload aarch64`.

## Build boundary

Both builds used local prepared kernel/toolchain bytes and a fresh candidate
copy. The recipe has two `FROM scratch` stages, no remote frontend directive,
no remote base image, no URL inputs and no package installation or downloads.
The build used `--network none`, `--pull=false`, `--platform linux/arm64` and a
private local artifact export. Input hashes were checked before and after each
build and remained unchanged.

The prepared tree retained `vmlinux.symvers` but lacked `Module.symvers`. The
recipe copied that exact 14,223-entry export table to `Module.symvers` **only in
the fresh build layer**, then invoked the kernel build with `ARCH=arm64`, `-j2`
and `M` pointing to the private candidate. It did not use
`KBUILD_MODPOST_WARN`. Compilation, modpost and module linking all completed
without warnings. The original emulator checkout and its retained build outputs
were not modified.

The resulting ELF is a little-endian ELF64 AArch64 relocatable object
(`ET_REL`, machine 183). All 33 undefined symbols match entries in the retained
export table; this is name compatibility, not runtime symbol resolution. The
baseline module has 26 undefined symbols. The change adds eight and removes one:

| Added undefined symbols | Removed undefined symbol |
| --- | --- |
| `capable`, `kfree`, `kstrdup`, `kstrtoint`, `memcpy`, `param_ops_bool`, `scnprintf`, `strsep` | `param_array_ops` |

The complete 33-name list is in [arm64-result.json](arm64-result.json). No symbol
was absent from the pinned export table.

## Retained metadata finding and correction

The first build passed strict modpost, producing private binary
`9f9e1207aea4f1d87f5f99a9666c7ff6fc7f086acb48e1631ea46f2d462c33ba`.
Independent ELF review found that replacing `module_param_array` with the
transactional callback had dropped the existing `spi_crc_len` array type label.
That first source, context, binary and evidence remain retained as historical
results.

The exact cached `moduleparam.h` confirms that `module_param_array_named` emits
`__MODULE_PARM_TYPE(name, "array of " #type)`, and that this macro expands only to
a constant string in `.modinfo`. The final source adds
`__MODULE_PARM_TYPE(spi_crc_len, "array of int");` after the callback declaration.
A second isolated strict build verified exactly one
`parmtype=spi_crc_len:array of int` entry, with every other `.modinfo` entry
unchanged. All five sections carrying `SHF_EXECINSTR` are byte-identical to the
first build, and the undefined-symbol list is unchanged.

Independent standard-library parsing of the final binary reproduced its ELF
identity, the five section comparisons, exact metadata change and the 33 export
matches. This review did not execute the module. Its first path lookup was
incorrect and failed before opening the final ELF; the corrected known path
passed, with no content assertion failure.

## Limits

Build success does not prove identity with a running kernel, module loadability,
module initialization, ioctl behavior, VFS/open-description lifetime, Linux
locking behavior, native input consumption or physical timing. The byte
comparison covers the stated executable sections and metadata, not a claim that
all ELF bytes are identical. The [host tests](HOST-TESTS.md) remain separate
executions with kernel-service shims.

The candidate still lacks a proved producer handback and stopped recovery
protocol. Owner loss can retain held input. This build supplies no runtime
identity, enabling manifest, installation approval or reason to reopen the
closed V2 runner. It is an emulator-kernel experiment, not an AZ-MODS core or
community release.

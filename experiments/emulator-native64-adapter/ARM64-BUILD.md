# Offline ARM64 compile and shared link

The frozen adapter, production syscall boundary and retained broker compile as
three separate ARM64 objects. A shared-library link with
`-Wl,--no-undefined` also passes. All three compile logs and the link log are
empty; the first build attempt passed.

The private shared library is 72,464 bytes, SHA-256
`56d3b80c6ef57fe6b1129cfac7283e1639f5e09c63fa9898a40de16ae7d9f15a`.
It is ELF64 little-endian AArch64 `ET_DYN`. The three inputs are `ET_REL` objects:

| Object | Bytes | SHA-256 |
| --- | ---: | --- |
| `adapter.o` | 5,200 | `010ce1333dfce202b20908eb7e7668df6a3c657609ca5a2a49c9c31ca93f3bae` |
| `native.o` | 2,008 | `44caf36913812ab50a0a6350a1939d98d51daab6fd3e904b614db3db14609b28` |
| `broker.o` | 12,976 | `80aa6bf60f98c59d72176c6bebd670688590142651bdbbedcdac7a7511c49fd9` |

The broker object is byte-identical to the frozen parent build. **No object or
library was loaded or executed.** No adapter syscall, backend callback, emulator,
device or installation was invoked. Runtime remains closed. Complete source,
artifact and evidence fingerprints are in [build-result.json](build-result.json).

## Exact inputs and compile boundary

A fresh private context captured the four frozen adapter sources, frozen
broker-restore C/header and pinned restore UAPI. Every source was checked before
copying and again inside the build. GCC and the retained musl provider were also
hash-checked inside the build. Context hashes matched before and after.

Compilation uses `-std=c11 -O2 -fPIC -Wall -Wextra -Werror -pthread -c`, plus a
source include path and compiler dependency-file output. Those dependency files
confirm the real Linux `linux/types.h`, `linux/ioctl.h`, AArch64 `asm` headers and
`sys/ioctl.h`; no host test shims were used. The adapter's layout, offset, flag
and ioctl-encoding assertions therefore compiled against the actual retained
Linux headers.

The compiler is `gcc (Alpine 13.2.1_git20231014) 13.2.1 20231014`, with GNU ld
2.41, from the exact local archive
`898432b5244d53de1b28943b829859d249241bf88b750337c2a9871d138ab54c`.
The two-stage scratch recipe has no remote frontend/base or downloads and ran
with `--network none --pull=false`. The local archive layer was cached; new
compilation and linking executed in a fresh layer. Frozen parents and the
original emulator were unchanged.

## Actual dynamic symbols

Direct standard-library ELF parsing agrees with `nm`/`readelf`. The shared
library has one `DT_NEEDED`: `libc.musl-aarch64.so.1`. It has no RPATH, RUNPATH or
SONAME. All 18 strong dynamic import names exist in the exact retained musl
provider, SHA-256
`7d605adcd1cdc4d37251f0e8e3099954e13554323aabfd5e9cf47c10ff63b76c`.
This includes `fcntl`, `ioctl`, `close`, errno access, the existing libc routines,
pthread mutex functions and stack-guard support.

Five weak imports remain: `_ITM_deregisterTMCloneTable`,
`_ITM_registerTMCloneTable`, `__cxa_finalize`, `__deregister_frame_info` and
`__register_frame_info`. Only `__cxa_finalize` is supplied by this musl ELF.
`--no-undefined` permits the other weak references; this is not a claim that the
final library contains no undefined dynamic symbols.

The dynamic exports are exactly the ten `mb_*` broker functions, the three
`mba_attach`/`mba_get_backend`/`mba_dispose` functions, and toolchain
`_init`/`_fini`. The three `mba_sys_*` definitions are global-hidden in `native.o`,
local in the linked library and absent from dynamic exports. No additional
needed library or unexpected export was found in this artifact.

## Limits

The link establishes compatibility with these retained link inputs, not a
successful runtime load or firmware/libc ABI match. It does not execute the adapter's libc calls,
fd duplication/close, Linux usercopy or any kernel handler. It cannot authenticate
the controller, prove exclusive mutation authority or validate live errno
classification, native stop, original-baseline authority, unknown-operation
resolution, host restoration or guarded restart. Raw binaries, provider bytes,
source snapshots, scripts, commands and logs remain private. This evidence grants
no runtime, recovery or teardown permission.

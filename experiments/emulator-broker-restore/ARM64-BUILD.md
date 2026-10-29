# Offline ARM64 broker restore compilation

The frozen broker successor compiles with
`-std=c11 -O2 -fPIC -Wall -Wextra -Werror -pthread -c`. The compile log is empty.
Its private `broker.o` is 12,976 bytes, SHA-256
`80aa6bf60f98c59d72176c6bebd670688590142651bdbbedcdac7a7511c49fd9`.
Direct byte inspection identifies ELF64 little-endian AArch64 `ET_REL`
(machine 183).

**This check did not link or execute the object**, invoke backend callbacks,
issue an ioctl or contact an emulator/device. Runtime remains closed. The
[sanitized result](build-result.json) records exact inputs, flags, artifact and
private evidence hashes.

| Input | SHA-256 |
| --- | --- |
| Frozen `broker.c` | `f331713ac361f265b37c9e781f85400691195915076ca2604aeb2311fd12cbb7` |
| Frozen `broker.h` | `04efb6b1eb22e56798837ae8ff72514a5a513da2d616215d77d4c8f6a85033c8` |
| Local toolchain archive | `898432b5244d53de1b28943b829859d249241bf88b750337c2a9871d138ab54c` |
| GCC executable | `d2fcfbe5ea467cbb15e6e4b0d1a0ee929575de38f5e9db5313def1b06c8b9fca` |

The compiler reports
`gcc (Alpine 13.2.1_git20231014) 13.2.1 20231014`. The fresh private context
adapts the [frozen broker recipe](../emulator-mixer-broker/ARM64-BUILD.md) only
for the selected source hashes. It uses two `FROM scratch` stages, no remote
frontend/base or downloads, and `--network none --pull=false`. The exact local
archive layer was cached; compilation of the new source executed in a fresh
layer. GCC and both source hashes were checked inside the build. Selected
context hashes matched before and after; the public, copied and compiled source
pairs also match. The first and only build attempt passed.

A separate standard-library ELF parser agrees with `nm` on all 14 unresolved
dependencies, unchanged from the earlier broker:

```text
__stack_chk_fail
__stack_chk_guard
calloc
free
memcmp
memcpy
memset
pthread_mutex_destroy
pthread_mutex_init
pthread_mutex_lock
pthread_mutex_unlock
pthread_mutexattr_destroy
pthread_mutexattr_init
pthread_mutexattr_settype
```

The defined global function set adds exactly `mb_restore` and removes no prior
function. These symbol checks do not validate linking, calling ABI compatibility
or backend correctness. In particular, the broker's semantic structures are not
ioctl wire structures; this compile supplies no wire-adapter evidence.

Raw context/archive, source snapshots, commands, logs, scripts and object bytes
remain private. No original or frozen parent file was edited. Behavioral tests
are separate; this build proves no native-stop condition, original-baseline
authority, producer quiescence, settings restoration, callback timing or safe
restart. It creates no recovery or runtime permission.

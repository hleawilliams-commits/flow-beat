# Offline ARM64 broker compilation

The frozen broker source compiles to an ARM64 relocatable object with
`-std=c11 -O2 -fPIC -Wall -Wextra -Werror -pthread -c`. The compile log is empty.
The private `broker.o` is 10,640 bytes, SHA-256
`37aef6bdd44abb21472f90776ede920ae8b904d02cbd51ceb7e3ab1378bd2829`.
It is ELF64 little-endian AArch64 `ET_REL` (machine 183).

This check **did not link or execute the object**, invoke backend callbacks or
contact a device. It does not validate runtime libc/firmware ABI compatibility,
mutex timing, ioctl behavior or process lifetime. The behavioral interaction
fixture is separate. Runtime remains closed.

| Input | SHA-256 |
| --- | --- |
| `broker.c` | `6d19795439378bd4c196b53e556bca2d67488a4ec9097177792d573541317cb8` |
| `broker.h` | `5d854db2b15d5130ce2c47c6e48f8b9c20508c9d0f3cd5de8ea2f990ca1fbc32` |
| GCC executable | `d2fcfbe5ea467cbb15e6e4b0d1a0ee929575de38f5e9db5313def1b06c8b9fca` |

The compiler is `gcc (Alpine 13.2.1_git20231014) 13.2.1 20231014`, from the same
pinned private local toolchain archive used for the isolated kernel candidates.
A fresh private source context used two `FROM scratch` stages, no remote
frontend/base or downloads, and `--network none --pull=false`. The compiler and
source hashes were checked inside the build; all selected context input hashes
were checked before and after and remained unchanged. Raw context/archive,
scripts, logs, source copies and binary stay private.

Direct ELF symbol-table parsing matched `nm` on these 14 unresolved dependencies:

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

Those names are not a claim that linking or runtime resolution has passed.
The [sanitized result](arm64-result.json) retains exact flags, artifact/source
hashes and limits. The [broker contract](BROKER-CONTRACT.md) separately defines
callback truth, admission, unknown-result and external lifetime requirements.

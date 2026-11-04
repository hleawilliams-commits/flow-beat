# ARM64 retained stream compile and link

The retained stream and graph now compile and link together for Linux ARM64.
One fresh build compiled ten production units and linked two shared libraries
from the same objects with strict warnings and no undefined link errors. All ten
compile logs and both link logs were empty. The independent byte-only audit
passed on its first invocation; no failed build or audit attempt occurred in
this checkpoint. [Sanitized result](build-result.json) records the exact inputs.

Nine units are unchanged; the copied wire source retains only the existing
[four-line whitespace patch](../emulator-owned-graph/wire-gcc-format.patch).
All 21 inherited graph source files and all eight inherited graph object files
match the earlier verified build byte for byte. The two added units are the
unchanged stream.c and actual POSIX stream_native.c. No parent host suite was
repeated, and the historical graph failures remain in their original evidence.

The [export map](stream-exports.map) preserves all seven graph APIs and adds
eight stream APIs. Keeping mbog_exchange is an intentional compatibility
choice; this fifteen-function surface is not claimed to be minimal or an
alias/security boundary. The diagnostic baseline exposes 61 production APIs
and two toolchain entries. All six syscall wrappers remain hidden/local.

Both shared libraries import the same 20 strong and five weak symbols.
The only new strong imports relative to the graph build are read and write;
every strong symbol exists in the retained musl provider. There is one
DT_NEEDED, libc.musl-aarch64.so.1, and no RPATH/RUNPATH. The audit checks
direct ELF parsing against captured nm/readelf output for ten objects, both
libraries, the compiler and provider. All 39 actual system-header dependencies
are copied and compared with the pinned rootfs bytes, including the real
Linux ioctl, pthread and POSIX stream headers. Source, map and compiler/provider
hashes match before and after the build. Actual dependency headers were captured
after compilation and checked again after linking; their copied bytes match
the immutable retained rootfs.

| Retained item | SHA-256 |
|---|---|
| Candidate shared library | d3879cb69a63a1741a98cd9ec3363c3625de8d2bba221bbd9b06ff563bc3c5d1 |
| Baseline shared library | 30dbfe2682a538e85a0d95887a3f441501f71d6f241b5feb4ae52aef9c43a864 |
| Independent private audit result | cb91c320a64bcef616ddb42a32e37d8e9c94ba24daef0214d2fd66e94890520d |

The build used only the retained local toolchain with container networking
disabled. Raw scripts, logs, binaries and rootfs stay private. Neither produced
ARM64 library was loaded or executed. Host socket/process execution remains
the separate [process fixture](../emulator-stream-process/) evidence; it does not
establish target runtime behavior.

This closes the target compile/link gap, not the guest-service contract.
It proves no firmware-libc match, native acquisition, SIGPIPE survival, blocked
I/O deadline, alias exclusion, teardown authority or durable recovery.
The [source-only service review](GUEST-SERVICE-REVIEW.md) identifies the existing
forwarder's reconnect/restart behavior, incompatible control-stream protocol
and supervisor EOF/error handling. The next concrete source slice is the
supervisor observation/event branch, reusing retained custody and distinguishing
actual exit from polling errors. No endpoint, main wiring, installation, core,
settings, catalog or physical-deck operation occurred. All runtime/product
gates remain CLOSED.

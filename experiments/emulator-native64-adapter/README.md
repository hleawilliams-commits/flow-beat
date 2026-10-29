# Offline native64 controller adapter

This separate C adapter connects the frozen [RESTORE broker](../emulator-broker-restore/)
to the exact [RESTORE kernel UAPI](../emulator-overlay-restore/) through native64
Linux libc calls. It is uninstalled and unconnected to the preview or runtime
runner. All live dispatch and recovery gates remain **CLOSED**. Frozen parents,
official cores and community packages are unchanged.

`mba_attach` duplicates an already-claimed controller fd with `F_DUPFD_CLOEXEC`,
then requires one complete owned STATE equal to every expected field and all
152 bytes. Subsequent callbacks use that private reference, so closing and
reusing the caller's original fd number cannot redirect them. The duplicate is
not exclusive ownership: other original, duplicated or inherited references can
still mutate the same session. The caller must keep the original fd stable
through attachment, including failure cleanup, and must establish live driver
provenance and sole-mutator custody separately.

FRAME, ROTATE and RESTORE have explicit typed wire translations. Each callback
issues one ioctl. Only a zero result is COMMITTED; the pinned input-only
handler's known precommit errors are REJECTED. Unexpected results and other
errors remain UNKNOWN, with no retry, implicit query or automatic disposal.
Failed or malformed STATE replies publish no fields. `mba_dispose` is a
separate terminal lifecycle action with external preconditions, not a recovery
operation or permission to close an unresolved session.

The focused host matrix passes **137 checks per plain, ASan/UBSan and TSan
mode**, plus **13 exact negative controls per mode** and strict standalone
adapter compilation. A fresh independent run matches the complete result byte
for byte. ARM64 Linux compilation of the adapter, native wrappers and frozen
broker, followed by a shared link, passes with real Linux headers. Nothing from
that build was loaded or executed.

Read [the contract](ADAPTER-CONTRACT.md), [host evidence](HOST-TESTS.md),
[independent review](REVIEW.md) and [ARM64 build evidence](ARM64-BUILD.md).
Run the focused fixture with a legitimate local emulator source checkout:

```sh
python3 experiments/emulator-native64-adapter/run-tests.py --emu ../xdj-az-emu
```

The runner exports pinned Git blobs into a fresh ignored directory, applies
the three frozen patches, and extracts seven exact kernel methods. Generated
source, logs, binaries and build inputs remain private. Passing parent suites
are not repeated. No emulator or physical device endpoint is contacted.

## Remaining gates

Native libc wrappers have compile/link and source-review evidence only. The
host fixture substitutes a serial fd table and syscall boundary; it does not
execute Linux VFS, actual user-memory faults, cancellation, fork or native
application input. TSan adds no concurrency proof for this serial fixture.
There is no actual preview/FFI integration, acquisition/CLAIM coordinator,
authoritative original baseline, native stop authority, durable journal,
two-file host-settings restoration or guarded restart. Final Drop/process
death can still lose custody; callbacks can block indefinitely. An exact STATE
does not prove native consumption, event ownership, audio quality or timing.
Historical staging failures and all other unresolved gates are preserved.

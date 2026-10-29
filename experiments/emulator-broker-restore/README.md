# Offline broker RESTORE successor

This separate C library adds retained RESTORE accounting to the frozen
[mixer broker](../emulator-mixer-broker/) for the separate
[kernel RESTORE candidate](../emulator-overlay-restore/). It is uninstalled,
unwired and has no native observations. All runtime gates stay **CLOSED**.
The original broker, preview experiments, kernel patches and emulator checkout
are preserved. This is source for a separately compiled successor; enlarged
semantic structs are not a binary-compatible update to old consumers and are
not ioctl wire structures.

A restore request contains an independent monotonic ID, a complete expected
source state, the selected injected/idle mode, and all 152 target bytes. The
broker copies it before clearing output or calling the backend, checks closed
admission and a fresh exact source snapshot, retains the exact kernel command,
and attempts that command once. No backend operation is silently retried.
An exact last duplicate returns its historical receipt without backend access;
a different request with that ID is a conflict. A later receipt replacing the
last one does not reset the restore ID sequence.

Both owned states (flags23 idle and flags31 injected) can be inspected and
restored. Producer adoption requires injected mode. A successful RESTORE advances
the epoch, resets the kernel sequence to zero, clears the old producer grant and
leaves admission closed. FRAME and ROTATE explicitly predict injected mode.
UNKNOWN blocks new work until a complete exact positive state reconciles the
retained expected result. An older state is not cancellation.

Read [the contract](BROKER-CONTRACT.md), [test evidence](HOST-TESTS.md),
[independent review](REVIEW.md) and [ARM64 object evidence](ARM64-BUILD.md).
To run the focused offline fixture with a legitimate local emulator source:

```sh
python3 experiments/emulator-broker-restore/run-tests.py --emu ../xdj-az-emu
```

The runner exports pinned Git source into a fresh ignored directory, checks
frozen parent hashes, applies the frozen patches and extracts seven exact kernel
methods. Raw generated source, logs and binaries remain under `private/`.
It does not contact, start, install into or control an emulator or physical deck.
The frozen parents' passing suites are not rerun.

## What remains open

This is request bookkeeping, not a stopped-recovery coordinator. It has no
live fd backend, wire/FFI adapter, original-baseline authority, native process
stop proof, host-file restoration/writer exclusion, guarded restart or durable
journal. The caller can still reauthor old bytes under a fresh ID/source. A
broker recreated after process death cannot infer or recover a lost pending
request. Backend truth and sole-mutator custody remain required assumptions.
Callbacks can block indefinitely. An earlier native SPI sample can complete
later; exact STATE is not native consumption or physical timing evidence.
No observer, core, community package, catalog or release is changed.

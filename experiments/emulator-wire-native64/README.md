# Offline wire → native64 composition

The existing C wire dispatcher now has focused integration evidence through the
actual frozen broker, native64 adapter and seven exact extracted driver methods.
No production source changed. This experiment adds a test harness, reproducible
runner, sanitized results and an [acquisition review](ACQUISITION-REVIEW.md).

Both fresh matrices pass **14 new scenarios and six strict negative controls
per plain, ASan/UBSan and TSan mode**. Four production files compile as separate
translation units with warnings treated as errors. Full result JSON is identical
between the independent runs; the retained-file audit binds the sources,
generated methods, variants and all logs. [Host evidence](HOST-TESTS.md) records
the scope and the corrected audit-only failure.

The chain uses one modeled duplicate, two adapter-mediated STATE calls during
attachment/broker creation, and a third for producer adoption. A FRAME then
reaches one actual extracted ioctl handler without an implicit STATE, retry or
close. Complete raw bytes, including CRC/tail bytes, survive into the kernel
model and the independently checked canonical response.

An actual modeled commit followed by a synthetic EIO remains UNKNOWN all the
way back through the dispatcher. Lost response retrieval remains historical.
A same-capability alias can still change the underlying state independently;
the cached receipt does not establish current state or exclusion.

This is a serial host fixture. It substitutes fd-table, syscall, usercopy, RNG
and lock behavior; it does not execute native Linux libc/VFS, a Rust host or a
network/pipe transport. TSan is not concurrency proof. The actual Rust/pipe
evidence remains separately frozen in [broker-wire](../emulator-broker-wire/).
No emulator or physical endpoint, installation, service or package changed.

The next owned acquisition step needs a retained original descriptor, an exact
one-shot CLAIM record and checked poststate. Current failed adapter attachment
can lose usable custody of an uncertain duplicate; current broker destruction
does not report successful disposal. A wrapper alone cannot close those gaps.
Authoritative baseline/stop, peer binding, exclusion, native consumption,
deadlines, durable custody and recovery remain open. All runtime, stems, USB
and release gates remain **CLOSED**.

To reproduce only these new scenarios with a legitimate local emulator source
checkout:

    python3 experiments/emulator-wire-native64/run-tests.py --emu ../xdj-az-emu

The runner exports pinned Git blobs and applies the frozen patches only inside a
fresh ignored directory. Raw source exports, logs, objects and executables stay
private. Parent suites are not rerun.

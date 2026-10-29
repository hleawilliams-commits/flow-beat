# Focused broker RESTORE host evidence

The first full run of the frozen successor passes **207 assertions per mode**
in plain, ASan/UBSan and TSan. Each of **16 strict compiling negative controls**
produces exactly its one designated failed assertion per mode. Compilation
must emit no diagnostics; unexpected failures, exits, assertion counts,
sanitizer reports and hangs are rejected. Standalone broker translation-unit
compilation also passes strict warnings in each mode, without fixture unused
symbol suppressions.

The [primary result](test-result.json) has SHA-256
`8afc621df2f38ccd74e0d0ee4c7a5f542fe30c01015a42174e6903ff5fc4c217`.
A single fresh independent run matches the entire result byte for byte, with
no excluded fields. [Review](REVIEW.md), [comparison](comparison.json) and the
[independent result](test-result-independent.json) retain that evidence.
ARM64 object checking is separate.

| Focused group | Assertions per mode |
| --- | ---: |
| Four original/target mode pairs, equal bytes with distinct mode, idle-to-ROTATE-to-FRAME | 28 |
| Full source/snapshot equality, cache conflicts and independent ID history | 44 |
| Historical cache after explicit adoption and later conflict | 3 |
| Lost ACK versus retained not-yet-delivered command, cross-operation blocking and full positive reconciliation | 87 |
| Actual kernel idle-target rejection and malformed callback outcomes | 17 |
| ID/epoch/sequence exhaustion and final-capacity success | 5 |
| Held callback with duplicate/conflicting contenders and caller-copy barrier | 7 |
| Independently selectable control oracles | 16 |
| Total | 207 |

The [manual inventory](test-inventory.json) is enforced by the runner. Fixture
setup uses fatal `assert`, and pthread create/join errors terminate; these are
not counted or accepted as expected negative-control failures. Boundary seeds
write fixture-only broker/kernel fields with no live worker. No such setters
exist in the public API. The concurrency cases launch contenders while the first
callback holds the broker mutex, join every worker before final oracles, and
mutate the caller request only after the callback barrier establishes that the
initial private copy has finished. They test selected schedules, not every
possible interleaving or external lifetime guarantee.

The [controls](controls.json) independently remove or alter: mode prediction,
retained cache ordering, mode/source/target equality, exact positive reconciliation,
idle adoption rejection, restore-ID preservation, UNKNOWN blocking, fresh snapshot,
bound source, admission closure, grant retirement, input/output alias copying,
and invalid-mode/invalid-source conflict ordering. The final two reproduce an
issue found during source review before the first fixture run: malformed values
were originally rejected before examining an existing same-ID receipt. The
implementation now handles the retained conflict first, so malformed conflicts
close admission consistently. Assertions were not relaxed. There was no failed
functional or sanitizer run of this frozen successor before the passing result.

## Source and execution boundaries

`parent-inputs.json` pins both old broker files and the frozen RESTORE preparation,
extraction and shim sources. Each run captures all test inputs first, invokes the
captured preparation helper, exports the pinned emulator Git blobs into a fresh
private tree and verifies each of three applied-patch inventories. It checks
that the final patch and manifests are the captured bytes. Only definitions are
loaded from the frozen runner; no frozen passing suite is executed.

The fixture executes these seven exact kernel-source methods:
`mixer_instance_init`, `xput_bytes`, `xget_bytes`, `ctrl_release`,
`mixer_owned_next`, `ctrl_ioctl`, and `ctrl_write`. Kernel declarations, complete
extraction, individual methods, shim prefix and UAPI shim hashes are retained.
The frame/header source is the exact frozen candidate. The test-only adapter
switches FRAME/ROTATE/RESTORE explicitly, maps the 208-byte RESTORE input and
validates native STATE version/reserved before semantic conversion. An unknown
operation never falls through to ROTATE.

This is ordinary host execution with owned-memory usercopy, pthread locking,
deterministic RNG and kernel-service shims. It does not execute the Linux kernel,
VFS, actual user-memory faults or an emulator/native application. PENDING stores a
copied command in the fixture and later explicitly delivers it; it is not a
running native ioctl or proof of cancellation. LOST_ACK really executes the
extracted mutation, then suppresses its reply. No daemon, real transport,
controller fd, wire/FFI integration, process-death journal, native-stop proof,
original baseline authority, host restoration, audio or physical timing was
tested. All runtime gates remain CLOSED. Historical stems failures and their
closed gate are independent and unchanged.

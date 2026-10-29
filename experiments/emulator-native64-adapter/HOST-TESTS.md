# Focused native64 adapter host evidence

The first complete frozen matrix passes **137 checks per mode** in plain,
ASan/UBSan and TSan. Each of **13 strict compiling negative controls** fails
exactly its one designated assertion in each mode. Standalone adapter
translation-unit compilation also passes strict warnings in each mode without
fixture unused-symbol suppressions. Unexpected exits, counts, diagnostics,
sanitizer reports and hangs are rejected.

The [primary result](test-result.json) has SHA-256
`9b7000366cd33b664948d7a75cd102379ce78e03ca43acda0383c8319b6ccf32`.
One fresh independent run matches all bytes with no exclusions; see
[review](REVIEW.md), [comparison](comparison.json), and the
[independent result](test-result-independent.json). ARM64 compilation and
linking are separate evidence. There was no failed functional or sanitizer
run of this frozen adapter before its passing result.

| Focused group | Checks per mode |
| --- | ---: |
| Attach, full expected STATE, duplicate outcomes and failure cleanup | 26 |
| STATE shape, exact flags, binding, syscall replies and output clearing | 21 |
| Typed wire commands, full raw payload and local/driver rejection | 24 |
| Known precommit errors and uncertain results before/after actual commit | 27 |
| Original-fd reuse, explicit disposal and uncertain close accounting | 12 |
| Actual frozen broker retention, cached results and reconciliation | 14 |
| Independently selectable control oracles | 13 |
| Total | 137 |

The [manual inventory](test-inventory.json) is enforced, not learned from
passing output. Setup assertions abort; they are not accepted control
failures. The source review added three malformed STATE flag cases before
the first run: an unknown bit, missing caller-owner, and missing enabled.
No production behavior or assertion was weakened.

The [controls](controls.json) alter explicit idle mode, the final raw payload
byte, stale errno handling on success, EINTR classification, positive-return
classification, precommit rejection, failed STATE output clearing, reserved
STATE validation, run binding, complete attach equality, private-fd use,
consume-before-close order and local zero-sequence validation. Each is a unique
source edit exercised by a single functional oracle with valid fixture cleanup.

## What executes

The actual adapter and frozen broker execute with seven exact extracted kernel
methods: `mixer_instance_init`, `xput_bytes`, `xget_bytes`, `ctrl_release`,
`mixer_owned_next`, `ctrl_ioctl`, and `ctrl_write`. Parent hashes, complete
patch inventories, method/declaration/extraction hashes, the owned-memory
kernel shim, Linux UAPI shims and every captured input are retained. Each run
uses fresh private captures, checks inputs again after execution and runs no
old passing suite. The production adapter has 22 native64 layout/encoding
assertions; host tests provide explicit Linux type/ioctl shims, while the
separate Linux build uses real Linux headers.

The fake fd table models references to the same file object. Reuse of the
original number targets a different object while the adapter's duplicate
continues to refer to the original owner. Only the final modeled close calls
actual `ctrl_release`. This establishes behavior within the model, not actual
VFS retention or exclusivity. Cleanup is deliberately fixture-owned and
quiescent, including after tests of UNKNOWN; it grants no real-session teardown
permission. Disposal can leave the selected overlay and permanent fence intact.

Normal ioctls execute the exact kernel methods. Synthetic wrapper results can
occur before or after that actual extracted mutation. The broker integration
faults only the mutator, preserving its real pre-submit STATE. A lost response
after commit reconciles only from the full exact positive result; a lower
state after a precommit unknown remains UNKNOWN and cannot authorize replay.
Counters check single calls, no automatic queries/retries/close, and cached
broker duplicates. Synthetic errno cases check classification; they do not
prove that a live kernel produces those outcomes. The source-reviewed error
classification applies only to direct libc calls and the pinned handler.

Partial STATE usercopy uses the inherited owned-memory `fail_to` shim and an
actual EFAULT return from the extracted handler. No field is published on
failure. A lying success with an incomplete copy cannot be detected generally;
success completeness depends on the exact kernel's copy/return contract.
The input-copy failure similarly uses `fail_from` and actual precommit EFAULT.

`native.c` is captured and hashed but not executed by these host tests. Its
three libc wrappers are separately compiled and linked for ARM64 Linux; each
calls libc once and captures errno immediately only on `-1`. There is no
real device, process lifecycle, native cancellation, deadline, concurrency,
two-file settings transaction, durable recovery, installation or physical
timing proof. All runtime and staging gates stay CLOSED.

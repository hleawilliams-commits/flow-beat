# Four-layer host evidence

The [primary result](host-result.json) and
[independent result](host-result-independent.json) both have SHA256
2eca57b73d56236f0f53562cf67c82f3d4d9a314c9366cfdc7c248b73f072e70.
Each matrix passed on its first invocation. No failed build, functional run or
sanitizer diagnostic occurred in either matrix. The
[comparison](comparison.json) records exact whole-result byte equality, with no
field exclusions.

Compiler: Apple clang 16.0.0 (clang-1600.0.26.6),
arm64-apple-darwin25.2.0. These are macOS host checks, not a Linux guest build or
execution claim. Production native.c is pinned for boundary review and is not
compiled or executed by this fixture. Its earlier separate ARM64 evidence is
unchanged.

## Exact composition and limits

The new harness links four unchanged production translation units:

- broker-wire wire.c and guest_dispatch.c;
- broker-restore broker.c;
- native64-adapter adapter.c.

No MBW_TEST seam or substitute broker/adapter runs here. The three mba_sys
functions use a serial synthetic descriptor table. The fixture uses the exact extracted
mixer_instance_init, xput_bytes, xget_bytes, ctrl_release, mixer_owned_next,
ctrl_ioctl and ctrl_write methods from the three-patch driver. The frozen
usercopy, RNG and pthread shims supply owned host memory and locks, not Linux
fault handling, VFS or application input.

The injected postcommit EIO calls the actual extracted mutation first, then
substitutes its syscall-boundary return. It demonstrates conservative outcome
propagation; it is not evidence that the running kernel emits EIO after commit.
Permission denial and partial-input EFAULT come from the actual extracted
handler under explicit capability/usercopy shims.

The response oracle assembles all 668 bytes independently with explicit endian
writes. It is not a codec encode/decode roundtrip. The native request check also
compares the complete 200-byte FRAME ABI. Direct fixture observations and alias
operations bypass the adapter counters; counts named native_calls mean
adapter-mediated calls only.

All cases are serial. Cleanup copies observations, destroys the fixture-owned
objects and closes its modeled references after synchronous calls finish.
That is test memory cleanup, not a safe-close, cancellation or recovery protocol
for a real unresolved session.

## New scenarios

Each selector has one final compound functional assertion after cleanup.
Setup/cleanup assertions are never accepted as negative-control success.

| Selector | Cross-layer fact checked |
|---|---|
| constructor | Attachment and broker creation make exactly two STATE calls and one duplicate; dispatcher construction does no native I/O or adoption. |
| idle | Adapter and broker accept owned idle flags 23, but wire admission refuses it without creating input. |
| constructor-drift | A changed final seed byte is refused against the retained broker state. |
| adopt | A third STATE through the retained duplicate establishes the complete expected snapshot for this local adoption. |
| frame | One FRAME transfers all 152 raw bytes into the model and all 668 canonical reply bytes; no implicit STATE or close. |
| fd-reuse | Reusing the original descriptor number cannot redirect adoption or FRAME away from the retained duplicate. |
| adopt-divergence | An actual same-capability alias write after construction causes fresh adoption to refuse the changed state. |
| permission | Actual modeled EPERM becomes exact definite rejection, with no mutation or retry. |
| copy-fault | Actual modeled partial-input EFAULT becomes exact definite rejection, with no mutation or retry. |
| postcommit-unknown | Actual modeled commit plus synthetic EIO retains UNKNOWN, old local expected state and closed admission; no replay or disposal. |
| lost-response | Discarding a positive reply and retrieving it again produces provenance 1 without another native call. |
| conflict | Changing the final byte of a duplicate closes dispatch and preserves the prior commit evidence. |
| alias-cache | Historical cache remains historical when another same-capability reference changes the current native model. |
| alias-next | The next stale FRAME is rejected by the actual handler; no retagging or renumbered retry. |

The alias scenarios deliberately preserve an accepted limitation. The library
does not exclude other references to the same open description. A cached ACK
cannot establish the current overlay. This C fixture also does not re-prove the
Rust host's refusal of cached provenance; that evidence remains separate.

## Strict controls and retained audit

Six private single-source mutations each compile and link, then produce exactly
their designated final FAIL label and exit 1 in every mode:

1. Truncate the adapter's raw FRAME by one byte.
2. Keep the caller's original descriptor number instead of the duplicate.
3. Classify EIO as a definite rejection.
4. Skip the broker's fresh adoption snapshot.
5. Mark a cached dispatcher response as fresh.
6. Omit the final seed byte from dispatcher correspondence.

The candidate must produce exactly one passing check per selector. A compile
failure, setup assertion, timeout, extra output or sanitizer diagnostic cannot
satisfy a control. Expected-failure labels are pinned in [controls.json](controls.json).

Each run captures five new inputs and 18 frozen inputs, derives 15 source files,
and records 33 compiles, 21 links and 60 executions. Production files retain strict
-Wall/-Wextra/-Werror independently; unused suppressions apply only to the fixture
translation unit containing kernel shims. The runner strips sanitizer/preload
environment overrides before helpers and all compile/link/test children.
It retains partial output on timeout and refuses overwriting an existing
requested result.

Independent review requested stronger retained-file verification before
publication. The audit now checks distinct captures, exact input sets, all nine
frozen C/header copies, all seven independently extracted methods, declaration
assembly, usercopy/UAPI shims, prepared manifests/patches, every private control,
and exact compile/link/run log contents and result inventories.

The first audit invocation failed because its independent declaration parser
kept a trailing comment after testmode_held's semicolon, while the frozen runner
extracts only through the semicolon. All seven method hashes had already matched.
The failed audit script and diagnosis remain private. Correcting only that
declaration reconstruction made the exact retained-file comparison pass.
No test source, captured input or result changed, and no tests were rerun.
The comparison explicitly records this one prior audit failure.

No runtime gate follows from these checks. Original baseline/stop authority,
actual acquisition and endpoint ownership, external exclusion, native
consumption/dwell, failed attachment/teardown custody, process death, recovery,
audio quality and physical timing remain unresolved. The historical stems
staging failure stays separate and CLOSED.

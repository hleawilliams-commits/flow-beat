# Focused guest stream evidence

Two fresh matrices passed on their first invocations. Each ran 14 new selectors
in plain, ASan/UBSan and TSan modes, with one final compound functional check per
selector. Twelve strict source controls per mode failed exactly their selected
oracle. The retained-file audit also passed on its first invocation. No unchanged
parent test suite was repeated.

[Primary](host-result.json) and [independent](host-result-independent.json) results
are byte-identical, with SHA256:
3c1e91ed681a42f5a1b01053b384b9683ad08682ec7ac80f6032f6d7b88aad7f.
The [comparison](comparison.json) records two matrix attempts, two successes and
zero failed attempts, plus the separate retained-file audit.

Each invocation records 61 clean compiles, 39 clean links and 78 executions using
Apple clang 16.0.0 (clang-1600.0.26.6), targeting arm64-apple-darwin25.2.0.
Production translation units use -Wall -Wextra -Werror without suppression.
Only the inherited fixture permits unused parameter/function/variable warnings.
One of the 61 compiles builds the actual POSIX stream wrapper separately; that
object is never linked into the scripted fixture or executed.

## New cases

| Selector | Focused observation |
|---|---|
| retain | The owner retains a fresh Ready graph and stream fd before consuming caller slots. fd0 transfers; negative fds, numeric collisions with graph fds, reused graphs and premature graphs refuse without transfer or I/O. |
| fragments | Fragmented ADOPT and FRAME consume exactly their records without read-ahead. Each step performs at most one operation; dispatch happens once, response bytes and progress match, and completed evidence moves only on explicit next. |
| header-refusal | Noncanonical or wrong-kind headers stop after the 48-byte header, before body reads or dispatch. |
| read-eof | EOF before a record or within header/body preserves credited progress and resources. EOF after explicit next preserves the previous completed record. |
| read-errors | Scripted EINTR, EAGAIN and EIO retain returned error and buffer effects without crediting failed progress, retrying or dispatching. |
| read-abnormal | Impossible negative or oversized counts terminate while retaining raw returns and buffer effects. |
| write-errors | A response write can fail after native FRAME commit. Credited and uncertain output remain distinguishable, with no replay or redispatch. |
| write-abnormal | Zero, impossible negative and oversized write counts terminate without false completion. |
| dispatch-refusal | The actual graph rejects a mismatched ADOPT payload; no response write occurs. |
| unknown-response | Actual extracted FRAME commit followed by a scripted EIO produces an Unknown broker receipt. Completing local response writes preserves Unknown. |
| reentry | Same-thread inspection sees armed operations and retained resources. State-changing reentry refuses while the operation is active; close inspection sees the stream slot already consumed. |
| dispose-order | Mutex or attribute destruction refusal stops before transport close. A separately justified later call retires remaining graph resources before the stream, preserving the first report. |
| close-uncertain | Modeled duplicate and stream close uncertainty retains evidence. Explicit graph finalization and later stream disposal never retry consumed fd numbers; the outer shell needs separate finalization. |
| partial-dispose | Allocated, taken and EOF-stopped owners release only on explicit disposal; known success consumes the owner slot. |

The fragment case reads 504 request bytes and writes 964 response bytes across
two records. Its expected FRAME response is constructed independently, including
the full echoed request, command and state. The harness exercises actual frozen
graph, dispatcher, broker and adapter code over seven exact extracted driver
methods. It reuses only the pinned graph fixture support prefix; parent selectors
and their main function are excluded.

Stream read/write/close, controller descriptors/syscalls, usercopy, RNG and kernel
locking are modeled. Successful synchronization uses real pthread calls;
scripted synchronization refusals precede those calls. This is a serial model.
TSan execution does not establish concurrent custody, cross-process ordering,
live VFS behavior or quiescence.

Inspection during a modeled operation is same-thread evidence. Child diagnostics
may be newer than the last outer publication. Archived child evidence is historical;
presence fields describe the current retained slots. Cleanup first copies the
observations, accounts for known modeled effects, and explicitly disposes remaining
resources. That fixture accounting supplies no runtime recovery authority.

## Strict controls

Every control changes only new stream.c in its private variant.

| Control | Oracle selector |
|---|---|
| read-arm-before-call | fragments |
| write-arm-before-call | fragments |
| dispatch-arm-before-call | fragments |
| retain-raw-read-return | read-errors |
| retain-raw-write-return | write-errors |
| retain-completed-record | fragments |
| require-request-header | header-refusal |
| retain-raw-dispatch-size | fragments |
| preserve-first-disposal | dispose-order |
| consume-successful-owner-slot | partial-dispose |
| preserve-graph-disposal-error | dispose-order |
| arm-stream-close-before-call | reentry |

[Controls](controls.json) bind exact mutations and failure labels;
[inventory](test-inventory.json) binds the 14 selectors. Each control must compile
and link cleanly, then return exit 1 with precisely its named failure and check
summary. Setup failures, sanitizer reports, timeouts, extra output and build
failures cannot satisfy it.

The graph-error control changes the reported classification while preserving the
existing early return and resource custody. The slot-consumption control frees
normally; its selector records the actual missing slot consumption before using
the release report to clear its local dangling pointer for safe cleanup. Other
controls preserve operational resource flow and reach their final functional
oracle.

## Retained provenance and limits

Each run captures nine new inputs, pins 27 frozen inputs and generates 28 source
or header files. The actual graph/child/wire files remain exact copies. The adapter
prefix, broker prefix/suffix and graph fixture prefix are cut at pinned boundaries.
A fresh export of pinned emulator source receives the three existing source
patches solely to extract the seven driver methods. No emulator module is built
or installed. No allocator substitution or hidden owner-field mutation is used.

The retained-file audit independently reconstructs those cuts and methods,
verifies all captured/frozen/generated bytes, every variant and compile/link/run
log, the wrapper compile-only record and both public result copies. It reruns no
builds or tests. Its script SHA256 is
915598b1c98393438febf64c2d8f817a7057f895d831fdca1b673759116e9ac8.
The comparison records all matrix attempts and the first audit attempt. Raw
exports, binaries, logs and audit scripts remain private.

The actual POSIX wrapper has host compile evidence only. No socket, pipe, endpoint
or emulator was contacted, and this checkpoint has no Linux ARM64 stream build.
The earlier graph ARM64 result does not extend to this new component. Scripted
EPIPE tests prove returned-error handling only: callers must independently arrange
SIGPIPE handling. A step bounds call count, not latency; blocking I/O or graph
operations may never return. Local write completion is not peer receipt, native
success or authentication.

External serialization, alias exclusion, quiescence and uncertainty accounting
remain requirements, not properties established here. Allocation failure and
counter exhaustion are source-reviewed only. Process death, nonlocal exits,
durable custody, transport/supervisor binding, native baseline/stop authority and
live deployment remain open. Historical parent failures and all runtime gates
are unchanged. No installation, core, package, catalog or release changed.

# Guest stream source contract

This component composes the frozen owned graph with an owned, supplied POSIX
byte-stream descriptor. The graph and stream are local resources; correlation
fields, matching bytes and constructor readiness do not authenticate a peer or
establish native ownership. All parent operation bodies remain separate and
unchanged. This document describes source behavior; focused validation and its
limits are recorded separately in [HOST-TESTS.md](HOST-TESTS.md).

## Resource transfer

`mbgs_create` allocates an owner without I/O. The caller retains that owner
before calling `mbgs_take`. Transfer validates both resources without changing
either caller slot, then stores both before consuming the slots to -1 and NULL.
The descriptor may be zero.

The supplied graph must be construction-Ready with a present dispatcher in
its initial Retained phase, zero last exchange, no attempted graph exchange
and no disposal. Its current original and duplicate descriptors must be held.
A stream fd numerically equal to either is refused. Other descriptors may
still alias the same open file description; excluding those aliases and all
other operational references is an external obligation. Refused transfer leaves
both resources with the caller.

No raw graph or child getter exists. Diagnostic descriptor numbers remain
usable in-process and must not be used to mutate, duplicate or close resources.
Graph diagnostic callback pointers remain scrubbed by the frozen graph.

## Framing and progress

The normal phases are Header, Body, Dispatch, Write and Written. A successful
transfer starts record1 in Header. Terminal and Disposing refuse further
steps. `mbgs_step` invokes at most one read, write or `mbog_exchange`; the
graph exchange can itself perform its frozen broker/native operations.

A read requests only the unfilled part of the 48-byte header. Header decoding
must identify ADOPT request length256 or FRAME request length248 before a body
read is allowed. Response kinds, malformed headers and unsupported lengths
terminate without body consumption or graph exchange. Body reads request only
the exact remaining record bytes, so a later record is not read ahead.

A complete request is armed before exactly one actual graph exchange. Its raw
return code, returned byte count and response storage are retained before
interpretation. Only MBWD_OK with the expected response length and matching
header kind, session and exchange enters Write. The graph remains responsible
for complete request/receipt semantics. An embedded broker error or historical
cached response is forwarded as produced; neither MBWD_OK nor transport
progress changes its meaning.

ADOPT responses contain296 bytes; FRAME responses contain668 bytes. Writes
request only the uncredited suffix. A valid positive returned count advances
progress; stale errno on that count is ignored. The owner retains each latest
read/write call's requested offset/count, armed/returned flags and exact raw
count/error. Read-side effects in the fixed buffer survive failed calls even
when no bytes are credited.

Every returned read/write error, including EINTR and EAGAIN, is terminal.
Read0 records EOF; write0 records no progress. Other negative counts and
overcounts are terminal contract failures. Buffers, credited offsets, resource
custody and the first terminal reason survive later refusals. No failed call
is retried and a partial response is never redispatched.

Written means only that returned local write counts cover the response. It is
not a peer acknowledgement or proof of broker/native success. Repeated steps
in Written perform no I/O. Explicit `mbgs_next` copies the current record to
one fixed `last_completed` slot, clears current storage and advances a checked
record number. It performs no I/O. This replaces any older completed record;
it is not a complete history. Counter exhaustion refuses before another call
or record replacement. An explicitly received duplicate request still follows
the frozen dispatcher's historical-cache policy; the stream owner never
synthesizes one.

## Inspection and serialization

`mbgs_inspect` copies the current journal and uses only the graph's pure
inspector. It performs no I/O, locking or backend callback. Graph presence is
reported separately from archived graph evidence, so a snapshot of a consumed
child cannot imply current custody.

All APIs require external serialization. The active flag rejects state-changing
same-thread reentry during transport or graph calls while permitting diagnostic
inspection. It is not an atomic lock or concurrent-access guarantee. Current
and last-completed records, first terminal cause, first/latest disposal reports
and once-only stream-close evidence are bounded volatile diagnostics.

## Explicit disposal

EOF or an error grants no cleanup permission. Disposal requires independent
quiescence and resolution/accounting for transport uncertainty, native ownership,
all aliases and pending callbacks.

`mbgs_dispose` permanently closes step/next, archives graph evidence and calls
the actual graph disposer first. A graph error or retained graph stops before
stream close, including when only the graph's uncertain empty shell remains.
Explicit `mbgs_finalize_graph` delegates to the graph's memory-only finalizer,
which refuses while any child remains. It archives needed graph evidence and
preserves outer uncertainty; it does not close the stream.

After the graph is absent, a separately justified disposal may consume the
stream slot before exactly one close. The descriptor number, armed/returned
state and raw close result remain recorded. Consumed descriptors are never
retried. Any graph or stream close uncertainty latches outer-shell retention,
even after later known cleanup. An already empty uncertain shell refuses
another disposal without replacing its first/latest reports.

Fully known-success disposal consumes the owner and returns its final report;
capture other needed diagnostics first. Otherwise `mbgs_finalize` is a
separate memory-only discard requiring disposal-started, no graph, no descriptor
and no active call. Finalization is evidence disposal, not recovery proof.

## Native boundary and remaining work

The three thin POSIX wrappers call read, write or close once and save errno only
for a -1 return. They add no endpoint opening, flushing, retry or signal policy.
Blocking calls and graph locks can wait indefinitely: the step limit bounds
call count, not latency. A write may raise SIGPIPE before returning; the caller
must independently arrange suitable handling if expecting retained EPIPE
evidence. Scripted error returns cannot prove signal survival. Nonlocal exits
may leave an armed operation without return evidence; process death may destroy
all custody.

Host/guest transport binding, supervisor retention, authentication, native-stop
authority, writer exclusion, deadlines, both-file recovery and durable journals
remain separate. This source adds no endpoint, main wiring, emulator/device
operation, installation, package, catalog update or runtime-gate change.

# Retained guest stream owner

This inactive successor retains an already-open byte-stream descriptor and a
fresh [owned graph](../emulator-owned-graph/) before any transport I/O. It adds
bounded framing and partial-I/O evidence around the actual graph dispatcher.
It opens no endpoint and adds no host, guest startup or supervisor wiring.

The caller first retains an allocated owner, then transfers both resources.
Transfer accepts fd0, requires a Ready graph whose dispatcher has never
exchanged, and rejects the graph's current original or duplicate fd number.
Numeric distinction does not exclude aliases or authenticate the stream.

Each step makes at most one read, write or graph-exchange call. Reads fill the
48-byte header and then exactly the known request remainder. A full request is
dispatched once; writes advance through the retained response. Explicit
`next` is required after a fully written response and archives the completed
record before starting another. Local write completion is not peer receipt or
native success.

EOF, errors, zero writes and impossible counts stop further steps while keeping
the descriptor, graph, request/response buffers and progress. There is no
automatic retry, replay, reopening, flush or disposal. The journal contains the
current record and one completed record, not a durable or unbounded history.

Explicit disposal requires independent quiescence and uncertainty accounting.
It tries graph disposal first and stops while the graph remains. An uncertain
empty graph shell needs explicit graph finalization. Only then may a later
disposal consume the stream fd before one close. Any close uncertainty retains
the outer shell and evidence until separate finalization.

The [source contract](SPEC.md) defines the phases, returned evidence and disposal
limits. Two fresh [host matrices](HOST-TESTS.md) passed 14 new selectors and 12
strict controls per plain, ASan/UBSan and TSan mode. Their complete result files
are byte-identical, and the retained-file audit passed. Actual POSIX wrappers
were host-compiled separately only; no Linux ARM64 stream build or stream runtime
was exercised. Prior suites were not replayed and existing gates remain unchanged.

All calls require external serialization and alias exclusion. A step bounds
call count, not time: I/O or graph operations may block indefinitely. The caller
must arrange SIGPIPE handling if expecting an EPIPE return; these wrappers do
not change signal policy. Process death and nonlocal exits can prevent return
evidence or destroy custody. No native authority, recovery, runtime activation,
installation or release permission follows.

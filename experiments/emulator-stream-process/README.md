# Retained stream process fixture

This inactive experiment connects the frozen Rust broker backend to the frozen
C guest-stream owner and owned graph in a separate local fixture process.
The C side uses the actual POSIX stream read/write/close wrappers. Controller
descriptors and driver surroundings remain modeled; this is not a guest launcher
or a native acquisition route.

Two unnamed socketpairs separate broker bytes from fixture control and evidence.
The child receives duplex data on fd0 and duplex control on fd1; stderr is
diagnostics only. The parent retains the actual child before sending GO. A
READY report means fixture construction completed, not native authority or
authenticated peer identity.

Four focused cases cover two successive FRAMEs, a modeled native commit followed
by EIO, a lost response after real stream write shutdown, and a partial request
followed by EOF. These compose existing implementations across a real local
process boundary. They do not repeat the parent suites.

Data EOF or failure leaves the C owner retained for an independent report.
Only an explicit fixture disposal command permits its known modeled cleanup.
The cleanup report distinguishes archived pre-disposal evidence from actual
postcall owner presence and raw disposal results. The parent must collect those
reports and wait for the actual process before its final functional checks.
Unexpected exit, control failure or forced abort is a failure, not a disposal
receipt.

The [fixture protocol](SPEC.md) defines fixed report bytes and lifetime rules.
The two focused host matrices passed; [HOST-TESTS.md](HOST-TESTS.md) records
their scope and results. The first [retained-file audit](comparison.json) also
passes; fragmentation-dependent raw reports are checked separately.

The selected fixture may ignore SIGPIPE to observe EPIPE from the unchanged
POSIX wrapper. That process-local choice is not production signal policy.
Blocking I/O has no latency guarantee. Local write completion, a child exit code,
matching correlation fields and copied diagnostics do not establish peer receipt,
native ownership, quiescence or recovery. Main, endpoint acquisition, production
supervision, Linux ARM64 integration and every existing runtime/release gate
remain unchanged.

# Retained stream process evidence

Two fresh matrices passed on their first invocations. Each ran four new process
selectors and three strict controls in plain, ASan/UBSan and TSan C modes.
Each selector has one final compound check, emitted only after explicit fixture
disposal and an actual child wait. No unchanged parent test suite was repeated.

[Primary](host-result.json) and [independent](host-result-independent.json)
sanitized results are byte-identical, with SHA256:
204da7920697f64cd58f29c6ac81bbc2abb8935d3ec78efe550438eb23304685.
This equality covers source/preparation, builds and functional outcomes. Raw
process evidence remains private and is checked separately; real OS fragmentation
changes some counts and report bytes.

Each matrix records 37 clean compile invocations, 12 clean C links and 21
executions. The standalone Rust compile also links the host checker. Eight
production C translation units remain frozen; the peer links and executes the
unchanged POSIX stream wrappers. Apple clang 16.0.0 targets
arm64-apple-darwin25.2.0; the host uses rustc 1.98.1. C production units use
-Wall -Wextra -Werror without suppression. Only the inherited fixture support
allows unused parameter/function/variable warnings. The Rust checker uses
-Dwarnings with dead-code allowance scoped to its three frozen modules.

## New process boundary

The host owns the actual Child and a separate control socket before GO. Only
the data socket enters the unchanged Rust WireBackend. The C peer already owns
fd0 and a fresh actual graph before READY. Each record uses the real POSIX
read/write wrappers; positive host call sizes are capped at 17 written and
19 read bytes. This bounds request sizes, not guest fragmentation or latency.

The peer keeps terminal transport state, the graph and full bounded request/
response evidence until explicit D over the control channel. Both live-child
observations, a CLEANUP report and actual wait are required. Post-disposal
reports contain archived pre-disposal snapshots alongside current owner/model
presence and raw returned disposal results; no freed owner is inspected.

| Selector | Observed composition |
|---|---|
| normal | One ADOPT and two successive FRAMEs return exact fresh receipts. All 752 request and 1,632 response bytes match independently constructed canonical values. The host advances through sequence2; explicit next and EOF preserve the final completed frame without replay. |
| unknown | The extracted modeled FRAME commits, then a fixture wrapper substitutes EIO for its successful return. Rust keeps Unknown and its acknowledged expected state stays at the seed; a repeated submit and baseline attempt send no more bytes. The full Unknown receipt and actual modeled sequence1/frame remain retained. |
| lost-reply | After modeled commit and positive dispatch bytes, real shutdown(SHUT_WR) makes the unchanged response write return -1/EPIPE. Fixture-only SIGPIPE ignore permits the return. Rust sees EOF, preserves Unknown and sends nothing on repeat; the C owner retains the complete positive 668-byte response with zero credited output. |
| partial-request | A Rust Write adapter actually sends 61 ADOPT bytes, then scripts a later error. Data closure produces actual C EOF after the header48 and body13; no dispatch or ADOPT follows. The full host intent, credited61 bytes and guest partial record survive. |

The Unknown receipt's expected_after is predicted before submission. It can
equal the committed model while acknowledged broker/host state remains older.
That match is not a fresh ACK or uncertainty resolution.

Model counters cover wrapped controller calls: normal ends at eight wrapped
ioctls (five STATE, one CLAIM, two FRAME), unknown/lost at seven and partial at
five. The inherited fixture's initial plan reads and legacy setup write bypass
those counters. Controller descriptor identity, ioctl surroundings, usercopy,
RNG and kernel locks remain modeled. Successful pthread calls are real.

## Strict controls and failure handling

Three controls alter only retained diagnostic publication in a private stream.c
copy. Each compiles/links cleanly and reaches exactly one named final failure
after the same explicit disposal and actual wait:

- dispatch-written-evidence: normal rejects a zero dispatch byte count.
- held-graph-evidence: normal rejects a false graph-presence report, including
  READY; a valid-but-wrong boolean never substitutes an early fixture abort.
- native-write-error-evidence: lost-reply rejects erased EPIPE evidence.

Build failures, sanitizer reports, socket timeouts/errors, malformed control
records, extra output and emergency termination cannot satisfy a negative
control. Unexpected control loss or partial reports retain available diagnostics
and fail. Host socket timeouts, child exit deadlines and the outer process-group
abort are fixture limits only. The abort path was source-reviewed before testing;
it is not an exercised production teardown or recovery mechanism.

## Provenance and limits

Each matrix captures seven new inputs, verifies 76 frozen inputs and generates
29 C source/include files plus three exact Rust modules. Pinned emulator Git
blobs receive existing source patches in fresh private exports only. Adapter/
broker/core and graph support cuts exclude legacy lifecycle and parent selectors.
The standalone Rust modules are reconstructed through the existing pinned
candidate chain; no full Cargo or preview build is claimed.

Private evidence includes all three fixed3,072-byte reports per execution,
canonical host transcripts, bounded pending/response records, diagnostic
snapshots and explicit cleanup/wait summaries. These files retain the actual
fragmentation rather than normalizing it. The [retained-file audit](comparison.json) passed on its first invocation.
It reconstructs exact C cuts and Rust patch outputs without executing helpers,
checks all capture/frozen/generated inventories and logs, and independently
decodes full canonical report/transcript/model bytes and host diagnostic values.
Both explicit disposal and actual-wait records are checked for all 21 executions
per matrix, including controls. There were no failed matrix or audit attempts.

Each matrix retains 63 raw reports; 36 differ between the matrices. Their
fragmentation-dependent values are verified individually, never silently removed
from a comparison or claimed identical. The sanitized result contains no raw
report hashes; the private retained-file audit binds them separately.
Audit script SHA256:
2e5408fe92295ca2658c8d6fb445ca91a9faea325ee6da4a9a7abbc1a6478d1c.
Private audit-evidence SHA256:
9372e4daa796f5ce2be7111de43a1f7c012ea00830f12c6f34dd89f469778ab5.

This establishes actual host socketpair/process I/O around modeled controller
effects. C sanitizer modes do not instrument Rust, test concurrent C inspection
or prove cross-process native ownership. The fixture's serial known effects
justify only its own cleanup. Matching state, EOF, child exit, copied reports
and successful local writes establish no native stop, peer authentication,
alias exclusion or recovery permission.

No named endpoint, running emulator, physical deck, native controller acquisition,
Linux ARM64 stream build, installation, settings, core, package or catalog was
changed. Production boot/supervisor binding, final Drop/death custody, authoritative
originals/native stop, deadlines, full overlay/both-file recovery, USB completion/
cancellation, stems activation, audio quality and physical timing remain open.
All existing runtime and community-release gates stay CLOSED.

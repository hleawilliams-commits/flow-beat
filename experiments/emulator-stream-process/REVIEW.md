# Process seam review

This review covers the peer, source preparer, runner and final Rust checker.
The review itself did not execute builds or tests. The two subsequently completed
host matrices are recorded in [HOST-TESTS.md](HOST-TESTS.md); this review does not
assert that the separate retained-file audit has passed.

The selected composition adds a real local process and actual unnamed socketpair
I/O around the frozen Rust wire backend and retained C stream/graph. It does not
add a production supervisor. The existing owned-startup component already retains
a supplied supervisor through worker join; repeating that generic owner would not
test the missing cross-process transport seam.

## Retained resources and reports

The C peer constructs the graph and transfers fd0 into an allocated stream owner
before READY. It performs no broker data I/O until G. Terminal EOF or returned
error leaves the stream owner and graph intact while a report is sent over fd1.
D is a separate fixture cleanup decision based on known serial model effects.
It is not inferred from disconnect, a receipt or the report itself.

CLEANUP uses the copied pre-disposal stream evidence, explicitly marked archived.
Its owner-presence bit comes from the actual postcall pointer; disposal fields
come from the actual return. This preserves exact buffers after successful free
without reading freed storage or claiming that historical held flags describe
current custody. The child rejects incomplete disposal and never substitutes a
raw close, retry or finalization for the returned result.

Model descriptors 3/8 are entries in the existing controller fixture, not operating
system descriptors. Transport fd0 and control fd1 are actual inherited endpoints.
The unknown-mode wrapper delegates to the real extracted model FRAME before
returning synthetic EIO. The lost-reply case calls real shutdown(SHUT_WR), then
lets the unchanged stream wrapper produce its own write return. SIGPIPE ignore
is confined to this fixture process.

The broker's receipt.expected_after is a predicted value filled before submit.
Unknown can therefore retain a predicted poststate matching the committed model
while the broker's acknowledged state and host accounting stay unchanged.
Neither matching bytes nor successful local response writes make Unknown an ACK.

## Source and runner boundary

The preparer uses captured frozen helpers and pinned inputs. It reconstructs the
exact adapter prefix, broker prefix/suffix and graph support prefix, excluding
the parent lifecycle implementations and parent test selectors. The standalone
Rust host compiles the unchanged producer, route and wire modules rather than
running Cargo or the older suites. This checks the linked seam without claiming
a complete preview build.

The peer links the actual POSIX stream wrapper. Controller calls, usercopy, RNG
and kernel locking remain modeled; successful pthread calls are real. Sanitizer
modes instrument the C peer only. The Rust host remains an ordinary unsanitized
build. This is serial functional coverage, not native controller or concurrent
custody validation.

The initial runner used a timeout that killed only the Rust host. Review required
a fresh process group for each invocation so a timeout can abort its private C
descendant too. The runner now retains available timeout output before signaling
that group, then attempts to drain remaining output. A signaling/drain failure
keeps a failed attempt. That emergency path is fixture failure and evidence loss,
never a normal graph-disposal receipt. Normal success must still include the
host's actual wait for its child.

The initial attempt marker is inside failure accounting. Frozen inputs and new
captured inputs are checked before preparation and again after a completed run.
Build diagnostics, malformed outputs, sanitizer reports and timeouts cannot
satisfy negative controls.

Three controls change only retained diagnostic publication in copied
stream.c. They preserve operational resource flow: dispatch-written evidence,
held-graph presence evidence, and the retained native write errno. The final
Rust checker accumulates those valid-but-wrong values into a post-cleanup
functional oracle. Altered READY graph presence continues through G, terminal
inspection, D, cleanup inspection and actual wait before reporting failure.

Actual OS fragmentation may change read/write call counts, step counts and raw
report hashes across runs. A retained-file audit must verify coherent bounded
progress, full semantic records and operation results independently, and describe
any specifically excluded nondeterministic hashes. It must not describe these
raw records as byte-identical merely because functional outcomes match.

## Final checker review

The reviewed checker is the captured source with SHA-256
`a95521e88c19fae451ca21e919eafd053d843cdc8277f2f6368bd5b9be0011d5`.
Its parser requires the exact fixed header, reserved bytes and boolean domains.
Independent append-only encoders compare full canonical request/response bytes,
including all frame bytes. Completed I/O observations require positive counts,
bounded call counts, matching remaining lengths and checked offset advancement;
EOF checks also require untouched write/dispatch fields to remain zero.

The actual child is stored before READY is read. Readiness and held-state child
liveness polls execute even when another semantic check has already failed.
Returned semantic mismatches accumulate rather than causing an early assertion.
The data endpoint is dropped once; the child remains held for its terminal
report and explicit D command. CLEANUP compares both copied record histories
and the model's actual release transition: flags become 25 while instance, run,
epoch, sequence, CRC length and all frame bytes remain unchanged.

The one functional result is emitted only after D, the cleanup report and an
actual successful child wait with empty stderr. Raw control reports, host byte
transcripts and wait/completion evidence are saved privately. Unexpected actual
socket errors, report failures, timeouts and emergency cleanup return fixture
failure rather than satisfying a negative control. The synthetic host write
error after 61 actual bytes is the separately selected partial-request input.

No source-review blocker was found in this selected composition. Controller
counts refer to wrapped model calls, not every direct fixture setup invocation.
Actual host IPC is now exercised by the focused matrices; modeled controller
results do not establish native ownership or production teardown authority.

## Remaining supervision gap

The old boot supervisor treats stdin EOF or read failure as app disappearance
and escalates child termination. Its wait helper also treats a polling error as
completion. Those behaviors do not provide checked native teardown or uncertainty
accounting and are not called by this fixture.

This checkpoint cannot establish authentication, external alias exclusion,
authoritative native baseline/stop, deadlines, SIGPIPE survival under production
policy or durable custody across death. No emulator endpoint, actual controller,
guest main, installation, package or release is exercised. Existing runtime and
recovery gates remain unchanged.

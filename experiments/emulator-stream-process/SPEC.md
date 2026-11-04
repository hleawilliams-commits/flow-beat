# Stream process fixture contract

This document specifies a separate offline fixture around unchanged production
components. It does not add a production supervisor or infer permission from EOF,
matching state, a report or an exit code. [HOST-TESTS.md](HOST-TESTS.md) records
the separate host validation evidence and its limits.

## Resources and control flow

The parent creates two unnamed socketpairs. The child data endpoint becomes fd0;
the child control endpoint becomes fd1. Both descriptors are duplex. The other
endpoints remain parent-owned. Stderr carries diagnostics, never protocol bytes.

The child constructs the frozen modeled-controller graph and transfers that
fresh Ready graph and fd0 into an allocated guest-stream owner before publishing
READY. The parent retains the actual Child handle before sending the one-byte
ASCII G command. No broker input is sent before that handshake. Neither the
handshake nor possession of a descriptor authenticates a runtime peer.

After G, the child drives bounded mbgs_step calls with explicit mbgs_next between
locally completed responses. It retains terminal stream state and publishes one
TERMINAL report without disposal. Normal and Unknown scenarios end after the
parent closes its data endpoint; the partial case closes after a failed host
write attempt, and the lost-response case ends on the actual EPIPE return.

The child then accepts only the one-byte ASCII D command for explicit fixture
disposal. This is justified by known serial modeled effects, not by the data
failure. It captures pre-disposal evidence, invokes mbgs_dispose once, and reports
the actual result and postcall owner pointer before exiting on known complete
cleanup. Retained resources, uncertainty or control errors cannot be silently
converted to successful cleanup. Reports and actual process wait precede final
functional assertions.

Control-channel I/O is fixture plumbing, separate from broker transport. A
truncated report, malformed command, failed report write, unexpected child exit
or timeout fails the fixture. Emergency termination of the private process, if
needed for a failed test, establishes neither guest disposal nor recovery.

## Fixed report layout

Every report is exactly 3,072 bytes. Integers are big-endian. Native structures,
padding and pointers are never serialized. All reserved bytes are zero; boolean
fields are strictly zero or one. Signed metadata values use sign-extended 64-bit
two's-complement representation. Unavailable metadata is zero.

| Byte offset | Content |
|---|---|
| 0 | Eight ASCII bytes AZSPRP01 |
| 8 | u32 version, exactly 1 |
| 12 | u32 kind: 1 READY, 2 TERMINAL, 3 CLEANUP |
| 16 | u32 case: 0 normal, 1 unknown, 2 lost, 3 partial |
| 20 | u32 complete report length, exactly 3072 |
| 24 | Eight reserved zero bytes |
| 32 | 64 metadata slots, eight bytes each |
| 544 | Canonical 200-byte modeled state |
| 744 | Current record, 1120 bytes |
| 1864 | Last completed record, 1120 bytes |
| 2984 | 88 reserved zero bytes |

The modeled state uses the broker wire encoding: instance[0], instance[1], run,
epoch and sequence as five u64 values; flags as u32; CRC length as i32; then all
152 frame bytes. It comes from a pure copy of the modeled globals, not an extra
STATE ioctl.

Metadata slot numbers below are relative to offset32:

| Slots | Values in order |
|---|---|
| 0–4 | owner_present, evidence_valid, evidence_archived, inspect_result, last_step_result |
| 5–10 | stream phase, current_operation, active, taken, descriptor_held, stream_fd |
| 11–17 | graph_present, graph_evidence_valid, last_completed_valid, first_terminal, terminal_operation, disposal_started, uncertainty_seen |
| 18–20 | disposal_attempts, graph_finalize_attempts, graph phase |
| 21–26 | claim_present, adapter_present, broker_present, dispatcher_present, graph_disposal_started, graph_uncertain_close_seen |
| 27–32 | wrapped modeled native_calls, STATEs, CLAIMs, FRAMEs, duplications, closes |
| 33–36 | modeled original_live, duplicate_live, owner_is_original, fenced |
| 37–40 | sigpipe_ignored, shutdown_attempted, shutdown_result, shutdown_errno |
| 41–46 | dispose_attempted, dispose_return, report.completed, report.result, report.owner_released, report.uncertainty_seen |
| 47–49 | report.graph_attempted, graph_returned, graph_result |
| 50–54 | stream-close attempted, returned, descriptor, raw result, raw errno |
| 55–58 | graph_report.graph_released, broker_attempted, broker_returned, broker_result |
| 59–62 | adapter-close raw result and errno, original-close raw result and errno |
| 63 | Number of mbgs_step calls made by the fixture |

Controller counters cover calls through the modeled syscall wrappers. Direct
fixture setup operations are outside those counters.

Fd numbers, enum values, API results, terminal results, close results and errno
fields preserve their numeric values; they do not convey use authority.

READY and TERMINAL carry current pure inspection with evidence_archived=0.
CLEANUP carries the saved pre-disposal inspection with evidence_archived=1.
Its owner_present field comes from the actual pointer after disposal; its
disposal fields come from the actual returned report. Therefore archived held
flags may remain true after known consumption and must not be interpreted as
current ownership. No freed owner is inspected.

Each 1120-byte record begins with 24 eight-byte metadata slots:

| Slots | Values in order |
|---|---|
| 0–2 | record number, read_calls, write_calls |
| 3–7 | request_received, request_expected, response_length, response_sent, header_valid |
| 8–13 | last read: attempted, returned, offset, requested, raw count, raw errno |
| 14–19 | last write: attempted, returned, offset, requested, raw count, raw errno |
| 20–23 | dispatch: attempted, returned, raw result, written |

The exact request buffer256 starts at record+192. The exact response buffer668
starts at record+448. The final four bytes at record+1116 are zero. Raw I/O counts
and errors are signed64; size/counter values are unsigned64. Credited lengths,
not nonzero buffer bytes, define valid progress. The parsed request header can
be checked independently against retained raw bytes when header_valid is true.
An invalid last_completed flag leaves no completed-record claim.

## Four cases

Normal adopts once and sends two successive immutable FRAME requests. The host
must validate both matching fresh receipts and advance its sequence/request
accounting correctly. The peer's final native model and operation counts must
agree independently. Explicit next and eventual EOF must retain the completed
last frame without replay.

Unknown delegates a FRAME to the exact extracted model operation, then reports
EIO through the modeled controller wrapper after commit. The actual broker
produces Unknown. Its receipt.expected_after was predicted before submission;
it can match the committed model even while the broker's acknowledged expected
state and host accounting remain unchanged. No equality turns it into a fresh
positive ACK.

Lost response performs a real SHUT_WR on fd0 after FRAME dispatch and before the
unchanged POSIX response write. The fixture installs process-local SIGPIPE ignore
so that write can return EPIPE. Both shutdown result and stream raw return remain
reported. The host sees a missing response and keeps Unknown; the graph and exact
response bytes remain retained until D.

Partial request uses a host Write adapter that actually sends 61 bytes, then
returns an error on a later call. After host data closure, the C owner observes
EOF with a complete 48-byte header and only 13 body bytes. It must preserve those
bytes and avoid dispatch entirely.

Positive fragmentation can vary with OS scheduling. Tests may limit host call
sizes and compare exact byte transcripts, bounded offsets and returned-call
relationships; they must not invent deterministic guest syscall counts.

## Boundaries

The production Rust modules, C graph/children, guest-stream owner and POSIX
stream wrappers remain unchanged. Driver surroundings and controller syscalls
remain modeled. The actual stream wrapper runs only against private local
socketpairs. Signal handling, shutdown, handshake and process coordination belong
to this fixture, not a new deployment API.

Inspection is serial between guest-stream steps. There is no concurrent C
inspection, asynchronous cancellation or bounded-blocking claim. Child retention
until explicit wait covers only this controlled process lifetime; final Drop,
abrupt death and external aliases remain separate hazards. The older supervisor's
stdin-EOF kill policy is neither invoked nor adopted.

No named endpoint, emulator control, physical deck, native controller acquisition,
main wiring, Linux ARM64 stream build, installation or release is authorized by
this fixture. Native-stop authority, durable custody, authenticated/exclusive
peer binding and full recovery remain open.

# Inactive broker wire seam v1

Scope: an offline C codec/serial dispatcher over the actual frozen RESTORE
broker and a Rust MixerBackend over one caller-supplied Read + Write + Send
stream. No endpoint discovery/open/connect/listen, guest main, acquisition/CLAIM,
adapter attachment/disposal, main wiring, reconnect, retry, SSH fallback,
ROTATE, RESTORE, reconcile or release operation is provided.

This is a borrowed guest-broker reference and a retained host stream. Neither
proves broker/adapter pairing, sole-mutator ownership, peer authentication,
native observation, stopped identity, durable custody or recovery permission.
A 16-byte nonzero session label is caller-supplied correlation, not a secret
capability or authenticated identity. The caller must bind it to one retained
broker and supply authoritative initial state separately. Existing runtime,
staging, native ownership, USB and release gates remain CLOSED.

## Canonical encoding (frozen layout)

All unsigned integers are big-endian. Signed 32-bit fields use two's-complement
bits; C decoding must not rely on implementation-defined unsigned-to-signed
conversion. Byte arrays are copied unchanged. Semantic C structs are never
cast or serialized as memory. No unbounded allocation or implicit normalization.

Every record starts with this 48-byte header:

| Offset | Width | Value |
|---|---:|---|
| 0 | 8 | ASCII AZMBRPC1 |
| 8 | 4 | version 1 |
| 12 | 4 | kind: ADOPT request 1, FRAME request 2; responses 0x80000001 / 0x80000002 |
| 16 | 4 | exact total length below |
| 20 | 4 | reserved zero |
| 24 | 16 | nonzero immutable session label |
| 40 | 8 | exchange ID, 1 through UINT64_MAX-1 |

State is exactly 200 bytes: instance[0] u64 at0, instance[1] u64 at8,
run u64 at16, epoch u64 at24, sequence u64 at32, flags u32 at40,
CRC length i32 at44, raw frame[152] at48.
Grant is 40 bytes: instance0/1, run, epoch, producer, each u64.
Request is 200 bytes: grant at0, producer request ID u64 at40,
raw frame[152] at48.
Command is 196 bytes: instance0/1, run, epoch, sequence (five u64),
injected u32 at40, raw frame[152] at44.

| Record | Length | Payload after offset48 |
|---|---:|---|
| ADOPT request | 256 | state200, producer ID u64 |
| FRAME request | 248 | request200 |
| ADOPT response | 296 | mb_result i32, provenance u32, state200, grant40 |
| FRAME response | 668 | mb_result i32, provenance u32, outcome u32, operation u32, backend error i32, reserved u32 zero, request200, command196, expected_after state200 |

Provenance is 0 for the initial returned result, 1 for retrieval of the exact
cached last exchange. No other value is valid. Kind, version, exact length,
reserved fields, nonzero session and nonzero/non-max exchange are checked before
dispatch. Inputs contain exactly one record; trailing bytes to the codec are
refused. On a byte stream, the host consumes only the bounded expected response
record; it cannot prove that an unobserved suffix is absent. No resynchronization
scan or retry follows a malformed/truncated record.

## Guest serial dispatcher

The implementation lives in wire.h/.c and guest_dispatch.h/.c. It borrows one
already-created mb_broker. Constructor input includes the session label,
complete expected injected state and producer ID. Expected shape requires
nonzero instance pair/run/epoch/producer, flags31, CRC96, sequence below max.
The dispatcher never acquires, closes, destroys or replaces that broker or its
backend. Caller retains it through every call and externally serializes access,
including inspection and destruction. No callback reentry is supported.

Use an opaque heap object, explicit create/destroy, and copied diagnostics if
useful. Destroy frees only local storage and requires external quiescence.
Constructor checks actual mb_inspect: full expected state, no unresolved
command, admission closed, grant not issued and next_request_id1. This is local
broker correspondence, not a fresh kernel snapshot or external alias exclusion.

First exchange is exactly1/ADOPT. Request full state and producer must match the
retained seed. Invoke actual mb_adopt, whose own fresh exact snapshot check stays
unchanged. Copy full request into the journal and mark armed before the call;
retain the exact returned mb_result and grant before serialization.
Only MB_OK with the exact expected grant opens this dispatcher's FRAME path.

Later exchanges advance exactly one from the retained last exchange and are
FRAME only. They must carry the exact retained grant and expected producer
request ID (starting1). Refuse wrap/UINT64_MAX before calling the broker.
Retain the exact request before actual mb_submit, and the full returned API
result/receipt afterward. Do not infer COMMITTED from MB_OK.
A returned nonzero API result, UNKNOWN or REJECTED closes further dispatch.
Do not call quiesce, reconcile or destroy as an implicit consequence.
A valid positive receipt advances local expected state and next producer request ID; the producer ID and grant stay immutable.
Validate full receipt correspondence before advancing; mismatch closes the
dispatcher and must not manufacture a usable positive receipt.

One exact last canonical request may be retrieved without redispatching even
after closure. Compare every byte and length before returning its retained
response with provenance1. It is historical evidence, not fresh state.
Same exchange ID with changed bytes, skipped/old IDs, wrong session, invalid
record, repeated ADOPT or wrong grant permanently closes this dispatcher and
produces no usable response. Preserve its earlier journal. No ID is retagged,
no automatic replay, and no replacement session is created.
The last exchange cache is bounded, volatile, and not durable deduplication.
The supplied broker remains independently reachable to aliases; this wrapper
cannot fence them.

## Host MixerBackend

New broker_wire.rs uses actual crate mixer_route::MixerBackend and producer
Envelope/BaselineReceipt/FrameAck types. It owns a caller-supplied boxed trait
combining Read + Write + Send. Its constructor also consumes exact initial
state, producer ID and session label. Constructor validation failure must return
ownership of the supplied stream with the error, or retain an inert backend and
evidence; it must never silently discard the capability. Source module stays
inactive in main.

The first and only baseline call creates ADOPT exchange1. Before any I/O,
retain the complete canonical request and armed phase. A valid fresh response
requires matching kind/session/exchange, MB_OK, provenance0, exact initial state
and exact expected grant. Return that complete BaselineReceipt once. Any other
result is a terminal baseline error; never query/adopt again.

submit requires successful baseline, exact original grant and the next producer
request ID. Encode an immutable FRAME request under the next exchange ID.
Refuse local shape/counter/wrap errors before I/O. For a request that may have
been transmitted, retain it and all copied response evidence on any ambiguity;
return BackendOutcome::Unknown and block future writes. Local preflight refusals
return UNKNOWN without I/O and close the wrapper. Only a full matching fresh
broker receipt may return definite REJECTED. This also covers a repeated earlier
COMMITTED Envelope: no-I/O on the repeated call cannot make that prior commit
a rejection. Once any exchange is ambiguous or terminal, every later submit
returns UNKNOWN without I/O, including the same Envelope. Armed state itself
refuses reuse if a caller catches a panic from the stream before the original
call returns.

Only exact fresh positive FRAME evidence is accepted:
- MB_OK, provenance0, outcome COMMITTED, operation FRAME, backend error0.
- Every echoed request field and all152 bytes match the retained Envelope.
- Command matches instance/run/epoch, exact current sequence+1, injected1 and all152 bytes.
- expected_after matches complete prior state with that sequence, flags31,
  CRC96 and requested raw152 bytes, including CRC positions and tail.

Then return FrameAck with the exact request and kernel sequence, advance expected
state and next producer request ID; the producer ID and grant stay immutable. A definite REJECTED requires the same complete attempted
request/command/expected_after identity, MB_OK and positive backend error; it
closes further writes. UNKNOWN, malformed/partial/mismatched or cached responses,
any nonzero broker API result, and all transport faults remain UNKNOWN. In
particular MB_ERR_MUTEX can occur on unlock after a real callback. The adapter's
direct-ioctl errno allowlist must never classify socket/stream faults.

Positive short reads/writes may continue from the current offset of this one
record. Zero progress and any error (including Interrupted) fail terminally;
no restarting the record or command. Flush exactly once after the complete
request and before any response read; flush error or panic is terminal and
uncertain. Progress counts bytes accepted/returned by the supplied trait, not
proven delivery to the guest. No deadlines/cancellation or reconnect are
claimed. A supplied stream can block forever. It must not reenter this backend.
After a response is accepted, an unread suffix remains unobserved until later
I/O and is not automatically drained.

A separately clonable evidence handle should copy constructor/config, phase,
pending canonical bytes, byte progress and returned response/result without
locking the stream. Evidence is not an atomic distributed snapshot or disposal
permit. No implicit stream cleanup is a guest cancellation or native release.

## Focused offline verification

Compile C codec and dispatcher as standalone translation units against pinned
actual broker.h/.c. Use a synthetic backend only. New tests must exercise actual
mb_create/mb_adopt/mb_submit, exact duplicate versus conflict, all identity and
raw-tail fields, nonzero API result classification, unknown/rejected outcomes
and counter exhaustion. Do not rerun old broker/kernel suites.

Compile new Rust module/tests in a separately prepared actual preview candidate,
retaining all252 inherited test names as filtered. Use a private C fixture
process over pipes (no socket or emulator) to exercise actual C dispatcher/broker
with the Rust backend, plus controlled partial/failing I/O wrappers. Verify
both directions against independently constructed canonical bytes, not only
round-trips. Cover invalid/truncated headers, mismatched complete receipts,
cached positive replies, partial send/read, EOF after possible commit,
nonzero API result with otherwise positive receipt, no repeated baseline or
send after ambiguity, and independent evidence while I/O is blocked.

Full exports, binaries, fixture-process logs and scripts remain private.
Publish source and sanitized evidence only. No native receipt authority,
live Linux fd custody, actual process recovery or physical timing follows.

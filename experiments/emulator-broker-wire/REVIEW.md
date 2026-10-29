# Broker transport boundary review

Static review of the pinned emulator tree (06fc981707e69982c0554bb508e0c646c005a979)
and frozen ownership/preview predecessors. This is not a runtime observation.

The legacy path cannot carry the required request/receipt contract unchanged:

- Preview controls.rs sends one bus byte plus 152 frame bytes. It caches mixer
  state before writing, coalesces disconnected input and replays cached buses on
  reconnect. A successful stream write supplies no guest commit receipt.
- Guest forwarder.c reads those 153-byte records and writes the legacy
  controller. A refused controller write closes/reopens the descriptor and drops
  the frame. Reverse 157-byte records carry LED/MOSI/level data, not matching
  operation acknowledgements.
- The forwarder's read helper loses partial-record progress after EOF/error.
  The next loop begins another fixed record without a versioned framing boundary.
  Its write helper handles positive partial writes and EINTR but makes no progress
  on a zero-length write; callers also have abandoned/ignored output-error paths.
- The main path replaces the global controller descriptor independently of the
  output threads' descriptor lock. That existing lifetime model cannot establish
  retained one-broker/one-adapter custody.
- QEMU's control stream server does not make a successful second connection proof
  of a serviced peer. The documented backlog behavior and drive helper's socket
  retry/SSH fallback do not provide command outcome authority.
- Automatic guest service restart has no retained broker journal. Reconnect and
  restart must not silently create fresh ownership after an uncertain command.
- The separate ownership candidate permanently fences legacy mixer mutation; the
  preview transport successor already removes owned mixer commands from the
  legacy queue/cache. Restoring legacy replay would contradict that boundary.

The new seam therefore has explicit bounded framing, immutable session/exchange
and producer request identities, full semantic receipts, one-shot adoption and
no reconnect. This decision does not validate an endpoint, descriptor transfer,
guest supervisor or peer identity. A caller-supplied label is correlation only.

## Broker return semantics

Actual frozen mb_adopt takes a fresh backend snapshot and issues one grant.
Retrying adoption after a lost reply is not an idempotent recovery operation.
mb_submit records the next request before invoking its backend and distinguishes
COMMITTED, definite REJECTED and UNKNOWN. MB_OK alone says none of those.
The broker's mutex-unlock helper may return MB_ERR_MUTEX after a real callback
and a populated receipt. A nonzero API result with an apparently positive
receipt must remain uncertain.

The broker caches only the last matching FRAME receipt. Exact cached retrieval
is historical evidence, not fresh authority or assurance that another alias has
not changed native state. The wire dispatcher adds an explicit cached provenance
bit and the host accepts only a fresh response. Its local borrowed-broker check
cannot exclude aliases or establish native adapter ownership.

## Reviewed fail-closed boundaries

The host retains its supplied stream even when configuration is invalid.
Before every exchange it journals the complete request and armed phase. A caught
stream panic leaves an armed operation that cannot be reused. Once an attempt is
uncertain, later calls remain Unknown even if those later calls perform no I/O;
they cannot prove that the earlier transmission did not commit. A repeated
previously committed Envelope is also Unknown without I/O, never a fabricated
noncommit. All local preflight refusals use Unknown; only a matching fresh broker
receipt can supply definite Rejected.

Positive partial reads/writes continue only the current record offset. One flush
follows a complete request. An error, zero progress, flush failure, malformed
response or cached positive receipt closes further transmission. Progress counts
trait-level bytes, not delivered guest bytes. A supplied stream can block
indefinitely or buffer data; evidence snapshots use a separate journal and are
not distributed atomic snapshots.

Only exact full receipt correspondence can advance the expected state and next
producer request ID. The producer ID/grant remain immutable. All 152 raw bytes,
including CRC positions and tail, participate. Neither wrapper normalizes a
frame or treats metadata/mask change as intent.

## Remaining work

An owned guest activation/acquisition route must retain a live Linux descriptor,
adapter and broker and bind an authenticated/exclusive transport peer to that
exact instance. It must establish authoritative originals and stopped-process
identity, exclude external writers/readers, preserve uncertain pending commands
through process failure, and define actual recovery before any guarded restart.
A Rust trait object can also alias resources outside the wrapper.

No endpoint is created here. No actual native ioctl, guest main, physical deck,
source selection, MODS delivery or stems activation is exercised. No deadlines,
native consumption/dwell, callback cancellation, full overlay/both-file restore,
durable custody, audio quality or physical timing follows from these codecs.
All existing runtime, staging, native ownership, USB and release gates stay CLOSED.

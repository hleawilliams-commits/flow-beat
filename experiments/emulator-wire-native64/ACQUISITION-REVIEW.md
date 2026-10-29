# Acquisition and lifetime boundaries remain open

This checkpoint composes existing wire, broker and native64 adapter sources in a
host fixture. It does not add a runtime owner or claim that a wrapper around the
frozen constructors would make acquisition or destruction safe.

## CLAIM preserves content and creates a lasting fence

In the exact three-patch driver prepared by this experiment, controller open
does not claim ownership. The CLAIM handler checks writable mode, capability,
opt-in, version, reserved-zero fields, instance, nonzero run, prior epoch,
epoch capacity, sequence zero, absence of an owner and CRC96. Its locked transition advances the epoch, sets sequence zero, run, owner
and permanent legacy fence. It preserves the selected raw frame and injected
mode; it does not compare an expected baseline.

STATE takes a locked snapshot and copies it to the caller after unlocking.
Legacy input can change between a pre-CLAIM snapshot and the CLAIM transition.
A matching preflight therefore does not establish the post-acquisition content.
After CLAIM, content mismatch leaves an owned, fenced session requiring retained
custody. Final file release and the RELEASE ioctl clear the owner without
restoring the content or removing the fence. They are not rollback.

These findings come from static review of the pinned prepared driver. Seven
other exact methods are extracted and hashed in each result; controller open is
not among them and is not executed by this fixture. Raw prepared sources remain
private. Relevant prepared-source lines are controller open/release 482–497,
STATE 527–545, CLAIM 585–602, RELEASE 618 and legacy writes 641–690.

## The current constructors do not own the full graph

The [native64 adapter](../emulator-native64-adapter/adapter.c) duplicates an
already-claimed original descriptor and validates a complete poststate. On failed
validation it closes the new duplicate once, frees adapter storage and returns
null. Its report retains the primary error and raw close result; an uncertain
close does not return usable custody of the internal duplicate. The original
must remain owned and stable throughout attachment. Other original, duplicated
or inherited references remain possible mutators.

The [broker](../emulator-broker-restore/broker.c) borrows that adapter. Its final
mutex unlock may replace a success result even after output has been populated.
The dispatcher retains the API result separately from returned grant/receipt.
The mb_destroy function returns void; if mutex destruction fails it leaves the
broker allocated. Unconditionally disposing the adapter after that call does not
prove all references have disappeared.

Adapter disposal consumes its handle before attempting one close and returns a
report. Neither the consumed handle nor the zero API result establishes safe
recovery. The integration fixture destroys only its own serial, quiescent graph
after copying test observations. That test cleanup is not a runtime teardown
protocol, including for intentionally unresolved synthetic sessions.

## A concrete next seam

A separately reviewed owner can allocate storage before accepting transfer of a
caller-supplied original fd. It must retain that original, exact supplied plan,
prestate, armed CLAIM, raw return and poststate before later steps. A successful
CLAIM must be followed by full correspondence checks, not inferred readiness.
No automatic FRAME may convert an owned idle baseline into an injected baseline:
the adapter/broker accept flags 23, while producer adoption and wire admission
require flags 31.

To retain every acquired duplicate through failed validation, the adapter needs
a separate retain-before-validation interface. It cannot be obtained solely by
wrapping the current mba_attach. Any next owner must keep each returned
adapter, broker and dispatcher before subsequent calls, close admission on
failure, and preserve the graph and uncertainty. A checked destruction protocol
is separate work, not a constructor cleanup convenience.

CLAIM needs its own pinned error classification. In particular EBUSY is a
pre-mutation refusal in CLAIM, while the current FRAME/ROTATE/RESTORE adapter
conservatively classifies EBUSY as UNKNOWN. A shared allowlist would erase that
distinction.

Exact expected content, module/run identity, authoritative original baseline,
native stop and exclusion still need independent provenance. Opening a path,
observing matching values, or assigning a transport session label supplies none
of those guarantees. Endpoint/peer binding, supervisor custody, deadlines,
process death and durable recovery remain open. No new acquisition, adapter
variant, destruction API or runtime wiring is implemented here.

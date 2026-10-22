# Passive route observer with bounded off-thread capture

This unpublished source candidate replaces the previous observer's one-shot
off-thread mailbox with **16 immutable slots per process**. It preserves the
[original route observer](../route-observer/) and its single-sample runtime
result. A subsequent [bounded emulator run](runtime-evidence.md) captured all
four planned detents and selectively restored its fresh baseline. The unique module is `encoder-route-burst-observer` version `0.0.1`, log
prefix `ERB`, with opt-in `AZ_ENCODER_ROUTE_BURST_OBSERVER_EMULATOR=1`.

The entry remains `0x1b01cf8`; both exact firmware slots `0x2d453e8` and
`0x2d45758`, the full opening prologue and firmware tag are checked before one
exclusive claim. [Binding evidence](binding-notes.md) covers this new source.
The five-input assembly wrapper is byte-identical to the prior reviewed ABI,
SHA-256 `a1f1cf42cc587c3222d299ac9dd31bfd0944f1ea51ec98a19d74ba40088a5034`.
Its hidden `az_dispatch_*` symbols remain local to this separately loaded module.
No old ABI execution is represented as new evidence.

## Reservation, publication and consumption

An off-thread helper invocation copies five opaque input scalars (`x0`, `x1`,
full `x2`, `w3`, `w4`), original LR and its normalized Linux TID. It reads no native
argument objects and performs no later pointer reads or calls. Native execution
continues through the unchanged tail wrapper.

The producer loads `off_claimed` once. A value at least 16 increments
`off_capacity_dropped` and exits. Otherwise it makes **one source-level strong
CAS** from the observed value to its successor. Failure increments
`off_contention_dropped` and exits without using the failure-updated expected
value as an index. A failure is classified as contention even when another
writer just filled slot 16. There is no source retry, unbounded fetch-add,
rollback, reset or reuse. The successful reservation alone owns that slot.

Every slot starts explicitly EMPTY with a zero-initialized record. Its owner
sets WRITING, copies the complete record and publishes READY with release
ordering. The single worker scans all 16 slots each cycle. Only an acquire read
of READY authorizes the record copy, after which it marks the slot CONSUMED.
It can process a later READY slot while an earlier slot remains WRITING and will
revisit that earlier slot next cycle. `off_claimed` does not authorize a payload
read or imply READY. Slots remain occupied after consumption.

Off-thread reentry may claim another slot and finish before its caller. Each
helper preserves its own saved errno. Logs distinguish `lane=off-thread-burst`,
`off-slot=1..16` and `main-seq=0`; main-thread records use `off-slot=0` and their
existing main sequence. **A slot number is a reservation identifier, not native,
completion or global temporal order.** All TIDs are copied; Linux TID/PID equality
is only this observer's lane selector, not the native owner-thread predicate.

CONSUMED means copied by the worker, not successfully logged. The combined
main/off-thread native log budget remains 128 records, and the RAW budget
remains 128, with eight diagnostic summaries and one stop message at most.
Reaching either budget's next record stops the worker and disables capture;
an already copied record can be unlogged, and pending records need not drain.
A producer admitted before `live` becomes false may still finish publication.
The worker does not wait for such producers. No retained lifetime or shutdown
quiescence proof is claimed. Module mapping and original-target forwarding
requirements remain those of the prior source candidate.

Slots can be exhausted by unrelated earlier off-thread entries. Capacity and
contention losses are separate cumulative counters; neither identifies missing
argument values. Even a complete 16-slot sample is not proof of an uninterrupted
native event stream. Lock-free atomics and a single source CAS do not establish
a hardware wait-free or measured latency bound: ARM exclusive-store machinery
can retry internally under contention.

## Preserved behavior and limits

The main-thread guarded ring, RAW lane, state/live gates, early attempted/gated
counters, signed TID normalization, errno handling, tick heartbeat and worker
watchdog remain unchanged apart from module identity and the new off-thread
accounting. Initialization still registers the single RAW filter before starting
the worker and makes the native claim its final fallible service operation.
A failed initialization leaves the callback locally inactive; core loaded-state
and quarantine eligibility remain separate protections.

RAW records still copy only changed bytes 6 and 31–35, never edit the buffer and
return zero. They are checked SDK input **before core edits**. There is no normal
callback, and no claim that RAW values reach a native handler unchanged. Native
and RAW sequences are independent; worker log order or matching values do not
establish causality. Early gate counters are individual wrapping 32-bit atomics,
not coherent snapshots or proof of every historical state transition.

There is no explicit allocation, clock, log call or native-object access in the
capture paths. Host tests do not measure allocation, first-use TLS resolution,
real syscall cost or physical filter timing. These limits and native returns,
child identity, queue ownership, cancellation and focus handback remain open.
No menu, input consumption, navigation, stems activation, core watchdog change
or community release is included.

The scalar status allowlist is 27 own-module OBJECTs: 25 four-byte counters/state
values and two eight-byte sequences. It substitutes `off_claimed`,
`off_capacity_dropped`, `off_contention_dropped` for the prior two mailbox
scalars. The slot array and native values are excluded from scalar status reads.
Any later process sampling must remain sequential/noncoherent and independently
validate exact binary, mapping, identity and widths.

## Host and build evidence

The [actual-source runner](tests/run-tests.py) passes **32,339 assertions across
37 scenarios per plain, ASan/UBSan and TSan mode**. It verifies all 30 tracked
parent files against [frozen fingerprints](frozen-parent.json), compiles this
candidate in a unique ignored snapshot, enforces exact scenario counts and
rejects sanitizer diagnostic text. The [host result](host-result.json) binds its
source, fixtures, runner and SDK header.

New cases fill and consume all 16 slots, reject the seventeenth and later calls,
verify no reuse or overwritten payload, reject a `UINT_MAX` saturated counter,
force stale-CAS collisions including the last slot, hold an earlier writer while
a later slot is processed, and reenter the helper after reservation. They check
all scalar fields, high `x2` bits, LR, per-record TIDs and errno. Sixteen reserved
writers then publish concurrently against the consumer; a separate visibility
case prevents writer completion until the reader has copied its record. Payload
publication therefore depends on READY ordering, not a producer join. Existing
main/RAW, initialization, loss, watchdog and combined main/off-thread budget
coverage remains.

Six strict controls each fail exactly one expected assertion in every mode:
late attempted accounting, RAW mutation, premature READY, treating a failed CAS
as ownership, stopping the worker at a WRITING hole, and resetting CONSUMED.
These are specific discriminating schedules, not whole-engine concurrency or
native lifetime proof. Reservation CAS remains relaxed because it grants unique
ownership only; READY release/acquire publishes the payload.

The ARM64 candidate builds and passes SDK validation. [Build metadata](build-result.json)
records its exact binary and source fingerprints, the actual cached SDK image,
and verified scalar widths. No atomic helper is dynamically imported. No burst
host/test/build failure occurred in the completed matrix or SDK build; unique
source/control/build snapshots and development outputs are retained privately.
Earlier parent-experiment failures remain in their original evidence.

Reproduce without runtime installation, from this directory:

```sh
sh tests/run-focused.sh --result /absolute/private/path/burst-host.json
python3 /absolute/path/to/az-mods-sdk/tools/azmod-pack build .
```

The default SDK is the existing sibling checkout. Use an absolute `AZ_SDK`
override because the shell wrapper changes into this directory first. The subsequent [runtime evidence](runtime-evidence.md) is separate from these
source/build results and includes its own fresh baseline, capture and selective
restoration. Native returns and event ownership remain unproven.

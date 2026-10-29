# Ordered ACK callback and host file custody

This inactive Unix component implements the actual preview `SettingsSink` trait.
It composes the unchanged strict mapper with the unchanged retained settings-pair
engine. It adds no live factory, endpoint, native adapter, dependencies or startup
behavior. Main still supplies the literal absent owned pair.

## Construction and admission

`SettingsSinkBridge::attach` consumes an explicit directory file, one complete
baseline, a nonzero temporary namespace, an ordinary creation mode and an explicit
selector/TIME selection. It creates its pair writer and pair evidence internally,
so a caller cannot pair a writer with another writer's diagnostic handle.
The selection and baseline are fixed for this bridge's lifetime.

Each callback must have the exact key, next request ID and next kernel sequence.
The first request ID is one. All duplicates, including an exact completed ACK,
are terminally refused before mapping or pair I/O. Noncontiguous requests,
wrong keys and exhausted counters likewise close subsequent writes. This is
stricter than the standalone pair's historical duplicate-cache behavior.
The accepted attempt and first pre-admission refusal are separate retained values;
later refusals cannot replace the last accepted plan or original closure cause.
Journal poison remains visible and cannot authorize work. A poisoned journal
cannot be updated to save a new refusal.

`FrameAck` is a constructible value. These checks do not authenticate receipt
provenance, prevent deliberate reauthoring or validate the native ABI. The actual
route validates its outstanding envelope and expected sequence before invoking
this callback. Binding the constructed pair's baseline to the backend's
authoritative baseline remains an external prerequisite.

## Retained operation order

1. Copy the same pair engine's last verified mixer image without filesystem I/O.
2. Publish `MappingArmed` with the complete ACK and that exact source image.
3. Map only that ACK and source with the fixed selection.
4. Publish the full mapped knob/mixer targets and `PairArmed` before pair I/O.
5. Invoke the pair engine once. Record `ReturnedOk` with its full receipt, or
   `Failed` with the typed mapper/pair error and terminal cause.

No bridge journal mutex is held across mapping, pair I/O or test fault hooks.
Mapping errors leave pair history/files untouched. Pair errors may leave a mixed
pair and retained temporary files; the pair's exact stages distinguish what was
attempted and verified. No rollback, repair, temp cleanup or FRAME retry follows.
Bridge counters track successful host persistence and may lag a known native
ACK after callback failure. Counter exhaustion after a final successful commit
closes future writes without reclassifying that successful callback.

The source image is a historical verified image, not a new read of named files.
The pair engine performs its own drift checks before mutation. Neither component
excludes competing writers, makes two renames atomic, or protects readers from a
mixed pair. The frozen writer-boundary audit continues to apply.

## Panic, evidence and callback completion

An unwind propagates. A prior armed phase refuses further attempts even when no
terminal error was recorded. Test hooks after mapping intent, after the complete
plan and after pair return run outside the bridge journal lock. A panic after
pair success but before bridge completion can leave bridge `PairArmed` alongside
pair `Committed`; this is missing callback completion, not permission to infer,
finish or replay it. The actual producer has already recorded the known native
ACK before invoking settings, and preserves `CallbackArmed` on callback panic.

A cloneable evidence handle retains both journals and the pair directory handle
after sink/route Drop. Its snapshot copies bridge then pair separately. Those
copies are noncoherent across the two components and perform no filesystem I/O.
The bridge completion record is not independent proof that the producer observed
the callback return; consult the producer checkpoint after the callback/worker
has finished, respecting its separate locking.
They are bounded in-process diagnostics, not a durable journal, recovery token,
fresh filesystem observation or teardown permit. Final handle Drop/process death
still loses custody; allocation failure and real power loss are not simulated.

## Native admission remains a separate gate

A settings callback error occurs after the backend's FRAME commit. The frozen
producer consumes that exact ACK and closes its current generation; it does not
leave a replayable unresolved request. Queued same-generation work cannot resend
the FRAME to repair files.

However, the frozen route can refresh/adopt a newer baseline after a callback
error. The permanently closed bridge refuses the later epoch only when its
callback runs, after that later FRAME has already committed in the backend.
A mismatched constructor/backend baseline has the same late-refusal limitation.
A future lifecycle owner must prevent failed-route rearming and establish
baseline correspondence before allowing native admission. This bridge alone
does not provide that fence.

## Unresolved runtime gates

Enforceable external writer/reader exclusion, native acquisition/FFI and receipt
authority, authoritative originals and stopped-process identity, callback
deadlines, native dwell/consumption, both-file and overlay restoration, durable
custody, final Drop/death and guarded restart remain unresolved. No emulator or
physical endpoint, application main, window, installed driver, guest service,
core, community package or catalog is changed. All existing runtime, staging,
native-ownership, USB and release gates remain CLOSED.

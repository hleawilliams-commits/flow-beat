# Stopped recovery remains unimplemented

The kernel generation primitive does not repair the frozen runner's restart
ordering. The earlier [actual-source counterexamples](../mods-shortcut-delivery/task-sdk-observer/overlay-policy-v2/RUNTIME-GAPS.md)
remain valid: `while_stopped` can restart before overlay restoration, and later
host-counter restoration does not repair the sticky controller frame. Neither
that runner nor its private helpers changes in this experiment.

This is a prospective contract for a separately reviewed broker and recovery
coordinator. It is not a permit, runtime state machine or implemented installer.

## Admission must retain a reproducible baseline

Before any input-bearing run, the coordinator needs a fresh owned snapshot tied
to the exact module instance, live broker open description, native process/start
identity, run scope, driver and producer sources. It must retain all 152 raw
mixer bytes, CRC prefix, injected/idle mode and the exact original host-counter
file bytes, mode and receipt-bound identity. A previously saved frame is not an
input baseline. A fresh snapshot still needs the enforced ownership and producer
barriers; equal observations alone do not supply them.

The initial baseline must independently satisfy the reviewed full-frame neutral
policy and host/overlay counter agreement. Refuse unsupported state or mismatch;
do not normalize it. To use only this patch's operations, the initial snapshot
must already have INJECTED set. FRAME and ROTATE always set it: writing an idle
template back does **not** restore an original noninjected state. Supporting that
case requires a separately reviewed tagged restore-to-idle operation and tests;
legacy clear is neither available nor an acceptable substitute after fencing.

Ownership/fence metadata intentionally changes during the experiment. The
restoration target is the admitted original overlay content/mode and host state,
while retaining controlled ownership; it is not a promise to restore the entire
pre-claim kernel state or return to legacy producers.

## Required ordering before restart

1. Close admission from all upstream producers and establish the exact retained
   broker/module generation. Identify any unknown request outcome; do not treat
   a low STATE sequence as proof the request was cancelled. Keep the broker
   description open and preserve diagnostics.
2. Obtain a verified, scoped stop of the intended native application. A command
   return or PID-only observation is insufficient for process/start identity or
   native quiescence. Failure must not reach an unconditional restart, fallback
   start, successful overlay receipt or producer resumption.
3. While stopped, revoke the old generation and commit the saved full neutral
   frame through the owned ROTATE operation. Verify the resulting instance,
   owner/run, new epoch, sequence 0, INJECTED mode, CRC prefix and all raw bytes.
   The broker must keep new-generation admission closed. A conflicting commit
   or unknown rotation result must remain unresolved, not be overwritten by a
   fresh retry with a newly inferred sequence.
4. Restore the exact original host-counter bytes/mode where required using its
   separately scoped receipt. Verify both host state and the complete owned
   overlay again while stopped. There is no atomic transaction spanning the
   kernel and host filesystem; exclusive admission and retained stop must cover
   this whole interval. A host-only success does not complete overlay recovery.
5. Only after those checks may a guarded restart of that same intended service
   be considered. Validate its fresh process/start identity and required startup
   state while retaining ownership and closed producer admission. A successful
   start is distinct from successful file or overlay restoration. No automatic
   fallback may mask failed restoration or an uncertain start.
6. Adopt the verified baseline in the next producer generation using the separate
   source-level handshake. Do not replay cached preview frames, treat enqueue as
   commit, or allow physically held UI inputs to rearm without release. Native
   events already sampled before the stop/rotation need their own lifetime and
   quiescence evidence; this kernel patch does not cancel them.

A verified restoration phase should be idempotent: repeat observations and
receipt checks rather than repeating completed writes or stop/start cycles.
Read-only refusal and host-only cleanup have separate paths and do not require
inventing a new guest restart. Recovery authorization must remain separate from
a fresh installation/input permit, so failure after installation does not make
restoration depend on reopening an input gate.

## Failure cases that still need actual-source interaction tests

- Owner/broker loss leaves the overlay unchanged and possibly held. Closing a
  file is not neutralization. Any successor recovery acquisition must be explicit,
  bind the observed module instance, revoke old generations and keep the
  application stopped; it cannot simply resume the old producer.
- A reboot/module reload changes the instance probabilistically. Restoring or
  cloning a VM snapshot can repeat it. A controller receipt does not establish
  continuity across those events. Old output and manifests must not be reused.
- A pending FRAME can race an uncertain ROTATE. One next sequence wins, and the
  losing operation cannot be silently renumbered. Test both orders and retained
  receipt reconciliation under real broker transport failure.
- Recovery refusal, host replacement, source drift, native restart failure and
  producer-disconnect/reconnect must remain distinct observable outcomes.
- Exact baseline restoration from a noninjected initial state is unsupported.
  Full bytes alone must not cause a successful recovery receipt in that case.

None of these steps has been executed against a device by this checkpoint. The
old V2 runtime dispatch remains unconditionally disabled. No stale cleanup,
old emulator snapshot restoration or physical-deck operation is authorized by
this document.

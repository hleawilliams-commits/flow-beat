# SDK worker and adapter lifetime review

This is an offline review of the pinned SDK/core contract for this passive
adapter. It identifies requirements for the adapter and its tests; it does not
report an installed observer or treat a mocked SDK result as native execution.
The existing [hook lifetime review](../task-hook-lifetime/) remains unchanged.

## Source identity

All source below was read using `git show` at upstream commit
`3e0c5cb010454228f4ee63226b60d060494eeaae`, preserving the dirty upstream
checkout. Line numbers refer to that commit.

| Source | SHA-256 |
| --- | --- |
| `src/azmod/community.c` | `4b2b72e2b623dcb1ecb0ef108a473815adab45a516a6aa13adb6cce512b80866` |
| `src/azmod/sdk_ui.c` | `5ce1d9356a45fc27bfc3f8957f0cf7d85a0e7ff1496f2130aa93fc3f538d68df` |
| `src/azmod/sdk_logic.h` | `9f6fd2a2917a1d0b6184563b62758c01c05cb85de9d72b8b8b226158dc9e5c6a` |
| `src/azmod/azmod-mod.h` | `b94e2ba6656c2b852f18691d32225486f29265953c8bf09dcac9a18ce561a630` |
| `src/common/azaltstack.h` | `f929777434f77e31a91055588e897158d6bfd8c6adc993dde0eeeca04eccbd4c` |

## SDK validation and start ordering

The real loader supplies `&g_cm_core`, a static service table, to `init`
(`community.c:1849`). That establishes the ordinary SDK pointer contract.
Checking a non-null pointer, ABI, advertised size and required service pointers
is still necessary for this adapter; it cannot validate an arbitrary unmapped
address merely by reading its `size` member.

`AZ_HAS` casts `core->size` to `size_t` (`azmod-mod.h:439`). A negative signed
size must be rejected before relying on that macro. Check the size through the
last field the adapter actually uses before reading that field, and require
all used function pointers. A null firmware-tag result must be handled before
string comparison. Firmware/opting-in checks and static entry binding precede
worker creation and claims.

`sd_thread_start` (`sdk_ui.c:296`) accepts the loading module as well as a
LOADED module. It checks caller ownership, callback code range, bus-callback
context and a four-running-thread limit. It reserves a thread count, allocates
startup data and calls `az_thread_create` with detached-thread attributes.
Failure releases the count and startup data; success returns 0. The underlying
new thread can execute **before** `thread_start` returns. Success is not an
acknowledgement that the worker reached a particular point in its loop.

The thread wrapper (`sdk_ui.c:284`) copies/frees its startup data, calls the
adapter's worker and decrements the count only when it returns. It does not
recheck the module state before invoking the worker. There is no core stop or
join handle. The SDK requests SCHED_OTHER and an alternate signal stack;
attribute return values are not checked in this path, and `azalt_entry`
(`azaltstack.h:89`) deliberately continues if alternate-stack setup fails.
A mocked successful `thread_start` does not validate either property at runtime.

Creating the worker before the first permanent hook avoids an avoidable
worker-creation failure after patching code. It also requires the worker to
understand an initializing state: it may run before either claim, between the
two claims, or before the final ready publication. Do not spend an active
capture budget merely because the worker was scheduled during initialization.
If an independent close can occur during initialization, check `Transport.start()`
and do not publish READY after it failed. A state transition must never reopen
a closed transport.

The callback may enter as soon as a claim redirects its entry, before the claim
service returns. Each wrapper must use the permanent original slot given to
that claim directly, and forward whether capture is configured, failed,
closed, full or rejected. Keep the transport/configuration safely published
but capture closed while claims are incomplete. A refusal of the second claim
retains the first forwarding target; no retry, reconstruction or cleanup may
invalidate it.

## Failed init, tick and late returns

After an init failure, `community.c:1852` clears the module descriptor/screen
and records failure. Successfully opened libraries stay mapped
(`community.c:1803`), and existing hooks stay installed. The core therefore
stops its normal callbacks but does not stop an already created worker or
native calls into an installed hook.

`community_tick` calls a module's tick only when the module is LOADED and has
its descriptor (`community.c:2129`). Ticks run on the UI thread at a variable
rate, with a 33 ms minimum separation in this implementation. Failed-init
worker shutdown must use explicit adapter failure state; it cannot depend on
receiving a later heartbeat. A worker that starts before init completes must
not mistake the expected lack of ticks during init for evidence of native
failure or core quarantine.

A heartbeat that does not change for several worker iterations is an observer
condition, not proof of core quarantine, a stopped UI thread, or elapsed time.
Sleeping a nominal interval does not turn iteration counts into a hard time
bound, particularly if a worker log call blocks on filesystem I/O. These local
conditions may close capture conservatively while leaving forwarding intact.

Closing is logical only. A before helper that already passed the open gate
can publish later; a reserved submission can return after close. Its after
helper may use only the private pre-call context and result, never native task
storage. The transport and original slots remain valid for the process
lifetime. A worker may keep scanning copied values after close, or exit with
explicitly incomplete output; neither choice proves all producers have ended.
Do not reuse slots or reclaim storage based on quiet counters, finite scan
counts, thread return, or a final diagnostic line.

## Logging is an attempted output, not an acknowledgement

`log` returns `void` (`azmod-mod.h:214`). `cm_log` verifies caller ownership,
then `cm_vlog` (`community.c:1177`) silently returns for disabled logging or the
per-file cap; formatting, open and write failures can also lose a record.
The SDK supplies no result indicating durable delivery, successful enqueue or
bytes written. Name adapter counters accordingly: attempted/emitted-to-service
is distinct from logged, flushed or delivered.

The special eight-slot log queue applies only when the calling thread's
`g_cm_in_frame` TLS flag is set. A separate adapter worker does not inherit
that flag. Its log calls ordinarily format and append directly, including
filesystem calls; this belongs on the worker, never the hook helper or tick.
A full on-frame queue is another SDK drop path, not the expected transport for
this worker. The adapter should not manufacture a log-return/error API in its
mock or claim to test an enqueue acknowledgement that does not exist.

With 16 immutable transport slots, entry and return attempts can each be
bounded by one per slot, plus a separately bounded set of diagnostics. Marking
a record seen means the worker attempted the void service call; it cannot
establish success. A no-op/suppressed logger should neither cause retries nor
unbounded output. Keep scan/record order distinct from native order, and avoid
calling a terminal summary a complete capture if a producer or return can
still publish later.

## Minimal adapter-specific test schedules

These are review requirements, **not test results in this note**. The owning
source/test evidence must state which were actually exercised.

- Null, wrong ABI, negative/truncated size, missing used service pointers, null
  firmware tag and opt-in mismatch must reject before any unsafe field access,
  native binding read, worker start or hook claim as applicable.
- Schedule a worker step before `thread_start` returns, while each claim is in
  progress and after init failure. Keep capture closed and avoid relying on a
  future tick; simulate callbacks during claims while preserving forwarding.
- Reject worker creation before claims. Refuse the first or second claim;
  preserve any successful original and permanent storage. Repeated init must
  not create a second worker, reconstruct storage or re-claim an address.
- Exercise no heartbeat during pending init separately from a quiet heartbeat
  during READY; close, a delayed before publication and a late normal return
  must retain the transport's documented behavior.
- Force a void logger to suppress every attempted line. Verify finite attempts,
  no implicit retry/delivery claim, per-slot output identity and continued
  native forwarding. No logger call is permitted from a hook helper or tick.
- Delay publication of an earlier slot while a later one is READY. The worker
  must revisit the earlier slot, acquire every payload it copies, and use
  entry/return seen states independently. Counters are not readiness or a
  coherent end-of-run snapshot.

Actual adapter-source tests with injected SDK/environment/binding services
can establish these schedules without touching native firmware. They do not
prove the SDK's real thread scheduling, log persistence, installed trampoline
execution, native task borrowing, hardware latency or whole-core races. This
review ran no new runtime operation or unchanged test suite.

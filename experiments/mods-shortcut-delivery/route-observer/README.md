# Passive encoder route observer — source candidate

This unpublished experiment observes the separately verified native encoder route
at `0x1b01cf8` and one RAW mixer-input callback. It adds diagnostics for the silent
state/live gates that limited the previous dispatcher experiment. It does not
open a menu, consume input, navigate, activate stems or establish native input
completion. **One emulator claim succeeded and one native off-thread sample was
retained during a four-detent run.** All 147 baseline files and the original host
counter were restored; the emulator is released. Read [runtime evidence](runtime-evidence.md)
for the bounded result and remaining gates. The earlier refused and negative-result
observers remain unchanged.

The unique module is `encoder-route-observer` version `0.0.1`, enabled only by
`AZ_ENCODER_ROUTE_OBSERVER_EMULATOR=1`, with log prefix `ERO`. Its initialization
checks the exact firmware tag, both immutable observer slots `0x2d453e8` and
`0x2d45758`, and all four opening instructions before requesting one exclusive
function-entry hook. See [the binding checks](binding-notes.md) and
[the native route evidence](../input-route/README.md). Conditional static
eligibility alone does not establish a successful live claim. The separate runtime
result establishes one claim and a bounded entry sample, not a registration
recipient, active child view, accepted task or completion.

## Two independent observation lanes

The native helper copies `x0`, `x1`, full-width `x2`, `w3`, `w4` and original LR.
Addresses remain opaque: it never reads native argument objects, calls through
captured values or reads them after return. Its ABI source is byte-identical to
the reviewed [five-input tail wrapper](../dispatcher-observer/abi/README.md),
SHA-256 `a1f1cf42cc587c3222d299ac9dd31bfd0944f1ea51ec98a19d74ba40088a5034`.
Hidden `az_dispatch_*` names are retained inside this separately loaded module.
The wrapper preserves native input/return classes and tail-forwards; no guessed
native return type or return-side callback is introduced. The existing original
pointer publication and mapped-module lifetime requirements still apply. The
unchanged prior 55-assertion ARM64 ABI fixture was not repeated as new evidence.

Before accessing errno or TID/TLS, the helper increments `native_attempted`, then
checks `state` and `live`, incrementing `native_state_gated` or
`native_live_gated` on the respective exit. State is checked first. These count
helper entry, not execution of the wrapper before its helper call. Enabled
capture preserves errno around observation. A failed signed `gettid` result is
not cached and can be retried. Linux TID equal to PID selects a 31-record-capacity
SPSC ring with a nonblocking producer/reentry guard. Other threads compete for
one immutable mailbox; its sole winner publishes READY with release ordering,
and the worker acquires it before copying. It is consumed once and never reused.
An off-thread record has sequence zero and no main-thread ordering meaning.

Exactly one SDK filter is registered, with
`AZ_FILTER_RAW | (1u << AZ_BUS_MIXER_IN)`. There is no normal callback. The RAW
callback returns zero and never modifies the supplied frame. It checks the bus,
non-null buffer and length of at least 36 before copying bytes 6 and 31–35. The
SDK's checked-input contract supplies CRC eligibility; this callback does not
validate CRC itself. Host tests invoke it directly, including short/invalid
arguments, without claiming those frames passed native CRC checks.

The first eligible RAW frame establishes a value baseline. Later frames produce
records only when one of those six values changes. A separate guarded producer,
ring and sequence isolate this lane from native capture. Sequence advances and
the baseline updates even if a changed record is dropped because the ring is
full; a repeated identical value does not repeatedly fill the ring. Counter wrap
in byte 34 is copied as a value, with no invented signed delta. `raw_heartbeat`
counts valid callback entries after state/live gating and before producer
ownership, including entries dropped by the reentry guard. It is separate from
the SDK tick `heartbeat` and is not proof that a record was queued.

RAW records are before core edits. Core menu/focus anchoring and later filters
can change what native firmware receives, as shown by the
[actual SDK chain fixture](../input-stage/README.md). RAW callback eligibility
still depends on the core's loaded state and unchanged quarantine watchdog.
Missing RAW or native records remain inconclusive without their respective gate,
liveness and loss evidence. Matching counts, values, or ordering across the two
lanes do not establish causal/native receipt or final delivery. There is no
global event sequence or timestamp correlation.

## Lifecycle and limits

Initialization registers RAW before starting the worker, and claims the hook as
its final fallible service operation. Local state becomes ready only after the
claim returns a non-null original. Registration, thread or claim failure leaves
local state inactive; a registered callback cannot capture on direct invocation.
The existing core also skips filters belonging to a failed/non-loaded module.
Tests mock these service calls and exercise their order, early callback delivery
and local failure gates; they do not simulate a live SDK installer. The pinned
SDK chain fixture separately tests its loaded-state gate.

The worker retains the existing 500 ms heartbeat watchdog: five unchanged tick
samples disable capture, and a later tick can re-enable it while the worker is
running. No core watchdog threshold, strike count or off flag is changed.
Logging is worker-only: at most 128 native records, 128 RAW records, eight changed
error/gate summaries, and one budget-stop message, plus the initialization
message. Encountering either event budget's next record stops the worker and
disables both lanes. Native forwarding continues. This bound can truncate one
lane before its own budget is reached. A log write is not guaranteed delivery.

All diagnostic counters/state atomics are always-lock-free 32-bit values; they
can wrap. `sequence` and `raw_sequence` are producer-owned 64-bit values. Reads
of multiple diagnostics are individual, noncoherent observations, not a capture
snapshot or historical proof of gating at an earlier input. Producer guards
protect capture state, not arbitrary native lifetime or signal-handler semantics.
The code performs no explicit allocation, logging, clock call or native-object
read in either hot path. Host tests do not instrument allocation, real first-use
TLS resolution or real `gettid` latency. Lock-free operations are not a measured
wait-free or physical-time bound. The compiled helper's early counters/gates
precede errno/TLS calls and has no dynamic atomic imports; its first-use cached
TID TLS resolution still relies on compiler/runtime TLS semantics. Hardware
filter timing and the cost of the observer itself remain unvalidated.

## Host and build evidence

[The runner](tests/run-tests.py) compiles the actual candidate `observer.cpp`
with SDK service mocks. All **31,427 assertions across 31 scenarios pass per
plain, ASan/UBSan and TSan mode**. The two strict negative controls each fail
exactly one expected assertion per mode: placing the attempted counter after
state gating loses the initialization-time trace, and writing RAW byte 34 fails
the full-buffer preservation check. The runner rejects sanitizer diagnostic text
for candidates and expected-failure controls and enforces every scenario's exact
check count. [Results](host-result.json) bind source/test/runner/header hashes;
unique ignored snapshots retain the compiled sources, controls and logs.

Coverage includes failed initialization and callback ordering, native gate
counters before test TID/errno clobbers, high-bit native values/LR, mailbox
contention and partial publication, native reentry, RAW gate/length checks,
first/unchanged/changed values, all six captured bytes, overflow/gaps, byte wrap,
full-buffer preservation, concurrent RAW producers with one paused owner,
independent native capture while RAW is paused, and concurrent transfer through
both lanes. Existing native tests remain. Worker execution is stepped in the
fixture; actual sleeping, real firmware dispatch and SDK quarantine integration
are outside this suite. TSan covers the exercised observer schedules, not the
whole core or native engine.

The first development invocation failed compilation because a test-only prefix
replacement changed `EDOM` to `EROM`. It was corrected before any assertions ran.
The first strict-control runner then expected the later gate assertion, while
the deliberately broken counter correctly failed the earlier initialization
assertion. Only that expected failure location was corrected; candidate source
and assertions were unchanged. Both failures remain in ignored development
receipts. They are harness mistakes, not suppressed candidate failures.

The exact source builds and passes SDK validation as an ARM64 shared object;
[build evidence](build-result.json) records its fingerprint. No package is
published, and the local binary is ignored. The pre-install build/review receipts retain their stage-specific no-runtime fields;
the later runtime result above is separate. One live scalar sample and its
original LR/code return site were retained; broader provenance, counter ordering,
return semantics, queued-event ownership, focus
handback, view cancellation, existing quarantine recovery and physical timing
remain unresolved.

Reproduce from this directory:

```sh
sh tests/run-focused.sh --result /absolute/private/path/host-result.json
python3 /absolute/path/to/az-mods-sdk/tools/azmod-pack build .
```

The default SDK path selects the existing sibling checkout. If overriding
`AZ_SDK`, use an absolute path: the shell wrapper first changes to the module
directory. These commands perform host/build checks only.

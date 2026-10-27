# Offline mixer broker and producer gate

This candidate adds two reusable libraries for the separate, uninstalled
[instance/generation kernel candidate](../emulator-overlay-handback/). It makes
no changes to the actual emulator preview, forwarder, driver scripts, runtime
runner, kernel patches or installed packages. No device or emulator is contacted.

The C broker serializes one immutable backend context and one producer grant.
It checks complete adoption state, maps contiguous producer request IDs to
kernel sequences, retains the last exact receipt, and closes admission on
uncertainty. Exact duplicates return the cached outcome without any backend
call. Unknown requests block rotation and adoption; only an exact positive
state can reconcile them, under the retained-file/sole-mutator assumptions.
Reconciliation remains closed until an explicit generation rotation and fresh
adoption. See [the contract](BROKER-CONTRACT.md).

The Rust producer captures its grant and request ID when input is created,
keeps a bounded canonical queue, and admits only its front entry with at most
one unresolved request. Closure invalidates unsent/cached work while retaining
that unresolved request. Socket success is not an acknowledgment. Baseline
adoption requires a complete matching receipt and a caller-supplied release
barrier; a new epoch is required after closure. See [integration and limits](PRODUCER-INTEGRATION.md).

## Evidence

- The broker executes against seven exact extracted V2 kernel functions: **203
  assertions** in each plain, ASan/UBSan and TSan mode, plus **six strict failing
  controls per mode**. This uses pthread/kernel-service shims and owned memory,
  not a loaded kernel. [Host evidence](HOST-TESTS.md).
- The actual standalone Rust gate passes **19 named tests and eight strict
  failing controls** with local `rustc`. Tests use synthetic frames, threads,
  mpsc queues and settings callbacks. No sanitizer claim is made for Rust.
- Both final test results were independently reproduced. See [review](REVIEW.md)
  and machine-readable [C](host-result.json) / [Rust](producer-result.json)
  results for exact source hashes and scope.
- The C library compiles to an ARM64 relocatable object under strict warnings.
  This is not a linked or executed guest program. [Build evidence](ARM64-BUILD.md).

## Remaining work

There is no C/Rust wire/FFI adapter, socket protocol, broker daemon, durable
receipt storage or actual preview integration. C requests are caller-mutable
values; Rust envelopes have private fields but can be recreated from old raw
bytes under a fresh grant. The libraries cannot prove fresh gestures. The
settings callback lock rejects an old generation, but does not preserve
creation order among callbacks in the same generation. Both accepted limits
have executable counterexamples.

Next compose an adapter with the pinned preview creation, queue/cache,
reconnect and host-settings paths. Preserve every other bus and forwarder
output. Prove old UI/saved-state caches are invalidated, settings updates have
explicit event/ACK ordering, baseline equality does not suppress adoption, and
uncertain IPC does not retry or erase a pending native operation. New broker or
producer objects must not be treated as proof that older operations are gone.
The current code trusts backend outcomes, receipt authority and caller release
observations; the eventual adapter must establish those facts.

Then implement the separately reviewed [owned stopped-recovery
transaction](../emulator-overlay-handback/RECOVERY-CONTRACT.md), including complete
original neutral overlay, injected mode and exact host bytes before guarded
restart. Native input already sampled cannot be retracted by a generation
change. Both kernel candidates and the observer remain uninstalled; live V2
runner dispatch remains disabled. This work does not advance MODS publication,
stems activation or USB native ownership gates.

## Reproduce offline

From the repository root, with the pinned emulator checkout beside it:

```sh
python3 experiments/emulator-mixer-broker/run-tests.py --emu ../xdj-az-emu
python3 experiments/emulator-mixer-broker/run-producer-tests.py
```

Runners retain immutable inputs, compilations, outputs and failing controls
under ignored `private/` directories. Do not upload those raw artifacts or
compiled objects. Public JSON contains sanitized provenance and result hashes.

# Independent offline integration review

No blocking correctness issue was found within this **inactive, injected-backend
scope**. The new candidate closes several concrete paths that could otherwise
reauthor old mixer state: creation-time generation capture, full baseline/model
adoption, a post-adoption release barrier, typed worker routing, and exact ACK
processing with ordered settings callbacks. The original emulator checkout and
older experiments remain unchanged. No live backend, settings-file adapter,
forwarder connection, kernel installation or observer run was performed.

This review follows [REVIEWPLAN.md](REVIEWPLAN.md). It covers the actual patched
App, controls worker classifier, main wiring and the copied producer/route
modules, plus the preparation and targeted test runners. The reviewed patch is
`264d3d26cdc6a3534fb7df299220b02d4043b1190416b1b0d6a578f68247a84e`,
exported from emulator commit `06fc981707e69982c0554bb508e0c646c005a979`.
The [candidate inventory](candidate-pins.json) binds the complete 16-file source
and lockfile export; tests do not build from the original working tree.

## Source findings

`Links.mixer=None` preserves the original legacy preview behavior, including
legacy mixer traffic and its existing settings path. `Some(route)` selects owned
mode even while the route is closed. Main constructs only `None`; no enable flag
or live backend factory was added.

In owned mode the App captures a key before processing an update. It submits that
same key with the authored frame rather than refreshing authority at send time.
A changed generation, queue-full error or failed channel enqueue closes admission
without advancing the App's enqueued-frame cache. The cache is explicitly not an
acknowledged native state.

Adoption copies all 152 baseline bytes and separately decodes the analog models,
Gray selector and independent little-endian FX TIME pair. It resets mixer drag
residues, pending/held contributions and X-pad state while preserving ERP, SHIFT
and deck state. Adoption and release confirmation each consume a non-admitting
update. Release/click events cannot open admission in their own update; the
confirmation requires a later focused, event-free, released input state. The
check runs before handlers in both panel and screen-only modes. The tested
headless event sequence is a local egui property, not a hardware release proof.

The controls worker classifies values before disconnected cache admission and
before connected legacy sending. Owned envelopes never become legacy frames;
untagged bus 2 is rejected while a route exists. Other-bus values keep their exact
legacy wire/cache representation. The LED reader code remains separate and
unchanged. No owned-mode shared counter-file load/store or legacy socket-success
settings path is selected by the App/worker integration.

The endpoint mutex serializes backend submission and receipt processing. The
producer mutex binds the complete positive ACK and its settings callback before
another request can be admitted. The callback receives the retained request's
frame and identity, not current UI state. A successful exhausting ACK runs its
admitted callback once before closing. A positive ACK after prior closure records
the known native result without running a late settings callback or reopening.
Callback error records a known commit, closes admission and does not make the
request eligible for replay. Backend panic closes and retains the unresolved
operation; callback panic poisons the producer and endpoint rather than silently
reopening them. Reentry into these locks is explicitly outside the callback
contract.

## Independent reproduction

The independent reviewer ran the frozen runner once in a fresh private export:

```sh
python3 experiments/emulator-preview-gate/run-tests.py \
  --emu-source ../xdj-az-emu \
  --result experiments/emulator-preview-gate/test-result-independent.json
```

The production configuration compiled with locked, offline Cargo. The actual
crate listed exactly 95 tests; only the reviewed 37 were executed: 11 new App
checks, two retained App checks, four worker-classifier checks, 12 route checks
and eight new producer checks. All 37 passed. The remaining 58 cases, including
boot supervisor tests and unchanged standalone producer tests, were listed but
not executed by this run.

All 12 source controls compiled and failed their one specified test with the
required assertion, exact test inventory/counts and exit 101. They cover stale
key retagging, release-update admission, omitted FX TIME model hydration, ERP
state erasure, owned/untagged mixer legacy fallback, mismatched ACK acceptance,
known-commit retention as pending, malformed rejections, unknown admission and
refresh over a staged baseline. A compile failure, timeout, unexpected failure
name or runtime diagnostic could not count as a passing control.

The author and independent results agree in every functional field: all source
and input fingerprints, compiler versions, full inventory, run names, compile
and test exits, pass/fail names and counts. The compiler was Homebrew
`rustc 1.98.1 (48a229cea 2026-09-01)`, host `aarch64-apple-darwin`, LLVM 22.1.8;
Cargo was `1.98.1 (797e8a9bc 2026-08-05)`.

| Retained artifact | SHA-256 |
| --- | --- |
| Author result | `33dd541807010d4129a1c4b62359b84b1b7c5806b6876de4b767580ad77c5465` |
| [Independent result](test-result-independent.json) | `e16f5e43364b6ce7fe650096705300ff81a1513bdf3b48f5a9a2bf5fd887ae01` |
| Private functional-comparison receipt | `5bf06f520cd32f6c0be3e12dd5350a13768d2cf5a1a594b616c54d12d4430254` |

Only `production_check_log_sha256` and each run's compile/test output hashes were
excluded from the functional comparison. Twenty-six of those raw hashes differ.
Logs contain private export paths, elapsed times and runtime thread identifiers;
this review does not claim whole-JSON or raw-log text equality. Both original
result sets and complete run inputs/logs/binaries remain retained. The independent
run had no failure, source change, retry or workaround. No sanitizer or target
ARM64 deployment result follows from this host Cargo run.

## Practical limits retained

- Status and staged-baseline observations use nonblocking lock attempts. Creation
  and logical close can still wait for a settings callback; the whole UI is not
  claimed to be nonblocking. Temporary status unavailability is not proof that
  native work stopped.
- Backend dispatch and settings callbacks execute synchronously in the shared
  controls worker. They can delay bus 0/1/3 sending and cache draining even
  though those bytes and routing are preserved. The separate LED reader does
  not establish independent latency for outbound control traffic.
- The canonical owned queue is bounded, but main's existing mpsc channel remains
  unbounded. Repeated generation changes could leave old envelopes in that
  external queue. No global queue-size bound is claimed.
- A malformed positive ACK closes admission and retains the exact unresolved
  envelope, but its diagnostic `uncertain` flag remains false. That flag cannot
  be interpreted as proof of non-commit. Reopening, refresh and retry remain
  blocked by the unresolved operation regardless of the flag.
- Baseline authenticity, ownership and neutral-frame policy are responsibilities
  of a future trusted adapter. Public receipt values alone cannot establish
  those facts. Unknown operation reconciliation, real broker transport/FFI,
  durable process recovery and a broker death protocol are not implemented here.
- The settings sink is injected and synthetic in these tests. `config.rs` is
  unchanged. Ordered callbacks do not prove real file ownership, durability,
  two-file atomicity, or rollback after a partial side effect. Native minimum
  button dwell also remains unproven: the UI's creation-time hold may be shorter
  between commits after queue delay.
- Only actual changed methods and selected headless interactions were executed.
  The real socket worker loop, application startup/shutdown, window system,
  forwarder, kernel ioctl path and native event consumption were not exercised.
  Source separation is not a completed safe handback or runtime authorization.

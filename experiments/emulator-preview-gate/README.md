# Inactive preview mixer gate

This separate patch composes the creation-time producer with the **actual pinned
az-preview App and control-message classifier**. It closes the local stale-key,
legacy-cache and settings-order bypasses exercised below. It supplies an injected
in-process trait boundary, not a broker process, wire protocol, filesystem writer
or live backend. `main` supplies `None`, preserving the existing legacy preview.
No enable flag or backend factory exists. The runtime runner remains CLOSED.

The base is emulator commit `06fc981707e69982c0554bb508e0c646c005a979`.
[Base pins](base-source-pins.json) cover all 13 tracked preview files;
[candidate pins](candidate-pins.json) cover the complete 16-file result, patch
and dependency lock. The original checkout and older experiments are unchanged.
The [frozen broker/producer libraries](../emulator-mixer-broker/) remain a separate
checkpoint; this patch copies and revises the producer rather than editing them.

## Behavior

| Boundary | This candidate |
| --- | --- |
| Mode selection | `None` selects existing legacy behavior. `Some(route)` selects owned mixer behavior even when the route is closed; errors cannot fall back to legacy bus 2. |
| Input creation | App captures one key before UI mutation. The producer checks that exact key under its lock while assigning the request ID and immutable full-frame envelope. Send/dequeue cannot refresh the key. |
| Baseline adoption | App copies all 152 bytes, decodes every analog UI model, Gray-coded FX SELECT and the independent little-endian FX TIME pair. It resets mixer drag residues, X-pad and mixer held/pending state while preserving ERP, deck and SHIFT state. |
| Rearming | Adoption consumes one update. A later focused, event-free update with pointer, touches, keys and modifiers released confirms adoption and admits no mixer input itself. Only a subsequent update may capture an input key. This check runs before handlers, including screen-only mode. |
| Queue/cache | The owned envelope and legacy frame have distinct types. The actual worker classifier runs before both connected and disconnected legacy cache paths. Owned envelopes never enter that cache or its reconnect replay; untagged bus 2 is refused in owned mode. |
| Existing buses | Bus 0/1/3 byte serialization and latest-value replay remain selected; the LED reader is unchanged. This is routing/byte preservation, not latency isolation. |
| Host counter | Owned App input avoids the old optimistic knob-file store and shared-file counter adoption. `None` keeps those legacy paths. No config implementation changes are included. |
| Positive ACK | The exact immutable request and next kernel sequence must match. Native commitment is recorded before invoking the synthetic settings sink, which receives that exact ACK. The gate lock orders the callback before later admission. Duplicate or mismatched ACKs cannot write settings. |
| Failure/close | Known rejection closes without settings. Unknown outcome retains the exact pending envelope and blocks adoption. A late exact positive ACK after closure records commitment without a settings write. Settings error closes while preserving the known native commit; panic poisons the gate. |
| UI queries | Status and staged-baseline queries use nonblocking lock attempts. Unavailable status is no input permission, not proof of closure. Creation and close remain serialized and can wait for an in-progress settings callback. |

`sent_mixer` is an enqueued-frame deduplication cache, never an ACK. Queue-full or
channel failure closes owned admission without advancing it. A closed gate does
not release a native button or restore a held overlay. No route replacement,
automatic resend, same-epoch rearm or unknown-result recovery is provided.

## Verification

[The primary result](test-result.json) records successful compilation of the
production cfg and **37 selected actual-crate tests: 35 new tests and two retained
App tests**. The full binary lists 95 tests; 58 unrelated/inherited tests were
deliberately filtered. Tests use headless egui contexts, actual changed methods,
in-memory channels and injected synthetic receipts/settings callbacks. They do
not execute application main, the window, socket worker, boot supervisor, host
settings files or emulator. The worker tests execute its actual classifier/cache
helpers, not a live reconnect loop.

All **12 strict source controls** compile and then fail exactly the named test's
functional assertion with exit 101. Compilation errors, unexpected panics,
timeouts, empty selections or changed inventories reject the run. Controls cover
stale-key refresh at send, admission during the release-only update, missing FX
TIME hydration, lost ERP held state, owned/untagged mixer cache bypasses, wrong
ACK settings, known-commit retention after settings failure, malformed rejection,
unknown closure and replacement of a staged baseline. The four App mutations
also have explicit [oracles](app-controls.json).

Compiler: Rust/Cargo 1.98.1 on `aarch64-apple-darwin`, with a locked offline Cargo
graph (including egui/eframe 0.29.1). This is a macOS host build, **not Linux ARM64
emulator/core validation**. Production compilation retains nine unused-code
warnings from the inactive seam; no warning policy was suppressed. No sanitizer,
audio, device or physical-timing claim follows from these tests.

[Independent results](test-result-independent.json), [comparison](comparison.json)
and [review](REVIEW.md) are retained separately. Compare inputs, compiled source
hashes, compiler identity, test names, counts and exits. Exact raw log hashes may
differ because paths, elapsed times and runtime panic-thread IDs differ; raw logs
are preserved privately and are not asserted byte-identical.

The first combined actual-crate build/test passed before the public harness was
created. Earlier focused checks were exploratory: 27 producer tests (including
19 inherited), then 11 and later 12 route tests. They are not added to the 37
actual-crate count. A fixture-only initial Rust compile failure was retained and
corrected (`unwrap_err` imposed an unnecessary Debug bound). An offline
all-platform `cargo metadata` attempt found an uncached Android dependency;
cached host builds succeeded without downloading it. Its tool-output observation
is historical, not a fabricated retained full log or candidate behavior failure.

## Reproduce offline

From flow-beat, with the existing local emulator Git checkout and cached host
dependencies:

```sh
python3 experiments/emulator-preview-gate/run-tests.py \
  --emu-source ../xdj-az-emu
```

The runner uses [prepare-candidate.py](prepare-candidate.py) to export pinned Git
blobs into a fresh ignored `private/prepare-<uuid>/` tree, applies only
[preview-gate.patch](preview-gate.patch), copies [Cargo.lock](Cargo.lock), and
checks the complete resulting inventory. Every variant has a separate source
snapshot. Cargo runs with `--locked --offline`; all build outputs and raw logs
stay private. The script never checks out, resets or edits the source repository
and never runs the preview application. Missing dependencies fail the run.

## Limits and next work

1. The injected backend is trusted to authenticate receipts, retain one owned
   broker session and supply a suitable full baseline. Neither a Rust value nor
   local frame equality proves ownership, neutral controls, native consumption
   or physical release. There is no C/Rust FFI, ioctl or IPC adapter.
2. `ctrl_thread` calls the owned backend synchronously. A delayed submit/settings
   callback can delay other bus sends and disconnected-cache draining; the LED
   reader remains a separate thread. App creation/close can also wait behind a
   callback. There is no deadline or latency bound.
3. The canonical owned queue is bounded per generation, but the existing mpsc
   channel is unbounded. Repeated closure/adoption can leave obsolete immutable
   envelopes in that external queue. They are rejected, not retagged, but this
   is not a memory/backlog bound or proof of quiescence.
4. Existing UI-time `MIN_HOLD` does not establish native dwell after delayed
   ACKs. Queued press/release can commit close together. Hidden-view release and
   headless event tests do not prove every OS touch/focus lifecycle. Kernel
   closure cannot cancel already sampled native input.
5. Only an in-memory settings sink is tested. The original counter and merged
   mixer-state files remain unchanged. No real ordered/atomic writer, partial
   file recovery, durability, mode preservation or cross-kernel/filesystem
   transaction is implemented. Do not normalize saved settings to fit a policy.
6. Malformed positive ACKs retain unresolved work with admission closed, though
   the diagnostic `uncertain` flag remains false on that path. The route has no
   reconciliation/query API; UNKNOWN stays blocked. This is fail-closed
   accounting, not a completed recovery implementation.

Next design the **bounded transport/lifecycle adapter** and delayed native-input
semantics before supplying a live backend. Then implement the explicit ordered
host writer and [owned stopped recovery](../emulator-overlay-handback/RECOVERY-CONTRACT.md):
original neutral full overlay, injected/idle mode and exact host bytes must be
verified while the owned native process is stopped, before guarded restart.
Do not bypass that gate by calling the current preview main or old runtime
runner. No old successful capture or unchanged kernel suite needs repetition.

No emulator/device endpoint, service, physical deck, package, catalog or core was
changed for this checkpoint. Native task provenance/ownership, MODS 0.1.3
publication, stems staging/activation, USB activation and hardware validation
remain separate open work. This patch is not a community release.

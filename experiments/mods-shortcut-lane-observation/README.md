# MODS 0.1.3 lane observations do not identify quarantine

The unchanged 0.1.3 candidate can log **“lane … switched off by the core”** and replace its active lane while every SDK registration remains loaded, has zero strikes and has `off=0`. This is a deterministic host counterexample to the diagnostic inference. It is not a physical or emulator reproduction, a callback-overrun measurement, or an explanation of the earlier accepted-then-cancelled tap.

The earlier four-lane fault-injection experiment proved response to deliberately stopped delivery. It did not prove the converse: missing increments do not establish that the core quarantined a lane. The SDK exposes no quarantine query. No inevitable-overrun or multiplied-lifetime guarantee follows from either experiment.

## Exact source and schedule

The runner reads `mods-shortcut/shortcut.h` from development commit `321b955b8eefb5470f0e0e636e3bb08ebc520c56` and the actual `cm_frame_filter`, `sdk_filter_run` and `sdk_strike` bodies/declarations from upstream `3e0c5cb010454228f4ee63226b60d060494eeaae`. The complete pinned SDK header is compiled with them. The real mod initializer calls the extracted registration function; the fixture does not substitute a lane dispatcher. Production bodies are unchanged. Source, extraction, shim, runner and fixture hashes are in [validation.json](validation.json).

The test supplies a stub owner lookup, logging/view services and monotonic fake SDK ticks. Its serial script places UI watches at the start of the next filter's timing function, before that filter's start timestamp is returned. The previous filter's end timestamp has already returned. Thus the modeled delay is **between measured callbacks**, not charged to the preceding or following callback. No wall-clock wait or background thread is used. There is no test request/pulse in this schedule.

The fixture invokes the UI watch serially while its single shim `g_cm_in_frame` flag is set. Production uses thread-local state and a separate UI thread. Stub logging verifies diagnostic call sites/counts only, not core log transport, UI-thread eligibility or concurrent execution. The fixture does not use that flag to decide lane state.

One SDK dispatch delivers one callback to each lane:

| Script point | UI time (ms) | Observed beat counts, lanes 1–4 | Result |
|---|---:|---|---|
| After lane 1, before lane 2 starts | 1000 | 1,0,0,0 | Lanes 2–4 each accumulate one stalled look |
| After lane 2, before lane 3 starts | 1250 | 1,1,0,0 | Lanes 3 and 4 are labeled switched off |
| After the dispatch finishes | 1500 | 1,1,1,1 | Lanes 3 and 4 clear; lane 1 is labeled off and lane 2 becomes active |
| After one further complete dispatch | 1750 | 2,2,2,2 | Lane 1 clears without restart; lane 2 remains active |

All SDK off flags and strikes stay zero, every registration stays loaded, and all input bytes remain unchanged. Three core-off diagnostics and one lane move occur. The schedule requires large gaps relative to normal frame cadence. Its frequency on hardware is unknown. It demonstrates the missing scheduling premise in the two-look inference; it does not establish that such delays occurred in an incident.

## Checks and controls

Four scenarios pass **82 assertions per plain, ASan/UBSan and TSan build**:

- Three complete frames with watches only after each dispatch: no off diagnosis or lane move.
- The split-frame schedule above: false diagnostics and a move, then resumed delivery without restart.
- No dispatch for two UI seconds: the local all-off state disables the shortcut even though the core registrations are healthy; one later frame clears it.
- Eight synthetic over-budget first-lane callbacks through the actual SDK timing/strike path: the core really quarantines that lane, and two later missed looks correctly move to lane 2. The test never writes an off flag to simulate this transition.

An additional **authoritative-hypothesis control** asserts that local off necessarily means core quarantine. It fails exactly one named assertion out of 83 in each mode: `split-frame/local-off-implies-core-quarantine`. This is an intentionally false hypothesis, not a mutated fix or a sanitizer failure. There are no sanitizer diagnostics. TSan here checks serial execution only; this is not a concurrency proof.

Run from the repository root, with the pinned upstream checkout available:

```sh
python3 experiments/mods-shortcut-lane-observation/run-tests.py \
  --upstream /path/to/xdj-az-mods \
  --result experiments/mods-shortcut-lane-observation/validation.json
```

Each invocation retains immutable source/build/output snapshots in ignored `out-history/<uuid>/`. This experiment has no firmware, network, device, or deployment dependency.

## Development consequence

Keep 0.1.3 unpublished pending a revised delivery/ownership contract and appropriate new checks. Report callback delivery as an observation, not confirmed core quarantine or “off until restart.” Before another multi-lane candidate, prove that UI-driven lane handover cannot process one input frame twice or cancel a pulse using its own edit; this fixture does not test or prove either failure. Adding spare registrations also requires review against the purpose of the core's cumulative watchdog. Do not reset quarantine or weaken protection to make it pass.

No mod executable, archive, catalog, core or runtime state was changed. Published 0.1.2 and its physical limits remain as documented. Native menu ownership and stems activation gates remain open.

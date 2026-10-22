# Independent counterexample review

An independent host invocation reproduced **82 passing assertions in each of plain, ASan/UBSan and TSan**. The authoritative-hypothesis build produced exactly **one intended failure out of 83 assertions per mode**, named `split-frame/local-off-implies-core-quarantine`, and no sanitizer diagnostics. Its result is byte-identical to `validation.json`.

The reviewer separately compared pinned source bytes and exact function boundaries, without importing the runner: 25 checks passed. The complete shortcut/header snapshots, three SDK function bodies, declarations and assembled extraction match the pinned revisions. The checked-out shortcut still matches its pin. Runner and test fingerprints were unchanged before and after the run.

| Item | SHA-256 |
|---|---|
| Independent result / validation | `8acb79c684197a987151b9c63f55b4cac17c91fcd5d5ad8f325704fc8d74488f` |
| Extracted SDK declarations/functions | `1db81fd23ed197526886abc95b93241e33a2b2311781a709f3cf8bc691e20b11` |
| Pinned shortcut | `b57da1ca86261302589d6ce7b8da56434201de659e49a0c5822151080b1ea06b` |
| Runner | `3825c358cea948725d1e16b2aec7ba7f7b375501d17e1469a11d3a5e3d31a3f9` |
| Test | `dc288ee7a7390162317871c3fe729565ed22d511ab278e2b1d1da6cd11b63fc8` |

The split schedule is a valid counterexample to an unconditional inference from missed beat increments to SDK quarantine. Its two modeled delays precede the following callbacks' start samples and follow the previous callbacks' end samples; they therefore need not produce measured callback overruns. The actual registration, dispatch, strike and watch bodies produce three false diagnostic invocations, one lane move and later delivery without restart while every SDK registration remains healthy. The ordinary whole-frame and genuine eight-strike scenarios discriminate this result from a broken dispatcher or disabled watchdog.

This is a **serial scripted interleaving**, not a measurement that this scheduling pattern occurs on a deck. The watch runs inside the mock timing service while one shim `g_cm_in_frame` flag is set. Production uses thread-local state and a separate UI thread. The stub logging checks formatted diagnostic call sites/counts, not actual log transport or UI-thread eligibility. That flag does not control the tested watch state. The fake SDK ticks and explicit UI times establish the intended ordering; they do not measure elapsed wall time, scheduling frequency or callback cost. TSan exercises only serial execution here.

The expected-failure variant adds a deliberately false hypothesis assertion; it is not a mutated implementation control. This experiment contains no pending request or pulse and provides no proof about same-frame double handling, pulse cancellation, native menu ownership, hardware incidents or general concurrent race freedom. The README preserves these limits. No correctness defect was found in the counterexample within that stated scope.

The ignored `out-history/independent-review-20261022T173021Z/` retains the invocation, input fingerprints, full console, result, status and independent source-comparison receipt. Compiler outputs and immutable fixture snapshots remain in the runner-created ignored archive. No production, test or runner source was changed by this review; no firmware, network or device access occurred.

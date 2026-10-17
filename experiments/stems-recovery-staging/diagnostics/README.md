# Focused ordinary-restart diagnostic

The original ASan assertion remains unexplained. These new runs add a discriminator between loader/join bookkeeping and ordinary progressive publication; they do not establish a cause or close the emulator installation gate. The frozen production patch remains `c8d8122ec0af65ebd478ebea1fedbb5b030c77ac186d2d9a5c7818ae01d6a03b` and the uninstrumented production extraction remains `c14d7c3504a92a08135219dcb8e5df81bf506e715307c924aefcda044888bef9`.

The original `completion_boundary(d, 1)` and its `consumed restart still launches ordinary load` assertion run unchanged. The diagnostic rotates across all four deck indices and stops at the first failed case. It also requires every independently registered worker to have started, returned, and successfully joined, and the loader to be idle at the assertion. This is stronger evidence than checking the fixture's single `loader_created` flag.

The test-only instrumentation records actual decoded-counter stores and loads, gate/mode/loading observations, entry and exit of `advance_stem_frames`, frame publication, and the post-join minimum used by `load_stems_ogg`. It does not substitute any atomic value, force a stale read, or change the production patch. Events live in thread-owned arrays and are read after joins. No clock, trace mutex, or global atomic event counter is added between the decoded-frame store and its following minimum scan. A separate registry mutex is used only for creation/join bookkeeping. Instrumentation still changes timing and code generation, so a passing instrumented run cannot rule out an uninstrumented failure.

## Retained runs

| Run | Outcome | Interpretation |
| --- | --- | --- |
| [001: focused restart](run-20261017-restart-trace-001/output.txt) | 1,000 cases; 15,000 assertions passed under ASan/UBSan | Every worker actually returned and joined in these cases. The first case retains its complete per-thread trace; later successful cases retain the aggregate result. No earlier failure reproduced. |
| [002: codec EOF control](run-20261017-codec-control-002/output.txt) | One case; 14 passed, 1 expected original-assertion failure | The ordinary codec deliberately returns no samples. All ten workers still start, return, and join. Ordinary decoded counters and post-join minimum are zero, and ordinary public frames are zero. This validates the diagnostic's failure capture, not the cause of the earlier unexplained run. |
| [003: default-path regression](run-20261017-default-path-003/output.txt) | Original plain suite: 528 passed, 0 failed | Adding the optional diagnostic path leaves the ordinary runner's uninstrumented production extraction unchanged. |

Each directory contains its immutable captured output, manifest, input hashes, and input source snapshot. The negative control intentionally leaves the original assertion failing and the process exits nonzero; it does not weaken or convert that assertion into a pass. Its traceback is the runner propagating the expected failed assertion, not an ASan memory error. Local paths in the captured output were sanitized.

The positive trace shows the early offset-triggered scan seeing zero counters, later decoder scans seeing actual partial progress, a decoder publishing 4,096 frames, and the joined loader returning with 4,096 public frames. The negative control shows no successful ordinary decoded-frame stores and a zero post-join result. If the unexplained assertion recurs with all decoded counters full but a zero post-join public result, the same trace format can distinguish that from a codec failure or an omitted/incomplete loader join. Such an all-full/zero-public result has **not** been captured here.

## Commands

From the repository root, use a new output/manifest name for each run and retain it without overwriting older evidence:

```sh
python3 experiments/stems-recovery-staging/run-tests.py --upstream /path/to/xdj-az-mods --restart-diagnostic 1000 --manifest /new/run/manifest.json
python3 experiments/stems-recovery-staging/run-tests.py --upstream /path/to/xdj-az-mods --restart-diagnostic 1 --diagnostic-empty-ordinary --manifest /another/new/run/manifest.json
```

The runner reads the same pinned source and applies patches only in a temporary tree. `--restart-diagnostic` uses ASan/UBSan and compiles an instrumented temporary harness generated from the existing test source plus `restart-trace.h` and `restart-main.c`. The normal test path remains available. No emulator, physical deck, or supplied checkout is modified.

## Trace interpretation

Events are ordered **within each thread only**. Thread numbers are registry identities, not a global event order or operating-system thread ID. `role=loader` records whether it was a private recovery or the ordinary replacement. A successful joined worker has `started=1 ended=1 joined=1 join_rc=0`. Final public frames are captured only for the ordinary loader: recovery may already have handed lifecycle ownership back before its wrapper returns. The wrapper's loading/mode/stale/hold fields are post-return observations; for recovery they can describe an already-started replacement, not the completed job. Use the per-thread events and independent joins for per-job evidence.

Event kinds: `1` atomic store, `2` atomic load, `3` advance entry, `4` advance exit, `5` publication of public frames, `6` joined load result. For atomic events, slots `0..3` are decoded counters, `-1` is gate hold, `-2` is loading, and `-3` is private recovery mode. For kind `6`, slot `1` identifies recovery and slot `0` ordinary. The numeric `value` is the actual observed/stored value. Non-loader final-state fields are unused zeros.

This diagnostic retains the host fixture's synthetic codec, synthetic memory values, and actual offset-completion tail with fixed correlation locals. It does not diagnose physical audio quality, test the complete native hooks, or prove absence of production races. The separate baseline ordinary TSan finding remains independently documented; neither that finding nor the successful trace runs explains the historical assertion without matching failure evidence.

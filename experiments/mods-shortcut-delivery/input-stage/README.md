# Encoder input observation limits

October 22, 2026. The previous dispatcher observer accepted its hook but recorded no entries. This follow-up tests which **SDK input stage** could distinguish a missing mixer input from later filtering. It does not identify the cause of that runtime result, install an observer, change input behavior, or repeat the emulator sequence.

The [runner](run-tests.py) extracts `sdk_filter_raw`, `sdk_filter_copy`, `sdk_filter_run` and `sdk_strike` unchanged from upstream `3e0c5cb010454228f4ee63226b60d060494eeaae`. The [fixture](test-chain.c) executes those functions, with mock timing/logging and a manually populated registration table. All **60 assertions pass in each plain, ASan/UBSan and TSan mode**. A strict negative control removes only the per-filter copy reset and produces exactly one named failure in each mode. An independent execution reproduced the complete matrix and byte-identical result JSON (SHA-256 `35ffdbbb722517c40b4de4ce91ad8343a221cd948fc32e2681517d38b8bfe2f9`). [Results](validation.json) bind the source, extraction, shim, test and runner hashes. Review also added exact check-count and mixer-bus constant assertions before the final runs.

## What the stages establish

| Observation | Proven scope in pinned source | Limit |
|---|---|---|
| RAW callback receives counter 52 | A checked incoming mixer frame reached this eligible callback, before core edits | No proof that native dispatch sees 52 |
| Earlier RAW callback edits its copy or returns EATEN | Later RAW callback still sees the original frame; source bytes are unchanged | A quarantined or non-loaded observer is still skipped |
| Normal callback receives counter 52 | The chain reached this callback after the core | A later callback can change it to 51 |
| No normal callback record | Earlier EATEN can stop the chain before the observer | Does not imply no incoming frame or no downstream frame |
| EATEN returned by a mixer filter | SDK returns 2 and stops later editing callbacks | The mixer caller only uses the nonzero result to stamp CRC; it does not drop the whole mixer frame |
| Callback exceeds budget eight times | Its cumulative strikes reach eight and future calls are skipped | No automatic recovery or reset is introduced |

The runner also checks the actual `bus_mixer_frame` caller's order: CRC eligibility, RAW dispatch, core knob tracking, optional counter anchoring, then normal SDK filters. The existing core can freeze byte 34 while its menu is open and focused, or an SDK surface holds focus, subject to the existing overlay watchdog. The previously tested [watchdog-order candidate](../../menu-watchdog-order/) remains separate and uninstalled. This new fixture executes the SDK functions; caller ordering is a source assertion, not execution of the complete core or proof of its installed binary.

Consequently, pairing RAW and normal callbacks is useful but **cannot by itself establish the final counter delivered to native firmware**. In particular, an earlier EATEN makes a last-registered normal observer blind. A future probe must state that boundary explicitly, retain callback eligibility/liveness and loss diagnostics, and correlate positive checked-frame observations with a distinct native route. Absence must remain UNKNOWN. Do not change filter order, bypass an existing owner, reset an off flag or weaken timing limits to obtain a sample.

## Host sender and guest boundary

Read-only inspection of emulator commit `06fc981707e69982c0554bb508e0c646c005a979` shows that `tools/az-drive.py` sends a bus tag plus 152 bytes, using tag 2 for the mixer. Its `knob()` updates byte 34 and the host counter file. The guest's `ctrl_write()` stores the bus overlay, while `spi_read()` copies it and stamps the mixer CRC over 96 bytes before returning data. These source relationships are consistent with the prior harness, which checked the SSH write status. They do not prove that a particular historical payload reached an EP147 read, survived later filtering or caused encoder dispatch. No transport, guest read or device operation was executed here.

MODS Shortcut's checked-out callback tracks byte 34 but does not write it. That source fact does not audit every loaded community mod or prove which focus state existed during the earlier input sequence. Preserve the later emulator setup and avoid attributing the zero-record result to any one filter without new evidence.

## Reproduce and limits

```sh
python3 experiments/mods-shortcut-delivery/input-stage/run-tests.py \
  --upstream /path/to/xdj-az-mods \
  --result /private/path/result.json
```

Each invocation retains a unique ignored `out-history/` extraction/build snapshot. The first development invocation selected a nonexistent public-header path and stopped before compilation; the second reached compilation and failed on an unused fixture callback parameter under `-Werror`. Both were harness-only errors corrected before any assertions ran. The latter failed generated source remains in its original snapshot; the first error is retained in tool history and described here, not presented as a separately captured artifact. Public header constants are now checked against the pinned definitions, including loaded state 1. No production source or threshold was changed.

This serial fixture supplies no concurrency/lifetime proof, physical timing measurement, callback allocation bound, live input provenance, queued-event ownership or direct-menu readiness. The timing source is deterministic and the log sink counts calls; it does not test the separate forced-log bug. All native navigation, stems activation and physical-validation gates remain open.

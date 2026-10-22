# Refresh the menu watchdog before deciding knob suppression

Separate, unpublished core candidate against upstream `3e0c5cb010454228f4ee63226b60d060494eeaae`. The [patch](watchdog-order.patch) changes only `src/azmod/bus_interpose.c`. It passes the focused host regression and a full isolated ARM64 core build. **No emulator installation, physical access, community package or release.**

The current frame dispatcher evaluates `mixer_browse_knob()` before `mixer_watchdog()`, then evaluates menu buttons after it. Both helpers consult the same `wd_stall` variable, but at the boundary they see different values:

| Incoming state | Upstream knob decision | Upstream click/BACK decision | Candidate |
|---|---|---|---|
| `wd_stall=200`, heartbeat unchanged | Retains byte-34 freeze using 200 | Releases normal menu/SDK suppression using 201 | Both use 201; releases the freeze in this frame |
| `wd_stall=201`, heartbeat advances | Leaves byte 34 native using 201 | Resumes normal menu/SDK suppression using 0 | Both use 0; acquires this frame's raw counter as anchor |

These are deterministic source-path mismatches, not an explanation of the physical shortcut quarantine or its earlier cancelled tap. The watchdog in this experiment watches the **overlay heartbeat**; the community frame-filter timing/strike watchdog is a separate mechanism and is not changed.

The candidate moves the existing `<12` return ahead of the existing `>=35` block, then calls the existing watchdog once before that block. CRC rejection still comes first; the SDK raw-copy callback still precedes the watchdog. No watchdog function, threshold, saturation limit, callback count, clock, lock, allocation, quarantine or ownership rule changes. Every previously eligible frame refreshes it once; invalid CRC frames and frames shorter than 12 bytes do not refresh it.

This intentionally moves the decision sample ahead of the tone, FX and x-pad callbacks. Their order relative to each other and the remaining frame processing is unchanged. It gives knob and button processing the same refreshed `wd_stall`; it does **not** make concurrent menu/focus/heartbeat fields one atomic ownership snapshot or guarantee the latest heartbeat at button dispatch. The earlier sample can still precede a later overlay-thread update. No hardware execution-time or callback-cost claim follows from the host tests.

Existing native behavior is retained, including the raw byte-34 jump and `g_srcrow_pin_ours` on focus release, the SDK-focus release's existing SOURCE pin, and the SOURCE-button handback that releases focus in button processing and drops the knob anchor on the next frame. This patch supplies no direct menu, native counter synchronization, queued-event cancellation, focus lifetime proof or new input owner. In particular, it does not solve general native handback. Existing naming/BACK and SOURCE-held suppression remain independent of the normal menu/SDK watchdog decisions.

## Regression

```sh
python3 experiments/menu-watchdog-order/run-tests.py --upstream /path/to/xdj-az-mods
```

The runner reads only the pinned commit with `git show`, applies the patch in a new ignored `out-history/` snapshot, and compiles the **complete actual read path** from `bus_interpose.c`, plus its actual CRC function. This includes the full frame dispatcher, knob, watchdog, menu buttons, SOURCE logic, reorder and independent tone/FX/x-pad functions. It uses the pinned upstream `busread_trace_test.c` fixture's prefix for state and external stand-ins, with only the two SDK filter stand-ins instrumented to capture ordering and an optional edit. The original full upstream fixture, exact original prefix, instrumented prefix and extracted production sources are preserved in every local snapshot. Their fingerprints are recorded in [validation.json](validation.json). No production function is replaced with a behavioral model.

The runner also verifies that everything outside `bus_mixer_frame()` is byte-identical. Its strict unchanged-source control executes the same tests and must fail exactly the 21 named assertions in [expected-unpatched-failures.txt](expected-unpatched-failures.txt). These failures are several observations of the two transition mismatches, not 21 separate defects. Any additional/missing failure, wrong exit status or sanitizer diagnostic rejects the run, including recovering UBSan diagnostics with an otherwise expected exit code.

| Variant | Plain | ASan + UBSan | TSan |
|---|---:|---:|---:|
| Pinned unpatched control | 978 checks / 21 expected failures | 978 / 21 expected | 978 / 21 expected |
| Candidate | 978 checks / 0 failures | 978 / 0 | 978 / 0 |

An independent run rebuilt both variants from the pinned source in all three modes and reproduced byte-identical validation metadata, extraction hashes and outputs. A separate read-only review found no blocking issue within the documented serial-frame scope.

[Sanitized results](validation.txt) cover:

- Menu focus and SDK focus at the 200→201 stall and 201→0 recovery transitions, including the next recovery frame's sticky anchor, signed raw detents and current-frame diagnostics.
- The still-held threshold at 199→200, both 8-bit counter wrap directions, untouched byte 35, full-frame byte comparison and valid outgoing CRCs.
- Every frame length from 0 through 128, two calls per length to establish unchanged refresh cadence, exact-size allocations under ASan, and no SDK filter access to short frames.
- All-`0xff` and deliberately corrupt CRC frames with each focus type: unchanged bytes and watchdog/knob/pending state; no SDK call or CRC repair.
- Saturation at 100,000 and reset by a fresh heartbeat; existing SOURCE held/released behavior and pin timing; current reorder enable/disable publication.
- Checked SDK raw observation before core edits, checked SDK editing after them, and CRC recomputation after a real stand-in SDK edit.

This is a serial bus-thread fixture; the drum/tone/x-pad callbacks are inert in these scenarios. It does not prove active-callback/overlay interleavings. TSan is included as a tool pass, **not** a concurrent overlay or whole-core race proof. Native firmware dispatch, queued input, physical timing, filter quarantine and real devices are outside the fixture. The broad upstream random-trace checksum is intentionally not updated or rerun as a substitute for the discriminating boundary assertions.

An initial author run retained its unpatched failure output locally before the strict expected-failure list was populated. Later review added explicit sanitizer-text rejection and corrected the test's reorder configuration so it actually toggles enabled→disabled. The final recorded suite reran after those harness changes; no production assertion was removed or weakened.

## ARM64 build

[Build metadata](build-info.json) and the [sanitized compiler log](build-log.txt) record a full `make CC=gcc azmod` build in the existing immutable `azmod-build:18.04` image, with `--pull=never --network=none --platform=linux/arm64`. All 465 public upstream regular files were compared against the archive: only the patched `bus_interpose.c` differs, and its hash matches the host regression extraction. The pinned asset extractor validated ten private headers against the existing official core before compilation; those headers and both core binaries stay outside this repository.

The local ARM64 candidate hash is `696a6c4b5b26cdc06e24da828de80058ecaaea28c94ad4430f1277685a28b0af`. It has never been installed. Keep this source patch separate from community packages and review actual-source interactions before composing it with any later core service.

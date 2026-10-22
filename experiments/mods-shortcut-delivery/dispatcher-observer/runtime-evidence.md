# Dispatcher observer emulator pass: no entries captured

October 22, 2026. The localhost-only emulator accepted the selected dispatcher hook and loaded `encoder-dispatch-observer` 0.0.1, but this bounded pass captured **zero entry records and zero diagnostic records**. The result is **inconclusive** for live encoder provenance and native event delivery. No direct menu, input-consumption change, stems fix or release resulted.

[The sanitized result](runtime-result.json) binds this run to binary SHA-256 `c791d1f289cc0bad1b912774476e192c1b1614d3f9fa9da42899f679f49edc5e` and records the hashes of retained private receipts and logs. Hook acceptance, the exact current loaded row and the ready message were checked for the current process. The process identity remained the same throughout the inputs and the later status reads. Both original cores were preserved.

## Controlled inputs and negative observation

The first screenshot showed SOURCE. The run then pressed BROWSE twice to reach a visually confirmed empty four-deck Waveform view. It then sent one clockwise encoder step and one counterclockwise step. The host control counter changed `51 -> 52 -> 51`, returning to its baseline. This counter belongs to the input harness; it is not a sampled native counter and does not prove native delivery or consumption.

The retained ready, Waveform, clockwise and counterclockwise stage logs each contained no `EDO entry` or `EDO diagnostics` records. Screenshots and raw logs remain private. No track was loaded and no audio-quality, playback-stress or hardware-timing test occurred.

After those inputs, two read-only samples of the observer's own module state found `state=1` and `live=1`, with heartbeat advancing `5876 -> 5918`. In both samples, main sequence, queue read/write indices, off-thread mailbox state, queue drops, off-thread skips, TID failures and nested drops were all zero. These reads did not inspect native pointer objects or send further input. They were sequential, noncoherent samples; they do not establish the gate state at the time of earlier input or an atomic view of the observer. The absence of records therefore does not prove that the dispatcher was never invoked, nor explain why it produced no record.

A later [source and binary audit](source-audit-2026-10-22.md) found no static observer defect that explains the result, but identified an evidence limit: the inline status sampler and map listing were not retained as separate files. Its load-bias calculation, read widths and decoding have therefore not been independently reproduced. The exact ELF's RW segment has a different virtual-address/file-offset displacement from its RX segment, so a future sampler must use PT_LOAD-aware address calculations. The saved runtime receipt remains historical evidence, with this provenance limit.

## Preserved harness findings

Before installation, a local fault-path review of the extracted `while_stopped` helper found that a recovery-receipt write failure could mask the saved primary exception. The modeled restart still ran, but the failed write could not create a receipt. The corrected helper preserves the primary exception, reports receipt failure, refuses success when a receipt fails without a primary, and still attempts restart when a start receipt cannot be written. Its focused review passed **115 assertions across 14 in-memory mock cases**. Both reports are retained privately. This exercised only the extracted helper, without importing its original module or executing transport, service, storage or process operations; it is not general validation of the runtime harness.

The first cleanup preflight also failed, **before stopping the process or mutating cleanup state**. The emulator USB filesystem reported mode `777` for the three newly uploaded probe files despite requested `644`/`755` modes. The retained diagnostic showed that all three were regular files with the expected hashes, that the USB mount used `vfat` with `fmask=0000`, and that all 15 existing USB library peers likewise reported `777`.

The cleanup correction accepted that effective mode only for those three known new files, conditional on the matching mount options and baseline peer modes. Registration and unrelated metadata checks remained exact. The failed preflight, diagnostic, correction receipt and earlier harness source are retained; the failure was not discarded or described as an observer failure.

## Restoration and remaining gates

The final restoration receipt confirms preservation of all **146 baseline regular files**, their bytes and modes, the baseline inventory, caches, loaded-mod set, prior absences, both original cores and the shared logging flag. The probe was unmapped and its opt-in removed. A clean empty restart passed, with a fresh loaded-status timestamp and the startup guard expiring naturally. The physical deck was not accessed.

This run establishes successful SDK claim and initialization for the candidate, followed by no captured entries in the tested empty-view input sequence. Live caller/thread/argument provenance, counter ordering, synchronous versus queued execution, event ownership, cancellation, focus handback and device timing remain unresolved. Further investigation needs a distinct observation or routing hypothesis; repeating the same successful installation and input sequence alone would not resolve those gaps.

# Stems memory-refusal recovery — emulator-only core experiment

Start with RESULTS.txt and build-info.json. This patch belongs to Kyle Hosman's GPL-3.0 upstream core, **not** to a community mod. No compiled core files or firmware are distributed here. The physical failure's cause remains unconfirmed.

Later review found that equal sampled positions can miss a moving loop or a stale observation stream. The [activity follow-up](../stems-retry-activity/README.md) reproduces those errors and strengthens retry scheduling, with host and emulator observation checks. Resume during decoding/publication remains unguarded. Read those limits before any hardware-test decision; the original results below do not establish a general guarantee that recovery stays inactive throughout playback.

Clone https://github.com/Kyle-Hosman/xdj-az-mods and check out `3e0c5cb010454228f4ee63226b60d060494eeaae`, then apply `memory-retry.patch` with `git apply --check` followed by `git apply`. Follow upstream BUILDING.md for the azstem target. The recorded build used the SDK ARM64 Ubuntu toolchain. Keep the patched build in the emulator.

BEFORE-FIX-RESULTS.txt is the historical reproduction before the fix. RESULTS.txt describes the later successful tests; references to original/fixed engine files refer to private local artifacts excluded from Git. The recovery pause rule and memory reserve are intentional. Review the test evidence and obtain physical logs before any hardware-test decision.

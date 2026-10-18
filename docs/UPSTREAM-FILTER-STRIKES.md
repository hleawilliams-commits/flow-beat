# Draft issue for Kyle-Hosman/xdj-az-mods: frame-filter strikes never decay

**Title:** Frame filters are permanently disabled by 8 lifetime overruns, including preemption

`sdk_input.c` switches a frame filter off for the session after `SDK_FILTER_STRIKES` (8) overruns of `SDK_FILTER_BUDGET_US` (200 µs), counted cumulatively. The timing is wall-clock (`sdk_ticks()` around `ff->fn`), so a call that the scheduler preempts counts as an overrun even if the filter does almost no work. On the mixer bus (about 1000 calls/s), an always-on filter on a busy deck (four decks playing, stems) eventually collects 8 strikes over a long set and is switched off with no way for the mod to tell or recover.

Seen on a physical XDJ-AZ with a community SOURCE-shortcut mod: registered callback `off=1`, `strikes=8`, button still visible, taps ignored.

**Suggestions (any one helps):**
1. Decay strikes over time, e.g. reset the count after N seconds without an overrun, or require 8 overruns within a sliding window.
2. Count only consecutive or clustered overruns.
3. Expose a read-only `filter_status(id)` so a mod can show a fallback instead of failing silently.

A community workaround (several registered filters with failover) exists in hleawilliams-commits/flow-beat `mods-shortcut` 0.1.3, but it only multiplies lifetime.

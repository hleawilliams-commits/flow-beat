# Visible MODS button with a disabled input filter

The later sustained touchscreen failure has a confirmed cause: the official core quarantined MODS Shortcut 0.1.1's frame filter after eight cumulative timing-limit overruns, while its UI surface remained visible. The protection is behaving as implemented. This does not establish which operation caused each overrun.

## Physical evidence

The [sanitized observation](observation.json) pins the exact core, mod and firmware identities. Bounded read-only inspection checked the mapped binaries, process identity and filter ownership before reading their known state. It found exactly one registered frame filter: the MODS callback, owned by a loaded mod, with `off=1` and `strikes=8`. The core was active. The table was unchanged at the end of the read.

Separate bounded samples found the shortcut enabled, visible, ready and on DECK, with a pending request older than 1,856 seconds, no pulse started and no SOURCE sent. In 0.1.1, only the frame callback clears pending. Once the core stops dispatching that callback, new UI taps are suppressed forever by the old pending request. This accounts for a visible, apparently unresponsive button without implicating the touchscreen hardware.

The user replied to the requested tap after the observation window. These records do **not** claim to trace that fresh tap. The earlier accepted request that was cancelled before injection is a separate event; its precise cancellation guard remains unknown. The previously missing button was another separate condition, its per-button setting being OFF.

## What the evidence does and does not justify

The pinned official core (`sdk_input.c`) measures elapsed counter ticks with a 200 microsecond budget and disables a filter at eight cumulative overruns. The 0.1.1 callback calls `clock_gettime` even when idle. Removing that idle clock work reduces unnecessary work, but no captured timing trace proves it was the sole cause. Scheduling/preemption can also affect elapsed time. Emulator performance does not establish physical long-run timing.

The physical mod log had no overrun message. Source review found that forced frame-thread logs lose their forced status when drained on the UI thread, so an absent message with ordinary logging off does not disprove the observed quarantine. A [separate source-only core experiment](https://github.com/hleawilliams-commits/flow-beat/blob/codex/az-mods-handoff-2026-10-16/experiments/frame-log-force/README.md) covers that logging issue; it is not part of a community package and changes no watchdog limits.

A community update can make its idle path cheaper and expire stale UI requests with honest failure feedback. It cannot re-enable the already quarantined callback in the running official core. A normal user-initiated Reload after stopping playback will load a new package and register a fresh filter. Do not remotely reset the off flag or bypass the watchdog.

Use the physical **SOURCE → MOD MENU** route during practice. No physical settings, files, controls, tracks or core were changed during this diagnosis. Raw captures, addresses, process identifiers and SSH details remain private.

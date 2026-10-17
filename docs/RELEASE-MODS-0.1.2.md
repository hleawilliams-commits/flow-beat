# MODS Shortcut 0.1.2: timeout recovery

This update removes clock/view work from idle input callbacks and clears stranded UI requests with a visible **“MODS timed out. Use SOURCE > MOD MENU”** message. It preserves native input priority, the existing SOURCE pulse deadlines and the official core's timing protection. The button still opens SOURCE; select the native MOD MENU entry to enter the menu.

The confirmed physical failure in 0.1.1 was a quarantined input filter after eight timing overruns, with a stale request left pending. The new timeout cannot reactivate that running filter. Less idle work is a mitigation, not evidence that the AZ will never hit the watchdog again. Physical long-run testing remains pending.

## Install when playback is stopped

Use the physical **SOURCE → MOD MENU** route. In **Community Mods**, refresh the existing **FLOW Beat Link** repository and update **MODS Shortcut** to **0.1.2**. Then use the normal **Reload** once playback is stopped; Reload unloads tracks. A fresh registration takes effect on that reload. Do not repeatedly tap a timed-out button during practice—use SOURCE instead.

Keep the verified USB backup and prior [0.1.1 package](https://github.com/hleawilliams-commits/flow-beat/releases/tag/mods-shortcut-0.1.1). If needed, disable this community mod through SOURCE → MOD MENU, or restore only its previous package/namespaced settings through the existing recovery process. Preserve newer official core files and update protections. No physical installation was performed during development.

The package contains only its manifest, license, icon and `mods-shortcut.so`. It includes no stems engine, firmware, core logging patch, USB1/USB2 buttons or theme. Stems capacity/retry and the other requested work remain separate.

[Build, concurrency and emulator evidence](evidence/mods-shortcut-0.1.2/README.md) and [physical incident evidence](evidence/mods-filter-quarantine-2026-10-17/README.md) state the checks and limitations. Publication status and package hashes are recorded in the publication receipt when completed.

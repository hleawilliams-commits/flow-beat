# User's update and recovery requirements

Start with `docs/START-HERE.md` for current state, branch status, validation and unfinished requests. This is a development handoff, not authorization to deploy every preview.

- Keep these mods separate from Kyle Hosman's AZ-MODS core. Use the documented SDK and community-mod packages with unique IDs and versioned manifests.
- Never bundle or overwrite `azmod.so`, `azmod-core.so`, `azstem.so`, firmware, boot hooks, signed manifests, or official update helpers in a community-mod update. Do not disable signature/version checks on the physical deck.
- Core experiments, including the stems memory-retry patch, remain separate emulator builds. Do not promote them into a normal community-mod release or silently apply them to the physical USB.
- Before a physical USB change, make and verify a fresh backup using the original local kit (`../recovery-kit/mod_recovery.py backup` in Henry's original workspace); `tools/recovery/` here contains source/tests only, not registration or the restore point. Preserve the existing restore point until the user has tested the new version successfully.
- Keep the previous `.azmod` release and source revision. Build, validate, and exercise changes in the emulator before physical testing. Update one mod at a time, away from a live set.
- Roll back community files and their namespaced settings only; preserve newer official core files and unrelated settings. The recovery kit is bound to the FLOWMATIC USB UUID and provides verified backup, community-only disable, and community-only restore commands.
- Do not promise that native community mods are sandboxed or that any update is risk-free. Report the actual checks, installed version, and physical-test status.

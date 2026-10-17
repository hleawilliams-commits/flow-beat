# Community-only recovery tool source

This is the reviewed algorithm used by Henry's private local recovery kit. Actual USB backups, `registered-usb.json`, `restore-point.json` and pairing/access files are intentionally excluded. **Use the existing local kit for Henry's USB; a fresh clone is not his restore point.** CLI operations refuse to run without registration and verify the external disk UUID before writes.

Backup copies and hashes the MODS payload; restore replaces community-mods and their namespaced settings while preserving the current official core/loader and unrelated settings. Disable changes the community switch. Never restore a whole old MODS copy over a newer official core.

Portable fixture tests (temporary folders only, no physical USB required):

```sh
python3 tools/recovery/test_mod_recovery.py
```

The source was copied unchanged. The test import/output paths were adapted for this repository. CLI operations depend on macOS diskutil and a separately verified registration. Registering another disk or rebuilding lost registration needs deliberate setup; do not bypass identity checks. Keep real backups private because MODS can contain pairing/access configuration.

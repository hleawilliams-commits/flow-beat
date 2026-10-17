# Archived emulator automation

These are the original development scripts, retained for another engineer/assistant to understand and reproduce the checks. They are not generic one-click tools. They retain original workspace-relative paths such as `outputs/az-modding/xdj-az-emu/tools` and `work/`. Run only in a prepared disposable emulator workspace, after reading/adapting paths, fixtures and starting UI state.

The upstream azemu helpers target the emulator's localhost SSH; do not redirect these recipes at the physical deck. Memory-pressure tests intentionally reserve emulator RAM. Camelot installation edits the emulator library through the supported spool. Test scripts may load/play tracks or press UI controls. No firmware, audio fixtures, databases, credentials or extracted EP147 executable are included.

- `test-deck-focus-emulator.py` and `selected-probe.py`: selected-deck UI and LED capture investigation.
- `picks-native-probe/`: native browser/filter investigation used before the 0.3.0 shortcut.
- `setup-stems-repro.py`, `monitor-stems.py`, `analyze-stems-repro.py`, `stems-load-all.py`: baseline stems reproduction recipes.
- `stems-memory-pressure.c`, `test-stems-reload-fix.py`: controlled memory-refusal/recovery checks; see experiments/stems-memory-retry for findings and prerequisites.
- `install-camelot-preview.py`, `read-jog-art.py`: artwork spool and received JPEG reconstruction for the static jog preview.

Build/harness tests in the mod folders are the more portable entry point. Run one QMP client at a time. Original scripts may rely on preceding manual setup; consult docs/SESSION-NOTES.md.

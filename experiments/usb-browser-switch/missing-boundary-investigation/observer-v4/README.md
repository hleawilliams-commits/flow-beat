# USB Browser Observer V4

Unpublished emulator-only diagnostic, unique ID `usb-observer-v4`, version 0.0.1. Read the [exact native contract and limits](../README.md). V3 is preserved separately.

Adds a scalar-only observation of the native empty-list request method. The exact native caller and active TLS scope correlate supplied request values and native scalar return. The new hook never dereferences the agent or a cached event. `EMPTY_PATH_ENTRY` does not mean a committed or completed source switch, and native result 0 does not establish completion.

The environment opt-in is `AZ_USB_OBSERVER_V4_EMULATOR=1`. All exact-firmware/prologue, thread, heartbeat, bounded-queue and partial-claim protections remain. This is not a USB-button package and has no activation API.

From the repository root with the pinned sibling SDK:

```sh
sh experiments/usb-browser-switch/missing-boundary-investigation/observer-v4/tests/run-focused.sh
python3 ../az-mods-sdk/tools/azmod-pack build experiments/usb-browser-switch/missing-boundary-investigation/observer-v4
python3 ../az-mods-sdk/tools/azmod-pack validate experiments/usb-browser-switch/missing-boundary-investigation/observer-v4
```

Host checks: 1,122 assertions each in plain, ASan/UBSan and TSan. ARM64 SDK build/validation passed. The [bounded emulator run](../runtime-evidence/README.md) observed two empty-path entry/result pairs followed by one copied commit boundary; final completion remains UNKNOWN. No catalog entry or release package is supplied. Keep raw firmware, library data, logs, screenshots and backups private; preserve original emulator files and protections before any coordinated run.

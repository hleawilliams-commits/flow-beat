# USB Browser Observer V3

Unpublished emulator-only observation experiment, ID `usb-observer-v3`, version 0.0.1. Read the [static contract](../README.md) before using it. It has no custom activation API and is not a USB-button release.

The two V2 entry observers are joined by an ordinary function-entry hook at `0x2014a88`. An exact native caller address and stack-local TLS context correlate a copied committed identity with the active media request. `COMMITTED_AT_ENTRY` is evidence at that boundary only. Missing observations, changed context and exceptions cannot be converted into completion. Neither original callback is followed by a native pointer read.

The mod refuses initialization without `AZ_USB_OBSERVER_V3_EMULATOR=1`, the exact EP147 firmware tag, exact prologues, required SDK services and accepted hook ownership. A partial claim failure leaves any installed wrappers inert and forwarding. The main-thread and heartbeat guards, bounded queue and 128-record budget remain.

From the repository root, with the pinned SDK checked out alongside it:

```sh
sh experiments/usb-browser-switch/completion-contract/observer-v3/tests/run-focused.sh
python3 ../az-mods-sdk/tools/azmod-pack build experiments/usb-browser-switch/completion-contract/observer-v3
python3 ../az-mods-sdk/tools/azmod-pack validate experiments/usb-browser-switch/completion-contract/observer-v3
```

The focused host suite passed 620 assertions each in plain, ASan/UBSan and TSan. It checks native originals invoked once, no reads after original return, frees/reentry/exception unwinding, owner/request changes, exact caller gating, nested depth limits and all three partial claim-failure positions. [Build/source hashes](../validation.json) and [bounded emulator evidence](../runtime-evidence/README.md) are separate. Physical behavior is untested.

No `.azmod` release or catalog entry is supplied. Keep private firmware, raw screenshots/logs, test libraries and backups out of Git. A developer emulator run requires a fresh exact-byte/mode baseline, unchanged core hashes, explicit localhost assertions, and restoration of community/configuration/approval state after removing this probe and its opt-in.

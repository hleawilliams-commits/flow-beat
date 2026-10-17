# MODS SOURCE delivery experiment

A simulated interruption in mixer-frame delivery reproduces an accepted MODS tap that never sends SOURCE. This establishes a limitation of the current shortcut under that condition. **It does not establish that the physical deck experienced that interruption.** No production mod, core or physical USB was changed by this experiment.

## Emulator comparison

Both builds use a unique `mods-shortcut-delivery` community ID. The ordinary MODS shortcut was temporarily disabled so their touch targets could not overlap. The emulator's current UI and stems cores stayed unchanged.

| Test | Delivery to the shortcut | Observed outcome |
|---|---|---|
| Normal adapter, 0.0.1 | Every validated mixer callback | Touch opened the native SOURCE screen. |
| Gap adapter, 0.0.2 | Callback adapter returns unchanged for 1250 ms after a request, then calls the existing shortcut | At the early sample, request pending 1, pulse 0, sent 0, no callback seen by the shortcut. The first delivered callback was 1283 ms after the request; it expired before injection, with pulse deadline 0 and sent 0. Native screen stayed on waveforms. |

The adapter delays only the shortcut's view of callbacks. Native EP147 still receives its ordinary mixer frames. It does not stop a bus, block a thread, alter the emulator kernel or create actual hardware starvation. That distinction matters: the native virtual device normally supplies repeated idle frames, whereas the reported physical request's reason remains unknown.

The paired screenshots and numeric observations are in [evidence](evidence/). `build-hashes.json` identifies the exact binaries and generated source files used. Screens contain empty emulator decks, not Henry's music library. The experimental button says D-MODS.

The normal control also exposed an instrumentation limit: the native screen visibly reached SOURCE while the SDK briefly reported BROWSE during that transition. The first diagnostic banner incorrectly called that a cancelled navigation. The helper now distinguishes a view change before any injection from a view change after a SOURCE press was sent, and uses shorter messages to fit the banner. Those feedback corrections have host regression coverage but have not been visually rerun. The recorded screenshots preserve the earlier wording; they are not a claim that navigation failed in the normal control.

## Why the pinned SDK cannot guarantee independent delivery

The checked SDK/core is 1.2.3. Its `frame_filter` service edits a frame **when one arrives**. It does not provide a frame producer, a native screen-navigation command or an open-Mod-Menu service. Its `post` service schedules a mod's own UI callback, but supplies no navigation primitive to call. `pointer_inject` supplies X pointer input; the waveform screen has no native touchscreen SOURCE target corresponding to the physical SOURCE button.

Consequently, a shortcut implemented only through `frame_filter` cannot send SOURCE while no mixer callback is being delivered. Removing the timeout merely leaves an old request pending; it does not create an input event. Ignoring genuine knob, LOAD or screen-button input would introduce a separate interaction problem. No cancellation guard was weakened in this work.

A reliable SDK route needs a separate upstream core change: a documented, size-gated UI navigation service such as `open_mod_menu(id)`, queued to the core's UI/message thread and implemented independently of MCU frame arrival. The core should own the supported native-navigation/focus transition, reject duplicate or invalid requests, and make opening the already-open menu idempotent. That requires designing and testing the core transition; merely appending a function pointer or setting a private menu flag is insufficient. No such core patch has been implemented here, and it must not be hidden inside a community-mod update.

## Files and checks

- `prepare-preview.py` generates an isolated source folder from the production component and diagnostic helper, replacing its ID and button label. `--mode normal` uses ordinary delivery; `--mode gap` inserts `frame-gap.h`.
- `frame-gap.h` is a test adapter, never a proposed shipping fix.
- `test-frame-gap.c` checks the adapter's boundary cases, including clock reversal. It passes with ASan/UBSan.
- Both original ARM64 emulator builds passed the SDK build/validation checks.
- The updated diagnostic helper passes 183 assertions in plain and ASan/UBSan runs, including the observed transitional-view case.
- The experiment used bounded memory inspection of its own named state words to distinguish request acceptance, pulse start, arrival delay and cancellation.

Example private build preparation from this repository:

```sh
python3 experiments/mods-shortcut-delivery/prepare-preview.py /tmp/mods-delivery-preview --mode gap
python3 ../az-mods-sdk/tools/azmod-pack build /tmp/mods-delivery-preview
```

No package was published. Loading this preview requires a deliberate emulator-only install; these commands only prepare/build source.

## Restored state

After testing, the diagnostic mod was unloaded and its temporary folder removed. All five saved enabled/approval/config files were restored byte-for-byte. Original MODS Shortcut 0.1.1 and Phase Strip 0.1.3 loaded again; the other preview mods returned to their prior versions. Both core SHA-256 values remained unchanged, recorded in `evidence/restored-preview.txt`. The restart left the emulator tracks unloaded. The physical deck was never accessed during this experiment.

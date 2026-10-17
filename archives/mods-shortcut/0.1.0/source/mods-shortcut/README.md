# MODS Shortcut 0.1.0

A small **MODS** button above the waveforms opens the native **SOURCE** list. Tap the official **MOD MENU** row to select it, then tap it again or press the browse knob to open the menu. The native preview asks for that confirmation. The button does not open Mod Menu directly: the pinned SDK has no public operation for that. A banner directs you to the MOD MENU row once SOURCE appears.

This is a separate community mod, with no firmware hooks or core replacement. It uses one SDK surface and an editable mixer-input filter. It does not load tracks, change playback or modify audio. It remains an emulator preview until separately tested on hardware.

## Layout and controls

| View | Button rectangle |
|---|---|
| 4 DECK | `(1080, 50, 110, 30)` |
| 2 DECK / 2/4 | `(900, 50, 110, 30)` |

The button is shown only on the deck screen, outside waveform pixels. Coordinates leave space after the current STEM PICKS and TAG MIX buttons in 4 DECK; in 2 DECK it sits before them. Disable **MODS source shortcut** in Mod Menu to hide it. Its namespaced setting is `mods-shortcut:show`.

The parent grouped-panels mod uses four surfaces, the SDK per-mod maximum. Keeping this shortcut separate gives it its own single surface and independent toggle. Check the global 16-surface allowance when enabling both with other mods.

## Input behavior

One tap requests one SOURCE press. It lasts at most 200 milliseconds, stops once SOURCE is observed, and is discarded if it waits more than one second to run. Duplicate requests are suppressed while pending and for 500 milliseconds after the accepted tap.

Physical screen buttons, LOAD, BACK, knob click or knob movement cancel the request without changing their incoming frame. Navigation away from the deck screen cancels the press too. The helper changes only mixer byte 32's SOURCE bit (`0x80`) and byte 6's native panel-press latch (`0x08`), leaving every other byte unchanged; the SDK recomputes the frame CRC. Incoming frames release the synthetic press. There are no pointer injections, guessed row coordinates, automatic knob turns or calls into core-private menu state.

SOURCE is sent only from the deck view: sending SOURCE while already on SOURCE would toggle native navigation. If the SDK input service is absent or refuses registration, the button stays hidden.

## Build and verification

From this folder:

```sh
python3 ../../az-mods-sdk/tools/azmod-pack build .
./tests/run-focused.sh
make test AZ_SDK=../../az-mods-sdk
```

Current host checks:

- 264 shortcut assertions cover bounded press/release, duplicate requests, every screen/LOAD/BACK bit, knob click and movement, delayed/reversed time, other views, no unrelated frame edits and banner delivery only after SOURCE appears.
- 125 component assertions invoke the actual surface and registered mixer callback, including label drawing, both layouts, contact event handling, bounds, hiding, row toggle and missing/refused input services.
- Focused suites pass plain and AddressSanitizer/UndefinedBehaviorSanitizer.
- Official SDK harness passes plain, ASan/UBSan and TSan. **That harness lacks `frame_filter`**; its scenarios specifically check the hidden fallback and layout. It does not exercise synthetic SOURCE input. The focused full-component test supplies that SDK service.
- Docker ARM64 build and SDK package validation pass.

Linux plain-harness checks also pass in the existing `az-mods-sdk:1` ARM64 container using GCC: **11 checks passed**, including measured no-I/O and no-allocation gates. The SDK and source were mounted read-only. This Linux run retains the same harness limitation: it checks the hidden fallback because `frame_filter` is absent, so these measured gates do not cover an active SOURCE pulse. The dedicated full-component tests exercise the registered mixer callback; emulator testing checks native navigation.

macOS harness checks do not measure the SDK's no-I/O/no-allocation gates. Host tests do not establish physical-device behavior. The root task records emulator navigation and layout results separately.

## Sources and limits

The source-button mapping is present in the pinned upstream core `src/azmod/bus_interpose.c` (`mixer_screen_buttons`) and emulator `az-preview/src/controls.rs` (`TOP_SOURCE`). Official Mod Menu entry is a native row implemented by upstream `src/azmod/source_list_entry.c`; this package leaves its state and selection to the official core.

The key preview, device images, firmware, music and USB state are not part of this package. Disable or remove this community mod to remove its surface and input action; preserve newer official AZ-MODS core files.

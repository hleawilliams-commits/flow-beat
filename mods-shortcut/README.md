# MODS Shortcut 0.1.1

**Hide WAVEFORM heading** leaves the title area blank. It adds no replacement text, diagram or deck indicator. Turn that setting off to restore the stock WAVEFORM title.

The existing **MODS** button opens the native **SOURCE** list. Tap **MOD MENU** to select it, then tap it again or press the browse knob to open the menu. The native preview asks for that confirmation. The pinned SDK has no direct Mod Menu-open operation; the button sends SOURCE and shows a banner directing you to the official row.

This is an emulator preview community mod. It does not change playback, load tracks, read deck-selection LEDs, or patch the official core. Physical testing remains separate.

## Layout and settings

| Surface | Rectangle | Setting |
|---|---|---|
| Blank heading mask | `(0, 0, 174, 46)` | **Hide WAVEFORM heading**, `mods-shortcut:hide-heading` |
| MODS, 4 DECK | `(1080, 50, 110, 30)` | **MODS source shortcut**, `mods-shortcut:show` |
| MODS, 2 DECK / 2/4 | `(900, 50, 110, 30)` | Same button setting |

Both settings default on and work independently. The surfaces appear only on the deck screen. The heading mask is input-transparent and stops before the native tabs at x180 and phase strip at y46. It sends stock black (`#000000`) to the SDK fill primitive. The core applies the active theme once, producing the same ground color as the native heading:

| SDK theme key | Native background |
|---|---|
| 0, stock | `#000000` |
| 1, Day | `#DCDCDC` |
| 2, AZ-MODS | `#000000` |
| 3, Midnight | `#050B18` |
| 4, Ember | `#0D0605` |
| 5, Forest | `#040D08` |
| 6, this workspace's Flow Neon extension | `#090611` |

These displayed values come from the pinned core's `day_mode.c` theme table, `DM_GROUND` and `azmod_dm_key()`. They are not passed back as fill colors: `azcm_fill_rect` reaches `ui_draw.c`'s themed `fill_rect`, so supplying an already-themed value tints it a second time. The mask redraws when the theme key changes. An unknown theme or missing theme service leaves the mask hidden, preserving the stock title. If the optional second surface or redraw service is unavailable, the mask is omitted while the MODS button remains usable.

This component uses two of the SDK's 16 global surfaces. Grouped Panels separately uses its per-mod maximum of four.

## SOURCE shortcut behavior

One tap requests one SOURCE press, bounded to 200 milliseconds. Arrival at SOURCE ends it; a request delayed more than one second is discarded. Pending requests and duplicate taps within 500 milliseconds are suppressed.

Physical screen buttons, LOAD, BACK, knob click or movement cancel the request without changing their incoming frame. Navigation away from the deck screen cancels it too. Only mixer byte 32's SOURCE bit (`0x80`) and byte 6's panel-press latch (`0x08`) are changed; the SDK recomputes the CRC. Incoming frames release the synthetic press. There are no automatic knob turns, pointer injections or calls into private core state. If the input service is absent or refuses registration, the button stays hidden.

## Build and checks

From this folder:

```sh
python3 ../../az-mods-sdk/tools/azmod-pack build .
./tests/run-focused.sh
make test AZ_SDK=../../az-mods-sdk
```

- ARM64 Docker build and SDK validation pass.
- 264 SOURCE-shortcut assertions and 232 component assertions pass plain and ASan/UBSan. They cover bounded input, cancellation, the actual registered filter, independent settings, a blank input-transparent mask, native background colors, unknown-theme hiding and optional-service failures.
- Official SDK harness passes plain, ASan/UBSan and TSan, with 9 measured checks per macOS pass.
- Linux plain harness passes 11 checks, including measured no-I/O and no-allocation gates.

The official harness lacks `frame_filter`: it tests the hidden SOURCE-button fallback and the active mask drawing/layout. Its gates do not cover an active SOURCE pulse; the focused full-component test supplies that service. macOS does not measure no-I/O/no-allocation. Host checks do not establish physical-device behavior. The [exact corrected build is visually verified on the native 4 DECK emulator screen](../docs/evidence/clean-heading/README.md): mask and adjacent background samples all match Flow Neon RGB `(9,6,17)`. Other layouts are not claimed as visually verified for this build.

Disable either setting independently or remove this community package to restore the underlying stock UI. Preserve newer official AZ-MODS core files.

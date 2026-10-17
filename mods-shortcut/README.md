# MODS Shortcut 0.1.2

**Hide WAVEFORM heading** leaves the title area blank. It adds no replacement text, diagram or deck indicator. Turn that setting off to restore the stock WAVEFORM title.

The existing **MODS** button opens the native **SOURCE** list. Tap **MOD MENU** to select it, then tap it again or press the browse knob to open the menu. The native preview asks for that confirmation. The pinned SDK has no direct Mod Menu-open operation; the button sends SOURCE and shows a banner directing you to the official row.

This community update reduces idle work and recovers stranded requests after the confirmed 0.1.1 failure described in [the incident evidence](../docs/evidence/mods-filter-quarantine-2026-10-17/README.md). It does not patch the official core or guarantee against another quarantine. See the [emulator evidence](../docs/evidence/mods-shortcut-0.1.2/README.md); physical testing remains pending.

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

One tap requests a SOURCE press with a 200 ms pulse deadline. Arrival at SOURCE ends it; a request older than one second is discarded. Pending requests and duplicate taps within 500 ms are suppressed. Reversed clocks and repeated timestamps are rejected.

Physical screen buttons, LOAD, BACK, knob click or movement cancel the request without changing their incoming frame. Navigation away from the deck screen cancels it too. Only mixer byte 32's SOURCE bit (`0x80`) and byte 6's panel-press latch (`0x08`) are changed; the SDK recomputes the CRC. Incoming frames release the synthetic press. If the input service is absent or refuses registration, the button stays hidden.

The idle filter now tracks the native knob and returns without a clock or view call. An active callback atomically claims an immutable timestamp token before reading the clock. The UI can expire an unclaimed token and show **“MODS timed out. Use SOURCE > MOD MENU”**, even when the core has stopped calling the filter. That allows another request without reusing the old callback's timestamp or pulse state. A callback holding the BUSY bit retains ownership until it returns; the UI never steals it. Completion is tied to its original token, so an old completion cannot report success for a new request. Replacing an expired active pulse passes one incoming frame unchanged before a new SOURCE press.

This timeout does **not** re-enable a filter that the official core has quarantined. Repeated tapping will not repair quarantine; use the physical SOURCE button and official MOD MENU entry. A normal EP147 restart starts a new filter registration. The SDK exposes no query or reset service for quarantine.

The core's 200 µs timing budget and eight-strike protection are unchanged. Active callbacks still read the clock, consult the SDK view and perform atomic operations. Preemption can exceed the budget, so this update does not guarantee freedom from future overruns. The 200 ms pulse and one-second request deadlines are checked when a callback runs; a callback paused after sampling time can resume later, and no release frame can be sent while callbacks are absent. UI timeout feedback similarly waits for a UI tick. These are not strict wall-clock guarantees.

## Build and checks

From this folder:

```sh
python3 ../../az-mods-sdk/tools/azmod-pack build .
./tests/run-focused.sh
make test AZ_SDK=../../az-mods-sdk
```

Host validation for 0.1.2:

- ARM64 Docker build and SDK validation pass.
- 302 shortcut and 232 component assertions pass plain and ASan/UBSan, retaining non-target input-byte preservation, pulse boundaries, cancellation, touch hit regions and heading-mask checks.
- 35 concurrent/idle assertions pass plain, ASan/UBSan and TSan. The test invokes the actual registered callback on a separate thread. It forces timeout/new acceptance before consumer claim, timeout while BUSY, a fresh clock read after delayed claim, and isolation from old completion. A simultaneous producer/filter run exercises thousands of callbacks. Idle and invalid frames make zero clock/view calls while valid idle frames still track the knob.
- Official SDK harness passes plain, ASan/UBSan and TSan, with 9 measured checks per macOS pass. Its missing `frame_filter` service means it covers the hidden-button fallback and heading mask, not an active pulse. macOS does not measure the harness's no-I/O/no-allocation gates.

The focused suite also models a quarantined filter by ceasing callback delivery: the UI recovers pending state, reports the manual fallback once, and accepts a later request. This proves recovery from the missing callback, not prevention of the physical timing overruns. The archived 0.1.1 package and source remain unchanged. Host checks do not establish physical-device reliability. The [release note](../docs/RELEASE-MODS-0.1.2.md) records availability and the normal stopped-playback installation path.

Disable either setting independently or remove this community package to restore the underlying stock UI. Preserve newer official AZ-MODS core files.

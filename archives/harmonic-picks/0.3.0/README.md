# Harmonic Picks

Version 0.3.0 is an emulator preview. Physical-deck testing is pending.

## Playable native browser shortcut

The **PICKS** button sits at the top left of the waveform screen, above Phase Strip.
It opens the native browser. Select a track, then press **LOAD 1, 2, 3 or 4** to load it
onto that deck; press PLAY when ready. The shortcut never loads or starts a track itself.

Set up native matching once for each USB:

1. Select the USB's **TRACK** category (or a playlist you want to search within).
2. Hold **TRACK FILTER/EDIT**. Enable **BPM** and **KEY**, set the desired BPM range,
   and enable the orange **MASTER DECK** button. Disable other conditions unless wanted.
3. Return to the list and enable Track Filter with a short press. Check for the funnel icon.
4. After loading a track, tap **PICKS** on the waveform screen to return to this list.

Native MASTER following was observed updating the reference BPM when MASTER tempo changed.
PICKS preserves the current source, category and filter state. It does **not** enable a
filter you turned off, reset your conditions, select TRACK, or refresh a different source.
If you browse elsewhere, PICKS returns there. This is a shortcut to stock matching, not
an automatic custom ranking or acapella-loading integration. The native list's results
and settings are separate from the custom suggestions below. Some USB filter settings
persist; verify the funnel and MASTER state after selecting a source/restarting.

Disable the button through **PICKS native browser shortcut** in this mod's settings.
The implementation uses the SDK mixer frame filter for a bounded 200 ms BROWSE press.
It edits only that button and its latch, preserves current faders/other controls, yields
to physical panel input, and never sends LOAD, PLAY, filter toggles or pointer taps.
There are no firmware hooks and no core replacements.

## Harmonic and acapella suggestions (Mod Menu)

Open **Mod Menu > Harmonic Picks**, then tap **Match deck 1, 2, 3 or 4**.
Choose the deck marked MASTER yourself: SDK 1.2.3 does not expose tempo MASTER.
The reference stays selected even if another deck starts playing.

- Matches the selected deck's current BPM; reranks while the page is open.
- Suggests the same Camelot key, adjacent numbers on the same ring, or the relative major/minor.
- Lists up to 40 suggestions, favoring same key and smaller pitch changes.
- **Pitch** is the signed adjustment needed on the suggested track, within +/-6%.
- **Time** explicitly labels 1x, 2x or 1/2x beat interpretation. For example,
  a 60 BPM track matches a 120 BPM reference at 2x interpretation and 0% pitch.
- **USB** identifies the source library. VOX is inferred from the title, not audio analysis.
- Tap **All tracks / Acapellas** or press the browse knob to change the filter.
- Tap **Previous / Next** or turn the browse knob to scroll.

The page only suggests tracks; it does not load a track or change playback. The buttons
provide touch paging; this does not change the core Community Mods screen's swipe behavior.

Keys are stored library metadata, not a measurement of the sounding key. Key Shift and
pitch with Master Tempo off can change the sounding key and are not tracked by this SDK.
BPM must be available from the selected deck; unknown BPM is not replaced with the original.
Library-key fallback checks track id plus title/artist, rejecting conflicting keys across USBs.

Libraries are read once when the mod starts, with a 20,000 eligible-track limit. Reload mods
after changing connected USB libraries. Suggestions depend on exported metadata; the mod
does not check each audio file's existence. The emulator's library contains more metadata
than the small selection of audio files copied into its test USB.

## Build and checks

From this directory, with paths adjusted to your SDK checkout:

```sh
python3 ../../az-mods-sdk/tools/azmod-pack build .
make test AZ_SDK=../../az-mods-sdk
clang -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I../../az-mods-sdk/include tests/test_matches.c -o /tmp/test-harmonic-picks
/tmp/test-harmonic-picks
clang -fsanitize=address,undefined -g -I../../az-mods-sdk/include \
  tests/test_shortcut.c -o /tmp/test-picks-shortcut
/tmp/test-picks-shortcut
```

Validation: ARM64 SDK build and package validation; 55 focused matching/input assertions
under AddressSanitizer/UBSan; 204 shortcut assertions cover pulse release, physical-button
priority, malformed/wrong-bus frames, stale requests and preservation of other controls; SDK harness passes plain, ASan/UBSan and TSan (8 checks per pass).
The macOS SDK harness does not measure no-I/O/no-allocation gates. In the emulator:
confirmed direct menu entry, reference selection, live BPM reranking (77.2 to 79.2),
touch paging, acapella filtering, and empty-deck behavior.

## Update and recovery

This is a community-only package: no official core, stems engine, firmware or boot files.
The prior 0.1.0 and 0.2.0 packages and sources are retained in the workspace recovery artifacts.
The 0.3.0 preview is installed only in the emulator. Follow the parent AGENTS.md before a
physical USB change, including a verified backup and a separate test before promotion.

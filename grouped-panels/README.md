# Grouped Deck Panels 0.1.0

Four compact headers bring each deck's title, original Camelot key, relative phrase counter and selected-side indicator together beside its native BPM box. This community-mod **emulator preview has been exercised in the emulator**; it has not been installed or validated on the physical deck. It does not change audio, stems, tempo, key or playback controls.

## Screen layout

The headers appear only on the deck screen in **4 DECK** view. Their positions are measured for the 1280 × 800 display:

| Deck | Header rectangle `(x, y, width, height)` |
|---|---|
| 1 | `(10, 561, 520, 46)` |
| 2 | `(650, 561, 520, 46)` |
| 3 | `(10, 681, 520, 46)` |
| 4 | `(650, 681, 520, 46)` |

The lower details panels retain their stock order, **1–2 above 3–4**. This mod does not reorder the scrolling waveforms, whose existing four-deck order is **3–1–2–4**, or enlarge their renderer. The headers leave the native BPM boxes at the right, remaining-time controls and overview waveforms below, and scrolling waveforms above clear. BPM remains the stock live readout; this mod does not paint a second BPM value. Header surfaces are input-transparent, so native controls receive their own touches.

The selected deck gets a cyan number and top border, plus **LEFT** or **RIGHT**. Selection comes from observing the existing deck-selection LED frames: left selects 1 or 3, right selects 2 or 4. The mod does not edit those frames or LEDs. This indicates which deck the physical side controls, not which deck is MASTER or playing. Unknown signals or signals older than one second remove the highlight.

**Grouped deck panels** in Mod Menu turns the display and data workers on or idle. The setting is namespaced as `grouped-panels:show`. Headers stay hidden in 2 DECK and 2/4 DECK views.

## Keys and phrase counters

**KEY** is the track's original analyzed key, normalized from Camelot, classic or Open Key notation. It does **not** follow Key Shift. `KEY --` means the key is unknown or unavailable.

**PHRASE** is an eight-bar, four-beats-per-bar counter anchored to the beat nearest the track's first play after loading. It is not analysis of the song's structure or a detected phrase downbeat. Start playback from a known downbeat to make the relative counter useful. The eight cells fill beat by beat; the last bar is amber.

The four **Deck N phrase starts now** actions in Mod Menu request a new anchor for that deck. Before first play, the counter stays unknown. Off-grid jumps, long jumps before track speed has been measured, or unavailable grid data can make the count uncertain; `PHRASE ?` makes that visible. Set a new anchor on a known downbeat after an uncertain jump. Reloading a track clears its previous anchor. An unloaded deck displays `PHRASE --`.

Deck snapshots supply the title and key when the core exposes them. Otherwise, a separate reader checks the two USB libraries for only the four loaded tracks. It keeps four metadata slots, not a whole-library cache. Scans wait for 750 ms of stable track IDs and run at most once per USB per 20 seconds; new metadata can take roughly 20 seconds plus scan time to appear, and continuous loading can delay it further. The 10 ms phrase worker continues independently of library scanning.

Missing metadata stays unknown. Conflicting title/key matches for the same content ID across USBs are withheld; so are results from a library scan truncated at the SDK's 20,000-track limit. Load events invalidate old results even when the new track reuses an ID. UI reads never wait for a library scan or a mutex: contention briefly gives an unknown field instead of a previous track's value. SDK title/key fields take precedence when available.

## Companion shortcut and surface budget

The separate [MODS Shortcut](../mods-shortcut/) package adds a **MODS** button above the waveforms. It opens native **SOURCE**. Select the official **MOD MENU** row, then tap the selected row again or press the browse knob to enter. Selecting and then entering the row follows the native list behavior. The pinned SDK does not expose a direct Mod Menu opening operation. The shortcut has its own toggle and bounded input handling; it is not part of these input-transparent headers.

The SDK allows four surfaces per mod and 16 across all mods. Grouped Panels consumes four, and MODS Shortcut 0.1.1 consumes two (button plus optional blank-heading mask). Before trying this layout in the emulator, disable these three superseded community packages from loading:

- `key-next-to-bpm` — four surfaces.
- `phrase-coach` — four surfaces.
- `deck-focus` — four surfaces.

Then restart the emulator's deck application so their 12 surfaces are released. Hiding their settings rows or toggling their overlays off does **not** unload those packages or free their surface slots. Keep their packages and settings for rollback. Do not remove unrelated features such as Phase Strip, Stem Picks or Tag Mix just to replace the lower headers.

This preview is separate from Kyle's core, official updater, firmware and stems engine. Do not copy an experimental core or emulator settings onto a physical USB. Before any separately authorized physical test, follow [AGENTS.md](../AGENTS.md), make and verify the existing recovery-kit backup, and retain the selected known-good restore point. A successful host test is not physical-device validation.

## Build and checks

Use the SDK revision recorded in [dependencies.json](../docs/dependencies.json), checked out beside `flow-beat`. From this folder:

```sh
python3 ../../az-mods-sdk/tools/azmod-pack build .
./tests/run-focused.sh
make test AZ_SDK=../../az-mods-sdk
```

The ARM64 package build uses the SDK's Docker toolchain. The focused runner accepts `AZ_SDK=/path/to/az-mods-sdk` and `CC=clang`; it builds in a temporary directory and removes its test binaries afterward. It runs layout, data and real-renderer paint checks in plain and AddressSanitizer/UndefinedBehaviorSanitizer modes, then runs the concurrent data checks with ThreadSanitizer.

- **Layout:** all LED selection bytes, both deck pairs, stale-signal handling, all four header rectangles and native-control boundaries.
- **Data:** 4,110 checks covering key conversion, load/eject and same-ID invalidation, USB conflicts, truncated libraries, scan throttling, nonblocking reads, phrase anchoring and uncertain jumps, plus concurrent worker/UI operations.
- **Paint:** the actual renderer with an instrumented SDK, covering day/night colors, every selection pair, unknown and unloaded states, long ASCII/UTF-8 titles, all 32 phrase beats, input-transparent surfaces and unchanged observed LED frames. Bounds and text-line boxes are checked; this does not substitute for native-font screenshots.

The SDK harness checks the full component in plain, ASan/UBSan and TSan modes. The Linux SDK run measured the no-I/O/no-allocation gates and passed all 10 checks; macOS harness runs do not measure those gates.

## Completed emulator verification

[Emulator evidence and screenshots](../docs/evidence/grouped-panels/README.md) record the completed checks:

- All four loaded decks showed key and relative phrase readouts during playback.
- Every left/right deck-selection pair was checked.
- All eight settled-idle checks passed.
- The grouped headers hid in both 2 DECK and 2/4 DECK views. Native BPM, time controls and overview waveforms remained visible.
- The MODS shortcut opened SOURCE, where the official MOD MENU row could be selected and then entered with a second tap or the browse knob.

These are emulator results. The key remains the original analyzed key, phrase timing remains relative to its anchor, and library metadata can still take roughly 20 seconds plus scan time to appear. Physical installation and hardware testing remain pending.

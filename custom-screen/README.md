# Custom Screen 0.5.0

Your own panels on the deck screen. Up to four click-through panels, each holding the readouts you choose, placed wherever you want them. The layout is a text file, `layout.txt`, made in the **Deck Screen Designer** (an interactive page: drag panels and widgets over the real 4 DECK screen, see every other display mod where it sits, then press **Copy layout**).

This build has been host-tested only (layout parser, FlowVJ, ANLZ and waveform tests, the SDK harness plain / ASan+UBSan / TSan, and an end-to-end run against FlowVJ's server with a fake deck). It has not run on the emulator or a deck yet. 0.3.0 adds `phrase_name`, `drop_in` and `mix_with` (host-tested against a real export). 0.4.0 adds `bpm_fit` and `pitch_to` (host-tested; not on the emulator yet).

## What a panel can show

| Widget | Shows |
|---|---|
| `deck_num` | The deck number in a block: cyan when that deck is selected, amber at 30 s left, red at 10 s |
| `title`, `artist` | The loaded track; `No track loaded` when empty |
| `key` | Original analyzed key in Camelot; green / amber / red against the deck that has been playing longest |
| `bpm`, `bpm_gap` | Current BPM; the gap to the other playing deck when it is within 8 BPM |
| `phrase`, `phrase_bar`, `beat` | Phrase meter: bar n of 4, 8 or 16 (Mod Menu **Phrase length**, default 4), one cell per bar filling beat by beat, four beat dots. From 0.5.1 it counts from the track's rekordbox analysis: bar 1 is the first downbeat of the rekordbox phrase playing (PSSI) or, without phrases, of the track (PQTZ beat-in-bar), so it is right wherever you start the track and after any cue, loop or jump. Tracks without analysis fall back to the anchored counter (bar 1 = where play was pressed; **Deck N phrase starts now** fixes it) |
| `time_left` | Remaining time, amber at 30 s, red at 10 s |
| `stems` | A STEMS pill **only while stems are playing** on that deck; nothing when stems are off or missing |
| `state`, `side` | PLAY / PAUSE; LEFT / RIGHT on the deck a side has selected |
| `clock` | The deck's wall clock (HH:MM, deck time zone) |
| `label`, `box` | Your own text; a filled rectangle |
| `flowvj` | FlowVJ's live Look (its lights look when no Look is live), with LOCK (beat follows the deck), HOLD (Show Director held), SAFE and BLACK badges; `FLOW not seen` when FlowVJ hasn't answered for 3 s |
| `vj_lights` | The lights desk's look, `BLACKOUT` in red |
| `vj_tap` | A button that sends `act=` to FlowVJ: `next` / `prev` (the next / previous saved Look, else the next preset), `hold` (Show Director hold), `safe` (the desk's SAFE MODE), `auto` (autopilot), `drop` (drop hit), `black` (blackout). HOLD / SAFE / AUTO / BLACK stay lit while on. It takes the touch only on the deck screen while FlowVJ answers; otherwise the tap reaches the deck |
| `wave` | The track's rekordbox waveform, zoomed and scrolling: `zoom=` beats across the widget (2 to 128, default 16), the beat grid with bar lines (`grid=1`, the default; `grid=0` hides it) and the playhead in the middle. `color=auto` keeps rekordbox's colours; a hex colour draws it in one colour |
| `overview` | The whole track, the played part dimmed, the playhead as a line |
| `phrase_name` | The rekordbox phrase playing now: INTRO, VERSE, BUILD, BREAK, BRIDGE, DROP (CHORUS in mid and low mood tracks), OUTRO; `--` when the track has no phrase analysis |
| `drop_in` | `DROP IN n`: bars to the next DROP / CHORUS phrase, amber, red in the last 4 bars; `IN THE DROP` during one; nothing when no drop is ahead |
| `mix_with` | The other loaded deck that fits this one best (key first, then the closest BPM) and its key: green for a good key match, amber for a close one |
| `key_wheel` | A small Camelot wheel: this deck's key as a big dot (B keys on the outer ring, A keys inside), the other playing decks as small dots in their deck colours (1 blue, 2 red, 3 green, 4 amber) |
| `bpm_fit` | The track tempos that mix with this deck's current BPM inside the pitch fader's reach: `117-131  ½ 59-65  ×2 234-263` for 124 at ±6 %. `range=` is the reach in % (1 to 50, default 6; set 8, 10 or 16 to match the deck's TEMPO range), `halves=0` drops the half / double time ranges |
| `pitch_to` | How far to move this deck's pitch to sit on the other deck: `+2.4% to 1`, or `-1.1% to 2 at ½` when it locks at half time (`×2` for double). The other deck is the playing one you're mixing out of, else the best mix partner. Green inside `range=` (default 6), amber up to twice that, red beyond |

The FlowVJ widgets need FlowVJ with `deck-status.js` and AZ BEAT switched on: FlowVJ sends its state to the deck on UDP 9102 (to the address it hears flow-beat from) and the buttons answer on UDP 9103. `examples/flowvj-panel.txt` is a ready panel.

Each widget follows deck 1 to 4, or `L` / `R`: the deck the left or right side has selected, so one panel per side can follow the deck you are mixing on.

## The layout file

```
version 1
name My screen
panel 1 x=10 y=561 w=520 h=46 views=4,24 bg=auto border=none bw=0 edge=top
widget 1 key deck=L x=54 y=25 w=90 h=21 size=14 color=auto align=l text=KEY 
```

Panels use screen pixels (1280 x 800); widgets use pixels inside their panel. `views` is any of `4` (4 DECK), `24` (2/4 DECK), `2` (2 DECK). Colours are `auto`, `dim`, `accent`, `cell` or six hex digits. `text=` runs to the end of the line, spaces included; on data widgets it goes before the value. Lines starting with `#` are ignored; a line the mod doesn't understand is skipped and named in a banner.

The mod reads, in order: `MODS/community-mods/custom-screen/data/layout.txt` on the USB (yours to edit on a computer), then the shipped `assets/layout.txt`, then the same layout built in. It checks the file once a second and redraws within a second of a change, with a banner naming the layout.

The shipped layout puts each deck's song name, a big key, BPM and time left in its left deck strip (4 DECK only, x 0-140, over the strip's empty top cell and its KEY label, beside that deck's waveform). Taps still reach the deck, so the KEY row's arrow works as before. `examples/` has the name-and-artist version, today's Grouped Panels headers rebuilt as widgets, and two more.

## 4-Deck Waveforms

`wave` and `overview` read the loaded track's analysis off the USB it came from, the way FLOW Beat Link 0.4.0 does: the rekordbox id gives the audio file's path (from the library), and an index of `PIONEER/USBANLZ` (built once per stick from each `ANLZ0000.DAT`'s path tag, rebuilt when the stick changes) gives its `ANLZ0000.DAT` and `.EXT`. Colour detail (PWV5) is used when the export has it, else the blue PWV3 detail; the grid is PQTZ. This runs on the mod's slow-work thread (`jobs.h`, the fourth and last thread a mod may start), only while a shown panel holds one of these widgets (or `phrase_name` / `drop_in` / `phrase` / `phrase_bar` / `beat`, which read the PSSI phrase tag from the same `.EXT` file, plain or masked as exports write it), and keeps only the four loaded tracks (about 100 KB each).

`examples/four-deck-waveforms.txt` puts all four decks in one panel over the 4 DECK waveform area (rows 3-1-2-4 at y 80 / 202 / 324 / 446, x 212 to 1088), each with its title, time left, a 16-beat wave and the overview, which leaves three panels free. Scrolling panels repaint at most 25 times a second in all, and the mod logs its longest paint every 10 seconds (`paint: N panels in 10 s, longest N us`) so a run shows whether it fits.

A track with no analysis on the stick shows "No rekordbox waveform for this track" and is looked up again every 30 seconds. Reading `PIONEER/USBANLZ` is outside the mod's own folders: the SDK allows it but doesn't promise it.

## Mod Menu

**Custom Screen** turns it on or idle. Under it: **Reload layout**, **Two-deck view** (A = Auto, 2 = 2 DECK, 24 = 2/4 DECK, as in Grouped Panels; one- and two-character labels because the Mod Menu's choice boxes are narrow), **Phrase length** (4, 8 or 16 bars) and **Deck N phrase starts now**.

## Try a layout on the emulator

1. In the Deck Screen Designer, press **Copy layout**.
2. Run `./push-layout.sh` from this folder (or `./push-layout.sh my-layout.txt`). It writes the layout to `MODS/community-mods/custom-screen/data/layout.txt` on the emulator's USB stick, the file the mod reads first, and the screen changes within a second with no restart. The mod must already be pushed and on. Set `AZ_EMU` if the emulator checkout isn't found.

## Surfaces and slots

The SDK allows four surfaces per mod and 16 in all. Custom Screen uses four. With it on, Grouped Panels (four surfaces) does the same job and can be switched off. Key next to BPM, Stem Badge, Phrase Coach and Mashup Helper are covered by widgets too, which is how this mod frees load slots.

## Build

On the Mac, from the repo root, with the SDK and arm64 sysroot beside it (as for flow-beat):

```sh
cd custom-screen
S=../../sysroot-bionic-arm64
make AZ_SDK=../../_sdk AZ_SDK_INCLUDE=../../_sdk/include \
     CC="clang --target=aarch64-linux-gnu --sysroot=$S --gcc-toolchain=$S/usr -fuse-ld=lld"
make test AZ_SDK=../../_sdk AZ_SDK_INCLUDE=../../_sdk/include
cc -std=c11 -Wall -Wextra -Werror tests/test_layout_file.c -o /tmp/t && /tmp/t
cc -std=c11 -Wall -Wextra -Werror tests/test_flowvj.c -o /tmp/f && /tmp/f
cc -std=gnu11 -I../../_sdk/include tests/test_flowvj_live.c -o /tmp/l -lpthread -lm && /tmp/l   # real UDP on 127.0.0.1:9102/9103
python3 ../../_sdk/tools/azmod_pack.py validate .
```

After changing `assets/layout.txt` for a release, run `python3 tools/make-default.py` so the built-in copy matches.

Deck readouts reuse Grouped Panels' `data.h`, `hud.h` and part of `layout.h`. No audio, tempo, key, control or library changes.

Waveform checks, from this folder:

```sh
cc -std=c11 -Wall -Wextra -Werror tests/test_anlz.c -o /tmp/a && /tmp/a
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -I../../_sdk/include -pthread tests/test_waves.c -lm -o /tmp/w && /tmp/w
```

`tests/fixtures` holds two real exports (one with a beat grid, one with an empty grid); `test_waves.c` builds a fake USB from them and paints all four rows while another thread loads and ejects.

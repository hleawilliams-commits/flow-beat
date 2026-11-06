# FLOW Beat Link (flow-beat)

An [AZ-MODS](https://github.com/Kyle-Hosman/xdj-az-mods) mod for the Pioneer XDJ-AZ. It sends
each deck's beat, BPM, play state, playhead and track to FLOW VJ (or any program) over UDP
port 9100, so visuals and lights lock to the decks.

It adds rows to the Mod Menu: **Send beat to FLOW** (on by default), **Send mixer to FLOW** (on; 0.5.0, FLOW Mixer Link), **Send track analysis to FLOW** (on), **Ableton Link from this deck** (off; 0.5.1, see below), **Send decks to Ableton** (off; 0.5.2, Link Audio, see below) and **Send mixer data (test)** (off; raw mixer frames for mapping). 0.5.3 adds Stem FX, Drum Roll HOLD and loop state for FLOW (no row, see below). It only reads deck
state through the AZ-MODS core's services: it does not patch the deck's code, change any
control or audio, or write files. Needs AZ-MODS 1.2.3 or newer.

## Mods in this repository

| Folder | Mod | Status |
|---|---|---|
| `/` (root) | **FLOW Beat Link** (`flow-beat`): beat, BPM, play state, track and (0.3.0) each deck's own audio level + bass to FLOW VJ over UDP; a test row sends mixer panel data for mapping | 0.5.3 released 2026-10-31: Stem FX / Drum Roll HOLD / loop state (AZ-MODS 1.2.6+), plus 0.5.2 Link Audio, 0.5.1 Link on Deck, 0.5.0 Mixer Link + Cue Map, 0.4.0 guest-USB analysis; emulator-tested, first deck run pending |
| `mods-shortcut/` | **MODS Shortcut**: SOURCE shortcut with lower idle work and safe timeout feedback | 0.1.2, emulator-tested; physical validation pending |
| `key-next-to-bpm/` | **Key next to BPM**: the loaded track's key as a badge on each deck, Camelot or classic | test build 0.1.1, installed on a deck, menu checked |
| `phase-strip/` | **4-Deck Phase Strip**: beat ticks for every playing deck under one playhead, works in 4 DECK | test build 0.1.1, installed on a deck, menu checked |
| `mashup-helper/` | **Mashup Helper**: Camelot match and pitch gap for the two decks in play | test build 0.1.0, not yet run on a deck |
| `stem-badge/` | **Stem Badge**: STEMS or NO STEMS on each deck | test build 0.1.0, not yet run on a deck |
| `set-log/` | **Set Log**: every track you play, written to a CSV on the USB | test build 0.1.0, not yet run on a deck |
| `harmonic-picks/` | **Harmonic Picks**: a Mod Menu page of tracks that fit the playing deck by key and BPM | test build 0.1.0, not yet run on a deck |
| `deck-meters/` | **Deck Meters**: a level meter per deck with clip warning, and a flash on the drop | test build 0.1.0, not yet run on a deck |
| `phrase-coach/` | **Phrase Coach**: a bar counter on each deck, amber in the last bar of the phrase | test build 0.1.0, not yet run on a deck |
| `set-clock/` | **Set Clock**: time left in the set, and a warning as the playing track ends | test build 0.1.0, not yet run on a deck |

**Link on Deck** (0.5.1, test): the row **Ableton Link from this deck** makes the deck itself a Link peer, so the Mac bridge below is no longer needed; see [Ableton Link from the deck](#ableton-link-from-the-deck). Turn the bridge off when the row is on, or two leaders fight over the session.

`tools/flow_link_bridge.py` runs on the Mac: it turns FLOW Beat Link's packets into an Ableton Link session, so AUTO/PILOT, Ableton and other Link apps lock to the deck (`python3 -m pip install aalink`, then `python3 tools/flow_link_bridge.py`).

Every mod has its own entry in `azmods-repo.json`; add `hleawilliams-commits/flow-beat` once on the deck and all of them show under Browse. Each mod's folder has its own README. Each mod has its SDK release workflow in `ci/release-<id>.yml`, like `ci/release.yml` for FLOW Beat Link: move it to `.github/workflows/` to release that mod on an `<id>-<version>` tag.

[MODS Shortcut 0.1.2 update and rollback instructions](docs/RELEASE-MODS-0.1.2.md).

## Install

On the deck: **Mod Menu > Community Mods > Repositories > Add**, enter
`hleawilliams-commits/flow-beat`, then **Browse > FLOW Beat Link > Install > Reload now**.

## FLOW Mixer Link (0.5.0)

The XDJ-AZ mixes audio in hardware, but its mixer panel still reports every knob and fader to
the deck program on the mixer bus. flow-beat reads that frame (read-only) and sends FLOW VJ a
`"type":"mixer"` packet when something moves (at most about 30 a second) and once a second:
per channel the fader, TRIM, HI/MID/LOW and COLOR knob; the crossfader, master level and EQ,
booth level, the lit Sound Color FX and its PARAMETER, Beat FX SELECT/active/channel/LEVEL/TIME,
and the X-PAD. Values are 0..1. The byte map is in `mixer.h`, taken from Kyle Hosman's AZ-EMU
and AZ-MODS sources; `tests/test_mixer.c` checks it on a computer
(`cc -o /tmp/tm tests/test_mixer.c && /tmp/tm`). Color FX and the Beat FX channel are latched
from button presses, so they read `"?"` until first pressed after the deck starts.

## Settings

In `MODS/azmods.cfg` on the USB (read every 2 s):

```
flow-beat:host = "192.168.1.20"     where to send (an IPv4 address; default: broadcast on every network the deck is on, Wi-Fi and LINK)
flow-beat:port = 9100
```

## Ableton Link from the deck

With **Ableton Link from this deck** on, the deck joins the Ableton Link session on its network
(Wi-Fi or LINK) and the lead deck leads it. Since 0.6.4 that is the deck on air: the playing deck whose channel fader is up most
(with faders level, the one started last), so cueing the next track with its fader down no longer moves Ableton:

- **Tempo**: Link's tempo is the lead deck's BPM, pitch fader included. A tempo change made in
  Ableton is overruled while a deck plays; with nothing playing the last tempo holds.
- **Beat**: Link's beat follows the deck's own grid phase, read every 5 ms on the deck. A jog
  nudge or a new lead deck moves the session at once; the playhead's small block-sized steps
  are averaged out so peers don't jump.
- **Bar**: Link's bar (4 beats) sits on the track's "1": from a **this is 1** press, else from
  the track's rekordbox beat grid (PQTZ, read off the USB the track plays from). Without either,
  only the beat is lined up.
- **Start/stop**: the session starts when a deck starts and stops when all stop, for apps with
  Start Stop Sync on.

**This is 1**: send `{"flowbeat":1,"type":"one","late":true}` (or `false`) to UDP port 9101 on
the deck (broadcast works). `late: true` makes the next beat the 1, `false` the beat just
played; without `late`, the nearest beat. It holds until the lead deck's track changes or it
stops. FLOW VJ's phrase-sync button sends this to the Mac bridge on 127.0.0.1:9101 today; it
needs to send it to the deck (or broadcast) instead.

Settings in `MODS/azmods.cfg`:

```
flow-beat:link = 1          Link on without the Mod Menu row (the row wins when there is one)
flow-beat:link_ms = 0       how far the deck's sound runs behind its playhead, ms: raise it if
                            Link apps play ahead of the deck, lower (negative) if behind
flow-beat:one_port = 9101   where "this is 1" presses arrive
```

Link uses UDP multicast 224.76.78.75 port 20808 and its own ports, never 9100, so FLOW VJ's
packets are untouched. Ableton Link is by Ableton AG, GPL-2.0-or-later, built in from
`third_party/link` (a git submodule: `git submodule update --init --recursive` before building).

### Stem FX, Drum Roll HOLD and loops to FLOW (0.5.3, test)

With AZ-MODS 1.2.6 or newer, flow-beat also sends what the DJ is doing with Kyle's new pad features, so
FLOW VJ can react to them. No new row and no new load slot: it rides on **Send beat to FLOW**.

`{"flowbeat":1,"type":"fx","seq":N,"decks":[{"d":1,"sfx":0,"drum":0,"hold":0,"smp":-1,"loop":0}, ...]}`
when anything changes (at most every 20 ms) and every second:

| Field | Meaning | Where it comes from |
|---|---|---|
| `sfx` | Stem FX pads held: bit 0 VOCAL ECHO (pad 5), 1 INST ECHO (6), 2 INST BRAKE (7), 3 DRUMS ECHO (8) | `azstem_stem_fx_on(deck)`, exported by azstem.so 1.2.6+ (dlsym; `-1` on older AZ-MODS) |
| `drum`, `hold`, `smp` | Drum Roll on (SLIP LOOP armed, pad 8 green), HOLD (pad 7), the selected sample pad 0-3 | the pad lights AZ-MODS paints (no export for these) |
| `loop` | 0 none, 1 a loop is held (RELOOP/EXIT lit), 2 looping (LOOP IN/OUT blinking), 3 a loop being set | EP147's own button lights |

`drum`, `hold`, `smp` and `loop` are `-1` for a deck that is not on a side right now (after DECK 3/4
select only the two shown decks have lights). A mod loaded before flow-beat that repaints the pads
(Pad Lights on its own pages) can hide the Drum Roll pattern on that side. See `fx.h`; tests in
`tests/test_fx.c` (`make -C tests fx`).

### Link Audio: each deck's sound into Ableton (0.5.2, test)

With **Send decks to Ableton** on (it turns Link on too), the deck joins the session
as the peer **XDJ-AZ** and offers four Link Audio channels, **Deck 1** to **Deck 4**. Each is that
deck's own sound, taken before the hardware mixer, so faders, EQ, Color FX, Beat FX and the master
are not in it (no software on the deck sees the master). In Ableton Live 12.4 or newer, turn on
Link Audio in Settings > Link, Tempo & MIDI and pick a deck as an audio track's input to record it
or put effects on it, with no audio interface.

- A channel only sends while a peer listens to it: 16-bit stereo, about 1.5 Mbit/s per deck. Use a wired network (the LINK port or Ethernet) for more than one deck.
- Each block is placed on Link's beat timeline when it is heard (the tap's sample count mapped to
  the clock, plus `link_ms`), so Ableton can line it up with its own beat.
- Each deck is sent at its tap's own sample rate, measured in the first half second and snapped to
  the standard rate it is (the emulator's taps run at 44100), so Ableton plays it at the right speed.
  The log names that rate per deck, the measured rate about 6 s in, and when a deck starts or stops
  being received.

```
flow-beat:link-audio = 1          on without the Mod Menu row (the row wins when there is one)
flow-beat:link_audio_ms = 0       added to link_ms for the audio only: raise it if a deck's audio
                                  lands early in Ableton, lower (negative) if late
flow-beat:link_audio_rate = 0     0: send at each tap's own rate, measured and snapped to 44100,
                                  48000, ... (the emulator's taps run at 44100); else force one
```

Link Audio comes with Ableton Link 4 (`LinkAudio.hpp`, same licence and submodule). One of its
headers needs a small fix to build with the SDK's GCC 7.5; the fixed copy is in
`third_party/link-gcc7` (see its README). `make -C tests audio` checks the four channels, the
samples, the format and the beat timing against a second Link Audio peer on the computer.

## Packets (UDP, JSON)

Every packet: `{"flowbeat":1,"type":...,"seq":N,...}`; `seq` counts all packets. Decks 1..4.

- `beat`, the moment a playing deck starts a new beat: `{"d":1,"bpm":128.00,"n":17}`
  (`n` = beats since that deck started playing).
- `state`, about 50 a second:
  `{"decks":[{"d":1,"loaded":1,"play":1,"bpm":128.00,"ph":0.4321,"pos":61.250,"id":1234,"start":3},...]}`
  `ph` = position in the current beat 0..1 (-1 = no grid); `pos` = playhead in seconds
  (-1 unknown); `id` = rekordbox track id (0 unknown), so the receiver can find the bar (the
  "1") in the track's rekordbox beat grid; `start` orders play starts (higher = started later,
  0 = not playing). Since 0.6.5 each deck also has `"key":"8A"` (the key as the stick's rekordbox
  library spells it, "" until found) and the packet ends with `"air":2`: the deck on air (the
  rule Link and the lights use), 0 = none. FLOW Key Sync on the Mac uses both to keep Ableton
  Live's Scale in the on-air deck's key.
- `track`, on load and every ~2 s: `{"d":1,"title":"...","artist":"...","key":"8A"}` (`key` since 0.6.5).

No deck clock is sent: the receiver times beats by arrival.

## Build

Built with Kyle Hosman's [AZ-MODS SDK](https://github.com/Kyle-Hosman/az-mods-sdk). Releases
are built with the SDK (`azmod-pack build`); fetch the Link submodule first
(`git submodule update --init --recursive`). Tests on a computer: `make test` (SDK harness),
`make -C tests` (Link end to end with a second Link peer, and the bar lookup). `ci/release.yml` is the SDK release workflow, ready to move to `.github/workflows/` for builds on a `flow-beat-<version>`
tag.

## License

GPL-3.0-only (see LICENSE).

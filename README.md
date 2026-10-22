# FLOW Beat Link (flow-beat)

An [AZ-MODS](https://github.com/Kyle-Hosman/xdj-az-mods) mod for the Pioneer XDJ-AZ. It sends
each deck's beat, BPM, play state, playhead and track to FLOW VJ (or any program) over UDP
port 9100, so visuals and lights lock to the decks.

It adds rows to the Mod Menu: **Send beat to FLOW** (on by default), **Send mixer to FLOW** (on; 0.5.0, FLOW Mixer Link), **Send track analysis to FLOW** (on), **Ableton Link from this deck** (off; 0.5.1, see below) and **Send mixer data (test)** (off; raw mixer frames for mapping). It only reads deck
state through the AZ-MODS core's services: it does not patch the deck's code, change any
control or audio, or write files. Needs AZ-MODS 1.2.3 or newer.

## Mods in this repository

| Folder | Mod | Status |
|---|---|---|
| `/` (root) | **FLOW Beat Link** (`flow-beat`): beat, BPM, play state, track and (0.3.0) each deck's own audio level + bass to FLOW VJ over UDP; a test row sends mixer panel data for mapping | 0.5.0 built (FLOW Mixer Link + Cue Map); 0.4.0 built (guest-USB analysis: beat grid, waveform, phrases); 0.3.0 released; none of them run on a deck yet |
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
(Wi-Fi or LINK) and the lead deck, the playing deck that started last, leads it:

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

## Packets (UDP, JSON)

Every packet: `{"flowbeat":1,"type":...,"seq":N,...}`; `seq` counts all packets. Decks 1..4.

- `beat`, the moment a playing deck starts a new beat: `{"d":1,"bpm":128.00,"n":17}`
  (`n` = beats since that deck started playing).
- `state`, about 50 a second:
  `{"decks":[{"d":1,"loaded":1,"play":1,"bpm":128.00,"ph":0.4321,"pos":61.250,"id":1234,"start":3},...]}`
  `ph` = position in the current beat 0..1 (-1 = no grid); `pos` = playhead in seconds
  (-1 unknown); `id` = rekordbox track id (0 unknown), so the receiver can find the bar (the
  "1") in the track's rekordbox beat grid; `start` orders play starts (higher = started later,
  0 = not playing).
- `track`, on load and every ~2 s: `{"d":1,"title":"...","artist":"..."}`.

No deck clock is sent: the receiver times beats by arrival.

## Build

Built with Kyle Hosman's [AZ-MODS SDK](https://github.com/Kyle-Hosman/az-mods-sdk). Releases
are built with the SDK (`azmod-pack build`); fetch the Link submodule first
(`git submodule update --init --recursive`). Tests on a computer: `make test` (SDK harness),
`make -C tests` (Link end to end with a second Link peer, and the bar lookup). `ci/release.yml` is the SDK release workflow, ready to move to `.github/workflows/` for builds on a `flow-beat-<version>`
tag.

## License

GPL-3.0-only (see LICENSE).

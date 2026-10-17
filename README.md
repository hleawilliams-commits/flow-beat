# FLOW Beat Link (flow-beat)

**Claude / Grok / collaborator handoff:** start with [docs/START-HERE.md](docs/START-HERE.md). This development branch includes emulator previews, tests, screenshots and versioned archives. The live community catalog and existing releases are unchanged.

An [AZ-MODS](https://github.com/Kyle-Hosman/xdj-az-mods) mod for the Pioneer XDJ-AZ. It sends
each deck's beat, BPM, play state, playhead and track to FLOW VJ (or any program) over UDP
port 9100, so visuals and lights lock to the decks.

It adds one row to the Mod Menu: **Send beat to FLOW** (on by default). It only reads deck
state through the AZ-MODS core's services: it does not patch the deck's code, change any
control or audio, or write files. Needs AZ-MODS 1.2.3 or newer.

## Mods in this repository

| Folder | Mod | Status |
|---|---|---|
| `/` (root) | **FLOW Beat Link** (`flow-beat`): beat, BPM, play state and track to FLOW VJ over UDP | source 0.2.1; existing releases unchanged |
| `key-next-to-bpm/` | **Key next to BPM**: the loaded track's key as a badge on each deck, Camelot or classic | test build 0.1.1, installed on a deck, menu checked |
| `phase-strip/` | **4-Deck Phase Strip**: beat ticks for every playing deck under one playhead, works in 4 DECK | 0.1.2 emulator preview; hardware testing pending |
| `mashup-helper/` | **Mashup Helper**: Camelot match and pitch gap for the two decks in play | test build 0.1.0, not yet run on a deck |
| `stem-badge/` | **Stem Badge**: STEMS or NO STEMS on each deck | test build 0.1.0, not yet run on a deck |
| `set-log/` | **Set Log**: every track you play, written to a CSV on the USB | test build 0.1.0, not yet run on a deck |
| `harmonic-picks/` | **Harmonic Picks**: native browser shortcut plus custom harmonic/acapella suggestion page | 0.3.0 emulator preview; custom suggestions do not load tracks |
| `deck-meters/` | **Deck Meters**: a level meter per deck with clip warning, and a flash on the drop | test build 0.1.0, not yet run on a deck |
| `phrase-coach/` | **Phrase Coach**: a bar counter on each deck, amber in the last bar of the phrase | unpublished layout changes based on 0.1.0; emulator checked |
| `set-clock/` | **Set Clock**: time left in the set, and a warning as the playing track ends | disabled in emulator at user request |
| `grouped-panels/` | **Grouped Deck Panels**: original Camelot key, phrase and physical-side selection beside native BPM | 0.1.0 emulator preview; 4 DECK only; replaces three older header overlays |
| `mods-shortcut/` | **MODS Shortcut**: SOURCE shortcut and optional removal of the WAVEFORM heading | 0.1.1 emulator preview; independent heading/button toggles |
| `deck-focus/` | **Selected Deck Highlight**: cyan numbers/edges show left 1/3 and right 2/4 selection | 0.1.0 emulator preview; 4 DECK view only |

`tools/flow_link_bridge.py` runs on the Mac: it turns FLOW Beat Link's packets into an Ableton Link session, so AUTO/PILOT, Ableton and other Link apps lock to the deck (`python3 -m pip install aalink`, then `python3 tools/flow_link_bridge.py`).

The existing published mods have entries in `azmods-repo.json`; the new previews on this branch are not promoted there. For the published versions, add `hleawilliams-commits/flow-beat` once on the deck and all of them show under Browse. Each mod's folder has its own README. Each mod has its SDK release workflow in `ci/release-<id>.yml`, like `ci/release.yml` for FLOW Beat Link: move it to `.github/workflows/` to release that mod on an `<id>-<version>` tag.

## Install

On the deck: **Mod Menu > Community Mods > Repositories > Add**, enter
`hleawilliams-commits/flow-beat`, then **Browse > FLOW Beat Link > Install > Reload now**.

## Settings

In `MODS/azmods.cfg` on the USB (read every 2 s):

```
flow-beat:host = "192.168.1.20"     where to send (an IPv4 address; default: broadcast on every network the deck is on, Wi-Fi and LINK)
flow-beat:port = 9100
```

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
are built with the SDK (`azmod-pack build`). `ci/release.yml` is the SDK release workflow, ready to move to `.github/workflows/` for builds on a `flow-beat-<version>`
tag.

## License

GPL-3.0-only (see LICENSE).

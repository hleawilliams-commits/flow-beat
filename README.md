# FLOW Beat Link (flow-beat)

An [AZ-MODS](https://github.com/Kyle-Hosman/xdj-az-mods) mod for the Pioneer XDJ-AZ. It sends
each deck's beat, BPM, play state, playhead and track to FLOW VJ (or any program) over UDP
port 9100, so visuals and lights lock to the decks.

It adds one row to the Mod Menu: **Send beat to FLOW** (on by default). It only reads deck
state through the AZ-MODS core's services: it does not patch the deck's code, change any
control or audio, or write files. Needs AZ-MODS 1.2.3 or newer.

## Mods in this repository

| Folder | Mod | Status |
|---|---|---|
| `/` (root) | **FLOW Beat Link** (`flow-beat`): beat, BPM, play state and track to FLOW VJ over UDP | released, 0.2.0 |
| `key-next-to-bpm/` | **Key next to BPM**: the loaded track's key as a badge on each deck, Camelot or classic | test build 0.1.0, not yet run on a deck |
| `phase-strip/` | **4-Deck Phase Strip**: beat ticks for every playing deck under one playhead, works in 4 DECK | test build 0.1.0, not yet run on a deck |

Every mod has its own entry in `azmods-repo.json`; add `hleawilliams-commits/flow-beat` once on the deck and all of them show under Browse. Each mod's folder has its own README. Each mod has its SDK release workflow in `ci/release-<id>.yml`, like `ci/release.yml` for FLOW Beat Link: move it to `.github/workflows/` to release that mod on an `<id>-<version>` tag.

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

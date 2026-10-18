# Crate Remote 0.1.0

Browse both USBs on an iPad or phone while the deck's screen stays on mixing. See what fits the deck you're mixing from, search, open your playlists, and queue picks. Each pick flashes on the deck as **iPad pick: Title · key · BPM · USB n**. You still load with LOAD.

## Use it

1. Put the deck on Wi-Fi (or your phone's hotspot) and the iPad on the same network.
2. On the deck: **Mod Menu → Community Mods → Installed → Crate Remote** shows the address (`http://<deck>:8765`) and a 4-digit PIN. The PIN changes every time the deck starts.
3. Open the address in Safari on the iPad and enter the PIN. *Add to Home Screen* makes it full-screen.

| Tab | What it does |
|---|---|
| **Fits next** | Tracks on the chosen USB that mix with the reference deck: Camelot key green (good) or amber (risky), within ±3/6/10% BPM, half/double time included. Already-loaded tracks are left out. The reference is the playing deck furthest into its track; tap a deck to pick it yourself. |
| **Search** | Title or artist, keys coloured against the reference deck, BPM gap shown. |
| **Playlists** | Your folders and playlists, in your order. |
| **Queue** | Your picks (up to 12), removable. |

The deck strip shows all four decks: title, key, BPM, time left (amber at 30 s, red at 10 s), and stems.

## What it does and doesn't do

- **Uses the network:** one web page on port 8765, readable by devices on the deck's network that know the PIN. The **iPad browser** row in Mod Menu turns it off and closes the port.
- **Read-only:** reads the libraries through the SDK. It never loads tracks, writes the library, or touches controls.
- The queue lives in the deck's memory and clears on restart. It isn't a rekordbox playlist yet: the SDK only lets mods add playlists while nothing plays from that USB. A live "add to playlist" needs a new SDK service from Kyle.
- The deck doesn't give mods the loaded track's key, so the page looks it up in the library by track ID.

## Build and checks

```sh
python3 tools/embed.py                       # after editing ui/index.html
python3 ../../az-mods-sdk/tools/azmod-pack build .
make test AZ_SDK=../../az-mods-sdk
```

Host: 29 request/PIN/JSON checks (plain, ASan/UBSan), SDK harness plain/ASan/TSan. Emulator: 14,604 tracks and 119 playlists read from USB 1; PIN gate (403 without it); Fits next, Search, Playlists and Queue exercised at iPad size; banner on the deck. [Evidence](../docs/evidence/crate-remote-0.1.0/). Not yet run on the physical deck.

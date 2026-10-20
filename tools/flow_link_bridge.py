#!/usr/bin/env python3
"""FLOW Link bridge: the XDJ-AZ's beat on Ableton Link, so AUTO/PILOT (and Ableton, Traktor,
anything with Link) locks to the deck.

FLOW Beat Link on the deck broadcasts JSON over UDP port 9100. This listens on the same port
(shared, so FlowVJ keeps receiving), follows the lead deck (the playing deck that started
last), and drives a Link session:
  tempo  = the lead deck's current BPM
  beat   = lined up on every beat packet from the lead deck (a new beat starts on the deck)

Run:   python3 flow_link_bridge.py          (Ctrl+C to stop)
Needs: python3 -m pip install aalink
In AUTO/PILOT turn Link on; it shows one peer when the bridge runs.

Bar alignment (2026-10-18): the bridge asks FlowVJ (http://127.0.0.1:8717/deck-waveform, the same rekordbox
beat grid the lights use, also for guest USBs via flow-beat 0.4.0) for the lead track's grid, works out which
beat of the bar each deck beat is, and puts Link's bar (quantum 4) on the track's real "1". Without a grid it
still locks tempo and beat, as before.

"This is 1" (2026-10-20): FlowVJ's phrase-sync press (APC40 mkII SESSION button, or the BAR badge) makes serve.py
send {"flowbeat":1,"type":"one","late":bool} to 127.0.0.1:9101. The beat nearest the press becomes Link's bar 1
(forced for every peer, so a linked Ableton jumps onto it) and stays the reference until the lead deck's track
changes. Works for any track, grid or not (guest USBs included).
Note: with peers in the session Link's request_beat only shifts by whole bars, so a wrong bar position is fixed
with force_beat; whole-beat drift snaps still use request_beat as before.
"""
import asyncio, json, socket, sys, time, urllib.parse, urllib.request

FLOW = "http://127.0.0.1:8717"


def beat_in_bar(grid, pos_s):
    """rekordbox grid runs [[t0_ms, beat_in_bar_at_t0, period_ms, count], ...] + playhead (s) -> 1..4 of the beat
    nearest pos, or None."""
    run = None
    for r in grid or []:
        if isinstance(r, (list, tuple)) and len(r) >= 4 and r[2] > 0 and r[0] <= pos_s * 1000.0 + r[2] / 2:
            run = r
    if not run:
        return None
    t0, b0, period, count = run[:4]
    k = round((pos_s * 1000.0 - t0) / period)
    if k < 0 or k >= count:
        return None
    return int((int(b0) - 1 + k) % 4) + 1


def fetch_grid(d):
    """The FlowVJ server's rekordbox grid for a deck's track (blocking; run in a thread)."""
    q = urllib.parse.urlencode({"id": d.get("id", 0), "k": (d.get("artist", "") + "|" + d.get("title", "")).lower().strip()})
    try:
        with urllib.request.urlopen(f"{FLOW}/deck-waveform?{q}", timeout=20) as r:
            w = json.load(r)
        return w.get("grid") if w.get("available") else None
    except Exception:
        return None

try:
    from aalink import Link
except ImportError:
    sys.exit("aalink is missing: run  python3 -m pip install aalink")

PORT = 9100
ONE_PORT = 9101        # localhost only: "this is 1" from FlowVJ (serve.py /link-one)
QUANTUM = 4
STALE_S = 2.0          # no packet from the deck for this long: hold the last tempo


class Bridge(asyncio.DatagramProtocol):
    def __init__(self, link):
        self.link = link
        self.decks = {}         # d -> {"play", "bpm", "start"}
        self.lead = None
        self.beat_n = 0         # beats counted on the lead deck since it became lead
        self.last_rx = 0.0
        self.titles = {}        # d -> (title, artist)
        self.grids = {}         # (d, title, artist) -> grid runs | None (looked up once)
        self.pending = set()
        self.last_beat_t = 0.0  # monotonic time of the lead deck's last beat packet
        self.one = None         # (deck, title, artist, beat_n of the "1") from a "this is 1" press

    def this_is_one(self, late=None):
        """The beat nearest now is bar 1: force Link there, then count bars from it until the track changes."""
        b = self.link.beat
        lead = self.decks.get(self.lead) or {}
        if late is None:
            late = (b - int(b // 1)) > 0.5
        one = int(-(-b // 1)) if late else int(b // 1)      # the next whole beat, or the last one
        shift = (-one) % 4
        if shift > 2:
            shift -= 4
        if shift:
            self.link.force_beat(b + shift)
        if self.lead is not None and lead.get("play") == 1:
            title, artist = self.titles.get(self.lead, ("", ""))
            self.one = (self.lead, title, artist, self.beat_n + (1 if late else 0))
        else:
            self.one = None
        print(f"this is 1: Link bar reset (moved {shift:+d} beats{', next beat' if late else ''})", flush=True)

    def manual_within(self):
        """0..3 bar position of the lead deck's current beat from a "this is 1" press, or None."""
        if not self.one:
            return None
        d, title, artist, n = self.one
        if d != self.lead or self.titles.get(d, ("", "")) != (title, artist):
            self.one = None
            return None
        return (self.beat_n - n) % 4

    def datagram_received(self, data, addr):
        try:
            m = json.loads(data)
        except ValueError:
            return
        if m.get("flowbeat") != 1:
            return
        self.last_rx = time.monotonic()
        t = m.get("type")
        if t == "one":
            self.this_is_one(m.get("late"))
            return
        if t == "track":
            self.titles[m.get("d")] = (str(m.get("title") or ""), str(m.get("artist") or ""))
        if t == "state":
            for d in m.get("decks", []):
                self.decks[d.get("d")] = d
            self.pick_lead()
            lead = self.decks.get(self.lead)
            if lead and lead.get("bpm", 0) > 20:
                bpm = float(lead["bpm"])
                if abs(self.link.tempo - bpm) > 0.005:
                    self.link.tempo = bpm
        elif t == "beat" and m.get("d") == self.lead:
            # a new beat started on the lead deck right now: snap Link's beat to a whole beat, and when the
            # track's rekordbox grid is known, to the right beat of the bar (Link beat 0, 4, 8... = the "1")
            self.beat_n += 1
            self.last_beat_t = time.monotonic()
            b = self.link.beat
            target = round(b)
            want = self.manual_within()                # a "this is 1" press wins over the grid
            if want is None:
                within = self.within(self.lead)
                want = within - 1 if within else None  # 0..3: where in the bar this beat sits
            if want is not None:
                target = target - ((target - want) % 4) if (target - want) % 4 <= 2 else target + (4 - (target - want) % 4)
            if abs(b - target) >= 0.5:
                self.link.force_beat(target)           # wrong beat of the bar: with peers only force moves it
            elif abs(b - target) > 0.02:
                self.link.request_beat(target)

    def within(self, d):
        """1..4 for deck d's current beat, from its track's grid (looked up in the background once per track)."""
        st = self.decks.get(d) or {}
        title, artist = self.titles.get(d, ("", ""))
        if not title or st.get("pos", -1) is None or st.get("pos", -1) < 0:
            return None
        key = (d, title, artist)
        if key not in self.grids:
            if key not in self.pending:
                self.pending.add(key)
                info = {"id": st.get("id", 0), "title": title, "artist": artist}
                fut = asyncio.get_running_loop().run_in_executor(None, fetch_grid, info)
                fut.add_done_callback(lambda f, k=key: (self.grids.__setitem__(k, f.result()), self.pending.discard(k),
                                                         print(f"grid for deck {k[0]} '{k[1][:40]}': {'found' if f.result() else 'none'}", flush=True)))
            return None
        return beat_in_bar(self.grids[key], float(st["pos"]))

    def pick_lead(self):
        playing = [d for d in self.decks.values() if d.get("play") == 1 and d.get("loaded") == 1]
        if not playing:
            return
        lead = max(playing, key=lambda d: d.get("start", 0)).get("d")
        if lead != self.lead:
            self.lead = lead
            self.beat_n = 0
            self.one = None
            print(f"lead deck {lead}  {self.decks[lead].get('bpm', 0):.2f} BPM", flush=True)


async def main():
    link = Link(120.0)
    link.quantum = QUANTUM
    link.enabled = True
    loop = asyncio.get_running_loop()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    try:
        sock.bind(("", PORT))
    except OSError as e:
        sys.exit(f"port {PORT} is taken ({e}). Quit FlowVJ, start this first, then FlowVJ.")
    bridge = Bridge(link)
    await loop.create_datagram_endpoint(lambda: bridge, sock=sock)
    try:
        await loop.create_datagram_endpoint(lambda: bridge, local_addr=("127.0.0.1", ONE_PORT))
    except OSError as e:
        print(f"'this is 1' port {ONE_PORT} unavailable ({e}); bar reset pad disabled", flush=True)
    print(f"FLOW Link bridge: listening on UDP {PORT}, Link on. Waiting for the deck...", flush=True)
    last_peers = -1
    while True:
        await asyncio.sleep(1.0)
        peers = link.num_peers
        if peers != last_peers:
            print(f"Link peers: {peers}", flush=True)
            last_peers = peers
        if bridge.last_rx and time.monotonic() - bridge.last_rx > STALE_S:
            print("no packets from the deck: holding the last tempo", flush=True)
            bridge.last_rx = 0.0


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass

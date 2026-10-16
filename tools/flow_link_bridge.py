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
"""
import asyncio, json, socket, sys, time

try:
    from aalink import Link
except ImportError:
    sys.exit("aalink is missing: run  python3 -m pip install aalink")

PORT = 9100
QUANTUM = 4
STALE_S = 2.0          # no packet from the deck for this long: hold the last tempo


class Bridge(asyncio.DatagramProtocol):
    def __init__(self, link):
        self.link = link
        self.decks = {}         # d -> {"play", "bpm", "start"}
        self.lead = None
        self.beat_n = 0         # beats counted on the lead deck since it became lead
        self.last_rx = 0.0

    def datagram_received(self, data, addr):
        try:
            m = json.loads(data)
        except ValueError:
            return
        if m.get("flowbeat") != 1:
            return
        self.last_rx = time.monotonic()
        t = m.get("type")
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
            # a new beat started on the lead deck right now: snap Link's beat to a whole beat
            b = self.link.beat
            target = round(b)
            if abs(b - target) > 0.02:
                self.link.request_beat(target)

    def pick_lead(self):
        playing = [d for d in self.decks.values() if d.get("play") == 1 and d.get("loaded") == 1]
        if not playing:
            return
        lead = max(playing, key=lambda d: d.get("start", 0)).get("d")
        if lead != self.lead:
            self.lead = lead
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

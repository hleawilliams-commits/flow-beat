#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Watch the Art-Net the deck sends (flow-beat 0.6.0, "Lights from this deck"), with no lights.

Run it on any computer on the deck's network (or next to the emulator):

    python3 artnet_watch.py              # live colour bars for MLT's rig, frame rate, sender
    python3 artnet_watch.py --raw        # the first 64 channels as numbers instead

Each line is one fixture of the default patch (FLOW VJ's dmx_patch.json addresses), drawn as
true-colour blocks: the 4BAR's four pods, the pars, the two Spectra towers (8 of their 40
pixels), the Shocker's 8 segments. WHITE HIT shows when the drop's hit lands. Ctrl-C stops.

Only listens: it never sends, so the deck never steps back because of it. If FLOW VJ's
lightbridge already listens on port 6454 on this computer, run it on another one.
"""
import socket
import sys
import time

PORT = 6454
RIG = [  # (name, address, kind)
    ("4BAR", 59, "4bar"), ("SlimPAR 1", 74, "slim"), ("SlimPAR 2", 82, "slim"), ("EZpar", 90, "rgb"),
    ("Spectra L", 104, "spectra"), ("Spectra R", 268, "spectra"), ("Shocker", 432, "shock"),
    ("Backdrop", 479, "rgb"),
]


def block(r, g, b, w=2):
    return f"\x1b[48;2;{r};{g};{b}m" + " " * w + "\x1b[0m"


def ch(d, addr, c):
    return d[addr + c - 2]


def draw(d):
    out = []
    for name, a, kind in RIG:
        if kind == "4bar":
            cells = [block(ch(d, a, 4 + 3 * p), ch(d, a, 5 + 3 * p), ch(d, a, 6 + 3 * p), 4) for p in range(4)]
        elif kind == "slim":
            w = ch(d, a, 6)
            cells = [block(min(255, ch(d, a, 2) + w), min(255, ch(d, a, 3) + w), min(255, ch(d, a, 4) + w), 16)]
        elif kind == "rgb":
            cells = [block(ch(d, a, 1), ch(d, a, 2), ch(d, a, 3), 16)]
        elif kind == "spectra":
            cells = []
            for p in range(0, 40, 5):
                w = ch(d, a, 4 * p + 4)
                cells.append(block(min(255, ch(d, a, 4 * p + 1) + w), min(255, ch(d, a, 4 * p + 2) + w),
                                   min(255, ch(d, a, 4 * p + 3) + w)))
        else:
            cells = []
            for k in range(8):
                w = ch(d, a, 39 + k)
                cells.append(block(min(255, ch(d, a, 15 + 3 * k) + w), min(255, ch(d, a, 16 + 3 * k) + w),
                                   min(255, ch(d, a, 17 + 3 * k) + w)))
        out.append(f"{name:<10} " + "".join(cells))
    return out


def main():
    raw = "--raw" in sys.argv
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except OSError:
            pass
    s.bind(("", PORT))
    s.settimeout(1.0)
    print(f"listening for Art-Net on UDP {PORT} ... (turn on Lights from this deck)")
    frames, t_rate, rate, last_draw, hits, was_hit = 0, time.time(), 0.0, 0.0, 0, False
    while True:
        try:
            pkt, src = s.recvfrom(1024)
        except socket.timeout:
            print("\x1b[2K(no Art-Net for 1 s)", end="\r", flush=True)
            continue
        if len(pkt) < 18 or pkt[:8] != b"Art-Net\x00" or pkt[8:10] != b"\x00\x50":
            continue
        uni = ((pkt[15] & 0x7F) << 8) | pkt[14]
        n = (pkt[16] << 8) | pkt[17]
        d = pkt[18:18 + n] + bytes(512 - n)
        frames += 1
        now = time.time()
        if now - t_rate >= 1:
            rate, frames, t_rate = frames / (now - t_rate), 0, now
        hit = all(ch(d, 59, c) == 255 for c in range(4, 16))
        if hit and not was_hit:
            hits += 1
        was_hit = hit
        if now - last_draw < 0.08:
            continue
        last_draw = now
        lines = [f"from {src[0]}  universe {uni}  seq {pkt[12]:3d}  {rate:4.1f} frames/s  drops seen {hits}"
                 + ("   \x1b[1;7m WHITE HIT \x1b[0m" if hit else "")]
        if raw:
            lines += [" ".join(f"{v:3d}" for v in d[i:i + 16]) for i in range(0, 64, 16)]
        else:
            lines += draw(d)
        print("\x1b[H\x1b[2J" + "\n".join(lines), flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass

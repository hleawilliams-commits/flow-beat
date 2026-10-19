#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cut reel clips around Reel Marks.

    python3 reel_cut.py marks-20261024.csv IMG_4410.MOV [more videos...] [options]

Each video's start time comes from its own metadata (creation_time, which phones set to
when recording started). If that's missing or wrong, pass --start "2026-10-24 21:03:15"
(your local time) or nudge with --offset SECONDS (+ if the clip comes out early).

Options:
  --before 20 --after 10   seconds of video kept before / after each mark
  --out DIR                where clips go (default: reel-clips/ next to the marks file)
  --copy                   fast cut without re-encoding (cuts land on keyframes)

Writes one clip per mark that falls inside a video, plus clips.md with the time and the
tracks playing at each mark (for captions and tracklists). Needs ffmpeg and ffprobe.
"""
import argparse, csv, datetime as dt, json, os, re, subprocess, sys
from collections import OrderedDict


def probe(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-print_format", "json", "-show_format", path],
                         capture_output=True, text=True)
    if out.returncode:
        sys.exit(f"ffprobe couldn't read {path}: {out.stderr.strip()}")
    fmt = json.loads(out.stdout)["format"]
    dur = float(fmt.get("duration", 0))
    tags = {k.lower(): v for k, v in (fmt.get("tags") or {}).items()}
    start = None
    for k in ("com.apple.quicktime.creationdate", "creation_time"):
        if k in tags:
            s = tags[k].replace("Z", "+00:00")
            try:
                start = dt.datetime.fromisoformat(s).timestamp()
                break
            except ValueError:
                pass
    return dur, start


def load_marks(paths):
    marks = OrderedDict()
    for p in paths:
        with open(p, newline="") as f:
            for row in csv.DictReader(f):
                t = int(row["utc_epoch"])
                m = marks.setdefault(t, {"t": t, "set_minutes": row.get("set_minutes", ""), "decks": []})
                if row.get("title"):
                    m["decks"].append(row)
    return list(marks.values())


def slug(s):
    return re.sub(r"[^A-Za-z0-9]+", "-", s).strip("-")[:40] or "mark"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("marks", help="marks-<date>.csv from the USB (several allowed, comma-separated)")
    ap.add_argument("videos", nargs="+")
    ap.add_argument("--before", type=float, default=20)
    ap.add_argument("--after", type=float, default=10)
    ap.add_argument("--start", help='video start, local time "YYYY-MM-DD HH:MM:SS" (one video only)')
    ap.add_argument("--offset", type=float, default=0, help="seconds added to every mark")
    ap.add_argument("--out")
    ap.add_argument("--copy", action="store_true")
    a = ap.parse_args()
    marks = load_marks(a.marks.split(","))
    if not marks:
        sys.exit("No marks in that file.")
    out = a.out or os.path.join(os.path.dirname(os.path.abspath(a.marks.split(",")[0])), "reel-clips")
    os.makedirs(out, exist_ok=True)
    lines, made = ["# Reel clips", ""], 0
    for v in a.videos:
        dur, start = probe(v)
        if a.start:
            start = dt.datetime.strptime(a.start, "%Y-%m-%d %H:%M:%S").timestamp()
        if start is None:
            print(f"{v}: no start time in the file; pass --start", file=sys.stderr)
            continue
        for i, m in enumerate(marks, 1):
            at = m["t"] + a.offset - start
            if at < 0 or at > dur:
                continue
            s = max(0.0, at - a.before)
            length = min(dur, at + a.after) - s
            playing = [d for d in m["decks"] if d.get("playing") == "1"] or m["decks"]
            names = " + ".join(d["title"] for d in playing) or "no track"
            local = dt.datetime.fromtimestamp(m["t"]).strftime("%H-%M-%S")
            clip = os.path.join(out, f"mark-{i:02d}_{local}_{slug(names)}.mp4")
            cmd = ["ffmpeg", "-y", "-v", "error", "-ss", f"{s:.2f}", "-i", v, "-t", f"{length:.2f}"]
            cmd += ["-c", "copy"] if a.copy else ["-c:v", "libx264", "-crf", "20", "-preset", "veryfast", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart"]
            r = subprocess.run(cmd + [clip], capture_output=True, text=True)
            if r.returncode:
                print(f"ffmpeg failed for mark {i}: {r.stderr.strip()}", file=sys.stderr)
                continue
            made += 1
            when = dt.datetime.fromtimestamp(m["t"]).strftime("%-I:%M:%S %p")
            lines += [f"## Mark {i} · {when} · {float(m['set_minutes'] or 0):.0f} min into the set",
                      f"`{os.path.basename(clip)}` (from {os.path.basename(v)} at {at:.1f}s)", ""]
            for d in m["decks"]:
                lines.append(f"- Deck {d['deck']}: {d['title']} - {d['artist']} · {float(d['bpm']):.1f} BPM"
                             f"{'' if d.get('playing') == '1' else ' (cued)'}")
            lines.append("")
            print(f"mark {i}: {os.path.basename(clip)}")
    with open(os.path.join(out, "clips.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{made} clip(s) in {out}" + ("" if made else " (no mark fell inside the videos: check --start/--offset)"))


if __name__ == "__main__":
    main()

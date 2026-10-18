#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Find the tracks whose stems the XDJ-AZ will not load, and say why.

Read-only: opens files for reading and lists folders, never writes to the USB.

    python3 stems_check.py /Volumes/FLOWMATIC            # summary + problems
    python3 stems_check.py /Volumes/FLOWMATIC --csv out.csv --all

It follows AZ-MODS core 3e0c5cb (src/azstem/stem_playback.c):
  key      = the audio file's name minus its last extension        (get_loaded_track_name)
  folder   = STEMS/<key>, <key>_ or <key>-xxxxxxxx, ASCII case only (stem_folder_rank)
  identity = source.json file/size/fp vs the USB file               (stem_folder_identity)
  pick     = proven beats unknown, then rank; "different file" is never loaded
  stems    = *.ogg named vocal / inst|other / bass / drum, dot files skipped
  limits   = loaded path cut at 299 bytes, track name at 255 bytes
Deck tracks come from the rekordbox export, which this does not decrypt; it checks every
audio file under Contents/, which is where rekordbox exports them.
"""
import argparse, csv, json, os, sys, unicodedata
from pathlib import Path

AUDIO = {".mp3", ".flac", ".wav", ".aif", ".aiff", ".m4a", ".mp4", ".aac", ".alac", ".ogg"}
DECK_PREFIX = "/media/usb/sda1/"   # the deck's path to a USB file; only its length matters here
PATH_MAX_DECK = 299                 # src/azmod/track_list_rows.c: 300-byte buffer
NAME_MAX_DECK = 255                 # get_loaded_track_name: 256-byte buffer
SLOTS = ("vocals", "inst/other", "bass", "drums")


def ascii_lower(b: bytes) -> bytes:
    return bytes(c + 32 if 65 <= c <= 90 else c for c in b)


def folder_rank(name: bytes, key: bytes) -> int:
    """stem_folder_rank: 0 exact, 1 trailing _, 2 -8hex, -1 not this track's."""
    if not key or ascii_lower(name[:len(key)]) != ascii_lower(key):
        return -1
    t = name[len(key):]
    if not t:
        return 0
    if t == b"_":
        return 1
    if len(t) == 9 and t[:1] == b"-" and all(c in b"0123456789abcdefABCDEF" for c in t[1:]):
        return 2
    return -1


def fingerprint(path: Path) -> int:
    """docs/STEMS-FORMAT.md: FNV-1a 64 over size, head, middle, tail."""
    with open(path, "rb") as f:
        size = f.seek(0, 2)
        h = 0xCBF29CE484222325
        chunks = [f"{size}\n".encode()]
        for off in (0, max(0, size // 2 - 32768), max(0, size - 65536)):
            f.seek(off)
            chunks.append(f.read(65536))
    for data in chunks:
        for b in data:
            h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def parse_fp(v):
    if isinstance(v, str) and v.lower().startswith("fnv1a64:"):
        try:
            return int(v[8:], 16)
        except ValueError:
            return None
    return None


def identity(folder: Path, deck_path: str, size: int, audio: Path):
    """stem_folder_identity -> (1 proven | 0 unknown | -1 different file, reason)."""
    sj = folder / "source.json"
    try:
        raw = sj.read_bytes()[:4095]
    except OSError:
        return 0, "no source.json (loads by name)"
    try:
        j = json.loads(raw.decode("utf-8"))
        if not isinstance(j, dict):
            raise ValueError
    except (ValueError, UnicodeDecodeError):
        return 0, "source.json unreadable (loads by name)"
    have_size = isinstance(j.get("size"), int)
    f = j.get("file")
    have_file = isinstance(f, str) and f != ""
    fp = parse_fp(j.get("fp"))
    same_path = False
    if have_file:
        fl, sl = f.encode(), deck_path.encode()
        same_path = (len(sl) > len(fl) and sl[len(sl) - len(fl) - 1:len(sl) - len(fl)] == b"/"
                     and ascii_lower(sl[len(sl) - len(fl):]) == ascii_lower(fl))
    if same_path and have_size and j["size"] == size:
        return 1, "source.json matches path and size"
    if fp is not None:
        mine = fingerprint(audio)
        if mine == fp:
            return 1, "fingerprint matches"
        return -1, (f"fingerprint in source.json ({fp:016x}) is not this file's ({mine:016x}): "
                    "stems were made from a different copy")
    if (have_size and j["size"] == size) or same_path:
        return 1, "source.json matches " + ("size" if have_size and j["size"] == size else "path")
    if have_size or have_file:
        bits = []
        if have_size:
            bits.append(f"size {j['size']} vs USB file {size}")
        if have_file:
            bits.append(f"file '{f}' vs '{deck_path[len(DECK_PREFIX):]}'")
        return -1, "source.json says a different file: " + "; ".join(bits)
    return 0, "source.json has no file/size/fp (loads by name)"


def stem_files(folder: Path):
    """Which slots the deck would fill, plus problems it would hit."""
    got, notes, dots = [None] * 4, [], 0
    try:
        names = sorted(os.listdir(folder))
    except OSError as e:
        return got, [f"can't list folder: {e}"], 0
    for n in names:
        if n.startswith("."):
            dots += n.startswith("._")
            continue
        if not n.lower().endswith(".ogg"):
            continue
        l = n.lower()
        slot = 0 if "vocal" in l else 1 if ("instrument" in l or "inst" in l or "other" in l) else \
            2 if "bass" in l else 3 if "drum" in l else -1
        if slot < 0:
            notes.append(f"'{n}' matches no stem slot")
            continue
        if got[slot] is None:
            p = folder / n
            sz = p.stat().st_size
            got[slot] = (n, sz)
            if sz < 8192:
                notes.append(f"'{n}' is tiny ({sz} bytes): likely broken")
            else:
                with open(p, "rb") as fh:
                    if fh.read(4) != b"OggS":
                        notes.append(f"'{n}' isn't an Ogg file")
    return got, notes, dots


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("usb", type=Path, help="USB root, e.g. /Volumes/FLOWMATIC")
    ap.add_argument("--csv", type=Path, help="write every result to this CSV (not on the USB)")
    ap.add_argument("--all", action="store_true", help="also list tracks that are fine")
    a = ap.parse_args()
    usb = a.usb.resolve()
    if a.csv and usb in a.csv.resolve().parents:
        sys.exit("Write the CSV somewhere other than the USB; this tool never writes to it.")
    stems_dir = next((usb / n for n in os.listdir(usb)
                      if n.lower() == "stems" and not n.startswith(".") and (usb / n).is_dir()), None)
    contents = usb / "Contents"
    if not contents.is_dir():
        sys.exit(f"No Contents/ folder on {usb}: is this a rekordbox USB?")
    folders = []
    if stems_dir:
        folders = [n for n in os.listdir(stems_dir) if not n.startswith(".") and (stems_dir / n).is_dir()]
    fbytes = [(n, os.fsencode(n)) for n in folders]
    nfc_index = {}
    for n in folders:
        nfc_index.setdefault(unicodedata.normalize("NFC", n).casefold(), []).append(n)
    leftovers = [n for n in folders if n.endswith(".part") or ".azmods-" in n or ".azold-" in n]

    rows, used = [], set()
    tracks = sorted(p for p in contents.rglob("*")
                    if p.suffix.lower() in AUDIO and not p.name.startswith(".") and p.is_file())
    for audio in tracks:
        rel = audio.relative_to(usb).as_posix()
        deck_path = DECK_PREFIX + rel
        key = audio.stem
        kb = os.fsencode(key)
        size = audio.stat().st_size
        row = {"track": rel, "status": "", "reason": "", "folder": ""}
        problems = []
        if len(deck_path.encode()) > PATH_MAX_DECK:
            problems.append(f"path is {len(deck_path.encode())} bytes on the deck (limit {PATH_MAX_DECK}): "
                            "the deck reads a cut-off name, shorten artist/album/file names")
        if len(kb) > NAME_MAX_DECK:
            problems.append(f"file name is {len(kb)} bytes (limit {NAME_MAX_DECK})")
        cands = [(folder_rank(b, kb), n) for n, b in fbytes]
        cands = [(r, n) for r, n in cands if r >= 0]
        best, rejected = None, []
        for r, n in cands:
            used.add(n)
            idv, why = identity(stems_dir / n, deck_path, size, audio)
            if idv < 0:
                rejected.append(f"'{n}': {why}")
                continue
            score = (0 if idv else 1000) + r * 10
            if best is None or score < best[0]:
                best = (score, n, idv, why)
        if not stems_dir:
            row["status"], row["reason"] = "NO STEMS FOLDER", "the USB has no STEMS/ folder"
        elif best is None and rejected:
            row["status"], row["reason"] = "REJECTED", " | ".join(rejected)
        elif best is None:
            near = [n for n in nfc_index.get(unicodedata.normalize("NFC", key).casefold(), [])]
            loose = [n for n in folders if n.lower().startswith(key.lower()) and n not in near][:3]
            if near:
                row["status"] = "NAME MISMATCH"
                row["reason"] = (f"folder '{near[0]}' looks the same but its letters are encoded "
                                 "differently (accents/Unicode) or differ beyond capitals; rename it to "
                                 "match the file name exactly")
            elif loose:
                row["status"] = "NAME MISMATCH"
                row["reason"] = (f"closest folder '{loose[0]}' has extra text; the deck only accepts "
                                 "<name>, <name>_ or <name>-xxxxxxxx")
            else:
                row["status"], row["reason"] = "NO STEMS", "no stems folder for this track"
        else:
            _, n, idv, why = best
            row["folder"] = n
            got, notes, dots = stem_files(stems_dir / n)
            missing = [SLOTS[i] for i, g in enumerate(got) if g is None]
            if all(g is None for g in got):
                row["status"], row["reason"] = "EMPTY FOLDER", "folder has no usable .ogg stems" + \
                    (" (an emptier folder wins over a good one: check duplicates)" if len(cands) > 1 else "")
            else:
                extra = []
                if missing:
                    extra.append("missing " + ", ".join(missing))
                extra += notes
                if dots:
                    extra.append(f"{dots} Mac '._' files (harmless on the deck; safe to delete)")
                if rejected:
                    extra.append("also rejected: " + " | ".join(rejected))
                row["status"] = "OK" if not (missing or notes) else "PARTIAL"
                row["reason"] = why + ("; " + "; ".join(extra) if extra else "")
        if problems:
            row["status"] = row["status"] if row["status"] not in ("OK",) else "AT RISK"
            row["reason"] = "; ".join(problems) + ("; " + row["reason"] if row["reason"] else "")
        rows.append(row)

    orphans = [n for n in folders if n not in used and n not in leftovers]
    counts = {}
    for r in rows:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    print(f"USB: {usb}\nTracks checked: {len(rows)}   Stems folders: {len(folders)}")
    for k in ("OK", "PARTIAL", "AT RISK", "REJECTED", "NAME MISMATCH", "EMPTY FOLDER", "NO STEMS", "NO STEMS FOLDER"):
        if counts.get(k):
            print(f"  {k:<15} {counts[k]}")
    print(f"  stems folders matching no track: {len(orphans)}" + (f"   leftover temp folders: {len(leftovers)}" if leftovers else ""))
    show = [r for r in rows if a.all or r["status"] not in ("OK", "NO STEMS")]
    if show:
        print("\nTracks to look at:")
        for r in show:
            print(f"- [{r['status']}] {r['track']}\n    {r['reason']}")
    if orphans:
        print("\nStems folders with no matching track (renamed or deleted songs?):")
        for n in orphans[:40]:
            print(f"  {n}")
        if len(orphans) > 40:
            print(f"  ... and {len(orphans) - 40} more (see --csv)")
    if a.csv:
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=["status", "track", "folder", "reason"])
            w.writeheader()
            w.writerows(rows)
            for n in orphans:
                w.writerow({"status": "ORPHAN FOLDER", "track": "", "folder": n, "reason": "no track with this name"})
        print(f"\nFull results: {a.csv}")
    print("\nAfter fixing anything: replug the USB and load a different track first; "
          "the deck remembers a 'no stems' answer for the track that's loaded.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract actual pinned functions; compare original retry against the follow-up.

Uses only a temporary checkout. Never contacts or changes a deck or emulator.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

BASE = "3e0c5cb010454228f4ee63226b60d060494eeaae"
HERE = Path(__file__).resolve().parent

def function(text, name):
    m = re.search(r"^static [^\n]*\b" + re.escape(name) + r"\([^;]*?\)\s*\{", text, re.M)
    if not m:
        raise RuntimeError("missing production function " + name)
    begin = text.index("{", m.start())
    depth = 1
    end = begin + 1
    while depth:
        if text[end] == "{": depth += 1
        elif text[end] == "}": depth -= 1
        end += 1
    return text[m.start():end] + "\n"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", type=Path, required=True)
    ap.add_argument("--baseline-only", action="store_true")
    args = ap.parse_args()
    upstream = args.upstream.resolve()
    with tempfile.TemporaryDirectory(prefix="stem-activity-") as temp:
        root = Path(temp)
        for rel in ["src/azstem/stem_playback.c", "src/azstem/key_shift.c",
                    "src/azstem/azstem.c", "tests/unit/run.sh", "tests/unit/trackgen_test.c", "tests/unit/check.h"]:
            p = root / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(subprocess.check_output(["git", "show", BASE + ":" + rel], cwd=upstream))
        (root / "tests/unit/run.sh").chmod(0o755)
        subprocess.run(["git", "apply", str(HERE.parent / "stems-memory-retry/memory-retry.patch")], cwd=root, check=True)
        baseline = (root / "src/azstem/stem_playback.c").read_text()
        (root / "baseline.inc").write_text(function(baseline, "memory_retry_ready"))
        cc = os.environ.get("CC", "cc")
        flags = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g"]
        subprocess.run(flags + ["-DBASELINE", '-DRETRY_INC="' + str(root / "baseline.inc") + '"',
                               str(HERE / "test_activity.c"), "-o", str(root / "baseline")], check=True)
        control = subprocess.run([str(root / "baseline")], capture_output=True, text=True)
        print("Original retry control (expected failure):")
        print(control.stdout + control.stderr, end="")
        if control.returncode == 0 or "8 failed" not in control.stdout:
            raise RuntimeError("original retry did not expose all four loop and stale cases")
        if args.baseline_only:
            return
        subprocess.run(["git", "apply", str(HERE / "activity-followup.patch")], cwd=root, check=True)
        playback = (root / "src/azstem/stem_playback.c").read_text()
        key = (root / "src/azstem/key_shift.c").read_text()
        (root / "retry.inc").write_text(function(playback, "memory_retry_ready"))
        (root / "observer.inc").write_text(function(key, "stem_play_observe") + function(key, "stem_retry_play_activity"))
        (root / "trackgen.inc").write_text(function(playback, "deck_track_changed") +
                                           function(playback, "memory_retry_ready") +
                                           function(playback, "stems_tick"))
        # Verify the real audio hook publishes through the helper, not only test calls.
        assert "if (playhead_obs_ok(Sobs))\n                stem_play_observe(di, Sobs);" in key
        assert "g_retry_activity[deck] = 0;" in playback
        for mode, extra in [("plain", []), ("asan-ubsan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]:
            exe = root / mode
            subprocess.run(flags + extra + ['-DRETRY_INC="' + str(root / "retry.inc") + '"',
                            '-DOBSERVER_INC="' + str(root / "observer.inc") + '"',
                            str(HERE / "test_activity.c"), "-o", str(exe)], check=True)
            print("Follow-up " + mode + ":", flush=True)
            subprocess.run([str(exe)], check=True)
            regression = root / (mode + "-trackgen")
            subprocess.run(flags + extra + ["-Wno-unused-parameter", '-DTICK_INC="' + str(root / "trackgen.inc") + '"',
                            str(root / "tests/unit/trackgen_test.c"), "-o", str(regression)], check=True)
            result = subprocess.run([str(regression)], capture_output=True, text=True, check=True)
            print(result.stdout.splitlines()[-1])

if __name__ == "__main__":
    main()

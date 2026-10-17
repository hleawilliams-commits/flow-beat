#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce recovery publication with actual pinned production functions.

Reads only Git objects from --upstream; applies preceding patches in a temporary
directory. No checkout, emulator, physical device, or published binary changes.
"""
import argparse
import hashlib
import json
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
    depth, end = 1, begin + 1
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[m.start():end] + "\n"


def typedef(text, name):
    pattern = r"typedef struct \{[^}]*\} " + re.escape(name) + ";"
    match = re.search(pattern, text)
    if not match:
        raise RuntimeError("missing production type " + name)
    return match.group() + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", type=Path, required=True)
    ap.add_argument("--plain-only", action="store_true")
    ap.add_argument("--manifest", type=Path,
                    help="write portable source/extraction hashes to this JSON path")
    args = ap.parse_args()
    upstream = args.upstream.resolve()
    paths = ["src/azstem/azstem.c", "src/azstem/stem_playback.c",
             "src/azstem/stem_envelopes.c", "src/azstem/key_shift.c",
             "src/azstem/ofsload.c", "src/azstem/azstem_internal.h",
             "src/azstem/ep147rate.h",
             "tests/unit/run.sh", "tests/unit/trackgen_test.c", "tests/unit/check.h"]
    with tempfile.TemporaryDirectory(prefix="stem-publication-") as temp:
        root = Path(temp)
        for rel in paths:
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(subprocess.check_output(["git", "show", BASE + ":" + rel], cwd=upstream))
        (root / "tests/unit/run.sh").chmod(0o755)
        patches = [HERE.parent / "stems-memory-retry/memory-retry.patch",
                   HERE.parent / "stems-retry-activity/activity-followup.patch"]
        for patch in patches:
            subprocess.run(["git", "apply", str(patch)], cwd=root, check=True)
        source = {Path(p).name: (root / p).read_text() for p in paths}
        playback = source["stem_playback.c"]
        key = source["key_shift.c"]
        envelopes = source["stem_envelopes.c"]
        constant_sources = {
            "azstem_internal.h": ["NDECK", "NSTEM"],
            "ep147rate.h": ["AZ_EP147_RATE"],
            "stem_envelopes.c": ["XF_ORIG"],
            "stem_playback.c": ["OFS_N", "AZSTEM_MEM_FLOOR_MB"],
            "key_shift.c": ["POS_SAMPLE_OFF", "PLAYHEAD_MAX"],
        }
        constants = "".join(re.search(r"^#define " + name + r"\s+[^\n]+", source[path], re.M).group() + "\n"
                            for path, names in constant_sources.items() for name in names)
        constants += re.search(r"enum \{\s*OFS_IDLE,[^}]*\};", playback).group() + "\n"
        (root / "constants.inc").write_text(constants)
        # Copy the literal offset completion block; its calculation/decoder is
        # deliberately not faked as a tested production offset measurement.
        ofs = function(source["ofsload.c"], "ofs_load_measure")
        marker = "    /* Open the gate, unless the track was replaced meanwhile (drop_stems): then"
        release = ofs[ofs.index(marker):ofs.rfind("}")]
        assert release.count("advance_stem_frames(deck)") == 1
        assert "g_load_stale[deck]" in release and "g_gate_hold[deck]" in release
        # Preserve the actual render-before-observer order. Only the enclosing
        # function is renamed; its prefix is copied without edits. This fixture
        # does not claim to execute the later mix/mute/meter portion of hook_pbi.
        complete_hook = function(key, "hook_pbi")
        hook = complete_hook[:complete_hook.index("#ifdef AZSTEM_PCMCAP")]
        assert complete_hook.count("g_orig(self, buf, midi);") == 1
        assert complete_hook.count("stem_play_observe(di, Sobs);") == 1
        assert hook.index("g_orig(self, buf, midi);") < hook.index("stem_play_observe(di, Sobs);")
        hook = hook.replace("static void hook_pbi(", "static void hook_pbi_observation_prefix(", 1) + "}\n"
        snippets = (typedef(source["azstem_internal.h"], "deck_stems") +
                    typedef(source["azstem_internal.h"], "Float2") +
                    typedef(source["azstem.c"], "Pos32"))
        (root / "types.inc").write_text(snippets)
        snippets = (function(key, "stem_play_observe") +
                    function(key, "stem_retry_play_activity") +
                    function(key, "playhead_obs_ok") +
                    function(playback, "memory_retry_ready") +
                    function(playback, "advance_stem_frames") +
                    function(playback, "deck_track_changed") +
                    function(playback, "drop_stems") +
                    function(envelopes, "stem_gains") +
                    function(envelopes, "stems_play_original") +
                    function(key, "pbread_hook") +
                    hook +
                    "static void offset_release_block(int deck) {\n" + release + "}\n")
        (root / "production.inc").write_text(snippets)
        functions = {
            "key_shift.c": ["stem_play_observe", "stem_retry_play_activity", "playhead_obs_ok", "pbread_hook", "hook_pbi"],
            "stem_playback.c": ["memory_retry_ready", "advance_stem_frames", "deck_track_changed", "drop_stems"],
            "stem_envelopes.c": ["stem_gains", "stems_play_original"],
            "ofsload.c": ["ofs_load_measure"],
        }
        digest = lambda text: hashlib.sha256(text.encode()).hexdigest()
        if args.manifest:
            manifest = {
                "upstream_commit": BASE,
                "patches_in_order": [{"path": str(p.relative_to(HERE.parent.parent)),
                                      "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in patches],
                "patched_source_files": {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths},
                "complete_function_sha256": {name + ":" + fn: digest(function(source[name], fn))
                                              for name, names in functions.items() for fn in names},
                "compiled_production_inc_sha256": digest(snippets),
                "compiled_types_inc_sha256": hashlib.sha256((root / "types.inc").read_bytes()).hexdigest(),
                "compiled_constants_inc_sha256": digest(constants),
                "production_constants_extracted_unchanged": constant_sources,
                "fixture_constants": {
                    "BLOCK": 2048, "FRAMES": 4096,
                    "MAX_DECKS": "NDECK: test only the four stem decks",
                    "OFF_POS_PTR/OFF_POS/OFF_NUMCH/OFF_NUMSAMP/OFF_CHANS": "host offsetof NativeDeck/NativeBuffer; not firmware offsets",
                },
                "partial_blocks": {
                    "hook_pbi": "unchanged prefix through stem_play_observe; enclosing function renamed",
                    "ofs_load_measure": "unchanged final gate-release block in fixture wrapper",
                },
                "modified_production_files": [],
            }
            args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
        (root / "trackgen.inc").write_text(function(playback, "deck_track_changed") +
                                           function(playback, "memory_retry_ready") +
                                           function(playback, "stems_tick"))
        print("Pinned upstream: " + BASE, flush=True)
        print("Complete pbread_hook extracted unchanged; offset completion block extracted unchanged.", flush=True)
        print("hook_pbi prefix extracted through observation: native render precedes activity publish.", flush=True)
        print("Extracted production SHA-256: " + hashlib.sha256(snippets.encode()).hexdigest(), flush=True)
        cc = os.environ.get("CC", "cc")
        flags = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-pthread"]
        modes = [("plain", [])]
        if not args.plain_only:
            modes.extend([("asan-ubsan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]),
                          ("tsan", ["-fsanitize=thread", "-fno-omit-frame-pointer"])])
        for mode, extra in modes:
            exe = root / mode
            subprocess.run(flags + extra + ['-DTYPES_INC="' + str(root / "types.inc") + '"',
                            '-DCONSTANTS_INC="' + str(root / "constants.inc") + '"',
                            '-DPRODUCTION_INC="' + str(root / "production.inc") + '"',
                            str(HERE / "test_publication.c"), "-lm", "-o", str(exe)], check=True)
            print("\n" + mode + ":", flush=True)
            subprocess.run([str(exe)], check=True)
            # Preserve the earlier actual stems_tick identity/ejection suite.
            regression = root / (mode + "-trackgen")
            subprocess.run(flags + extra + ["-Wno-unused-parameter", '-DTICK_INC="' + str(root / "trackgen.inc") + '"',
                            str(root / "tests/unit/trackgen_test.c"), "-o", str(regression)], check=True)
            result = subprocess.run([str(regression)], capture_output=True, text=True, check=True)
            print("Existing production stems_tick identity/retry suite: " + result.stdout.splitlines()[-1], flush=True)


if __name__ == "__main__":
    main()

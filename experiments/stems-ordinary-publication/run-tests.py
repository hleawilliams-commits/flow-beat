#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pinned ordinary producer contract; real worker code, synthetic codec inputs."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

BASE = "3e0c5cb010454228f4ee63226b60d060494eeaae"
HERE = Path(__file__).resolve().parent


def function(source, name):
    match = re.search(r"^(?:static )?[^\n;]*\b" + re.escape(name) + r"\([^;]*?\)\s*\{", source, re.M)
    if not match:
        raise RuntimeError("missing production function " + name)
    begin = source.index("{", match.start())
    depth, end = 1, begin + 1
    while depth:
        if source[end] == "{": depth += 1
        elif source[end] == "}": depth -= 1
        end += 1
    return source[match.start():end] + "\n"


def typedef(source, name):
    return re.search(r"typedef struct \{[^}]*\} " + re.escape(name) + ";", source).group() + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", required=True, type=Path)
    ap.add_argument("--baseline", action="store_true", help="pinned upstream without candidate")
    ap.add_argument("--plain-only", action="store_true")
    ap.add_argument("--manifest", type=Path)
    args = ap.parse_args()
    names = ["azstem.c", "stem_playback.c", "stem_envelopes.c", "ofsload.c", "key_shift.c", "azstem_internal.h", "ep147rate.h"]
    with tempfile.TemporaryDirectory(prefix="ordinary-stem-contract-") as directory:
        root = Path(directory)
        for name in names:
            path = root / "src/azstem" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(subprocess.check_output(["git", "show", BASE + ":src/azstem/" + name], cwd=args.upstream))
        if not args.baseline:
            subprocess.run(["git", "apply", str(HERE / "ordinary-publication.patch")], cwd=root, check=True)
        src = {name: (root / "src/azstem" / name).read_text() for name in names}
        pb, env, ofs, key = [src[n] for n in ["stem_playback.c", "stem_envelopes.c", "ofsload.c", "key_shift.c"]]
        constants = {"azstem_internal.h": ["NDECK", "NSTEM", "AZW_N"], "ep147rate.h": ["AZ_EP147_RATE"],
                     "stem_envelopes.c": ["XF_ORIG", "ENV_MAGIC", "ENV_MAX_FRAMES"],
                     "stem_playback.c": ["OFS_N", "AZSTEM_MEM_FLOOR_MB", "PROG_CHUNK", "EJECT_EMPTY_TICKS"],
                     "key_shift.c": ["POS_SAMPLE_OFF", "PLAYHEAD_MAX"]}
        defs = "".join(re.search(r"^#define " + n + r"\s+[^\n]+", src[file], re.M).group() + "\n"
                       for file, values in constants.items() for n in values)
        defs += re.search(r"enum \{\s*OFS_IDLE,[^}]*\};", pb).group() + "\n"
        (root / "constants.inc").write_text(defs)
        (root / "types.inc").write_text(typedef(src["azstem_internal.h"], "deck_stems") +
                                       typedef(src["azstem_internal.h"], "Float2") + typedef(src["azstem.c"], "Pos32"))
        fixture = (HERE.parent / "stems-retry-publication/test_publication.c").read_text().split("#include PRODUCTION_INC")[0]
        for before, after in [("#define FRAMES 4096", "#define FRAMES (3 * PROG_CHUNK + 137)"),
                              ("static void env_progress(int deck) { (void)deck; }", "static void env_progress(int deck);"),
                              ("static void free_stems(int d) { g_dk[d].stem_frames = 0; }", "static void free_stems(int d);")]:
            assert fixture.count(before) == 1
            fixture = fixture.replace(before, after)
        (root / "fixture.inc").write_text(fixture)
        (root / "job-types.inc").write_text(typedef(pb, "pjob_t") + typedef(pb, "loadjob_t"))
        groups = [(env, ["free_stems", "env_cache_path", "env_cache_save", "env_progress", "env_cache_load", "build_env", "azstem_env_gen", "azstem_env_final", "azstem_frames", "azstem_wave"]),
                  (pb, (["publish_stem_prefix"] if not args.baseline else []) + ["azstem_present", "advance_stem_frames", "decode_stem_progressive", "dth_prog"]),
                  (ofs, ["ofs_stems_decoded"]), (key, ["ofs_stem_mono_lim"])]
        production = "".join(function(s, n) for s, names in groups for n in names)
        tail = function(ofs, "ofs_load_measure")
        tail = tail[tail.index("out:\n") + len("out:\n"):tail.rfind("}")]
        production += "static void ofs_load_measure(int deck,const char *track,const char *src) {\n" + \
                      "fixture_offset_boundary(deck); (void)src;deck_stems *dk=&g_dk[deck];\n" + \
                      "int apply=1,rate=44100;long lag=17,t0=0;const char *kind=\"fixture\",*vendor=\"fixture\",*why=NULL;\n" + \
                      "long long base=0;double fine=1,coarse=1,second=0;float *x=NULL,*nat=NULL;\n" + tail + "}\n"
        rest = [(pb, ["load_stems_ogg", "loader_thread", "deck_track_changed", "drop_stems", "start_load"])]
        production += "".join(function(s, n) for s, names in rest for n in names)
        (root / "production.inc").write_text(production)
        manifest = {"base": BASE, "patch_order": [] if args.baseline else ["ordinary-publication.patch"],
                    "patch_sha256": None if args.baseline else hashlib.sha256((HERE / "ordinary-publication.patch").read_bytes()).hexdigest(),
                    "sources": {n: hashlib.sha256(src[n].encode()).hexdigest() for n in names},
                    "functions": [n for _, names in groups + rest for n in names],
                    "production_sha256": hashlib.sha256(production.encode()).hexdigest(),
                    "fixture_sha256": hashlib.sha256(fixture.encode()).hexdigest(),
                    "partial_function": "ofs_load_measure actual out tail only; no codec/calibration work"}
        print(json.dumps({k: manifest[k] for k in ["base", "patch_order", "patch_sha256", "production_sha256"]}), flush=True)
        if args.manifest: args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
        flags = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-O1", "-g", "-pthread", "-DCANDIDATE=" + str(int(not args.baseline))]
        for key, file in {"CONSTANTS_INC": "constants.inc", "TYPES_INC": "types.inc", "FIXTURE_INC": "fixture.inc", "RECOVERY_DECLARATIONS_INC": "job-types.inc", "PRODUCTION_INC": "production.inc"}.items():
            flags.append('-D' + key + '="' + str(root / file) + '"')
        modes = [("plain", [])] if args.plain_only else [("plain", []), ("asan-ubsan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]), ("tsan", ["-fsanitize=thread", "-fno-omit-frame-pointer"])]
        for mode, extra in modes:
            exe = root / mode
            subprocess.run(flags + extra + [str(HERE / "test_ordinary.c"), "-lm", "-o", str(exe)], check=True)
            print(mode + ":", flush=True)
            result = subprocess.run([str(exe)], text=True, capture_output=True, timeout=90)
            output = (result.stdout + result.stderr).replace(str(root), "<TEMP>")
            print(output, end="", flush=True)
            if args.baseline and mode == "tsan":
                expected = ("ThreadSanitizer: data race" in output and
                            "decode_stem_progressive" in output and
                            ("env_progress" in output or "advance_stem_frames" in output))
                if not expected or "0 failed" not in output or "FAIL:" in output:
                    raise RuntimeError("baseline lacks expected producer race with zero functional failures")
                print("Baseline TSan race reproduced; no candidate fix applied.", flush=True)
            elif result.returncode:
                raise RuntimeError(mode + " failed: " + str(result.returncode))


if __name__ == "__main__":
    main()

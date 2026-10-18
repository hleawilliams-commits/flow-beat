#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host-only composition; extract actual functions from the exact ordered stack."""
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
FROZEN = [
    ("stems-memory-retry/memory-retry.patch", "5905b0f514cf33e2dafbbdecccf18125e5352108515eded6b424f4dbc6138f1b"),
    ("stems-retry-activity/activity-followup.patch", "69ed45bf1d8b645a574379be0c8b1389eaaec2b919e8de1529d6e75f6751c30c"),
    ("stems-recovery-staging/staging-only.patch", "c8d8122ec0af65ebd478ebea1fedbb5b030c77ac186d2d9a5c7818ae01d6a03b"),
]


def digest(value):
    return hashlib.sha256(value).hexdigest()


def function(text, name):
    match = re.search(r"^(?:static )?[^\n;]*\b" + re.escape(name) + r"\([^;]*?\)\s*\{", text, re.M)
    if not match:
        raise RuntimeError("missing production function " + name)
    begin = text.index("{", match.start())
    depth, end = 1, begin + 1
    while depth:
        if text[end] == "{": depth += 1
        elif text[end] == "}": depth -= 1
        end += 1
    return text[match.start():end] + "\n"


def typedef(text, name):
    return re.search(r"typedef struct \{[^}]*\} " + re.escape(name) + ";", text).group() + "\n"


def once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", required=True, type=Path)
    ap.add_argument("--plain-only", action="store_true")
    ap.add_argument("--manifest", type=Path)
    ap.add_argument("--suite", choices=["both", "ordinary", "staging"], default="both")
    ap.add_argument("--control", choices=["none", "helper-guard-removed", "helper-check-before-lock", "callsite-guard-removed"], default="none",
                    help="named negative controls in the temporary extraction, never patch files")
    args = ap.parse_args()
    if args.control != "none" and args.suite != "staging":
        ap.error("negative controls require --suite staging")
    names = ["azstem.c", "stem_playback.c", "stem_envelopes.c", "ofsload.c", "key_shift.c", "azstem_internal.h", "ep147rate.h"]
    files = ["src/azstem/" + n for n in names] + ["tests/unit/" + n for n in ["run.sh", "trackgen_test.c", "check.h"]]
    patches = []
    for rel, expected in FROZEN:
        path = HERE.parent / rel
        assert digest(path.read_bytes()) == expected, "frozen patch changed: " + rel
        patches.append(path)
    patches.append(HERE / "composed-publication.patch")
    standalone = HERE.parent / "stems-ordinary-publication/ordinary-publication.patch"
    assert digest(standalone.read_bytes()) == "b4ff1224d1f0faaa4e248d564d9a053877b644e277eb2d5d0ebe011623501a34"
    prior_staging_test = HERE.parent / "stems-recovery-staging/test_staging.c"
    current_staging_test = HERE / "test_staging.c"
    original_restart = function(prior_staging_test.read_text(), "completion_boundary")
    assert original_restart == function(current_staging_test.read_text(), "completion_boundary"), \
        "the frozen original completion/restart test must remain unchanged"
    with tempfile.TemporaryDirectory(prefix="stems-compose-host-") as directory:
        root = Path(directory)
        for rel in files:
            p = root / rel; p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(subprocess.check_output(["git", "show", BASE + ":" + rel], cwd=args.upstream))
        (root / "tests/unit/run.sh").chmod(0o755)
        for path in patches:
            subprocess.run(["git", "apply", str(path)], cwd=root, check=True)
        src = {name: (root / "src/azstem" / name).read_text() for name in names}
        pb, env, ofs, key = [src[n] for n in ["stem_playback.c", "stem_envelopes.c", "ofsload.c", "key_shift.c"]]
        helper = function(pb, "publish_stem_prefix")
        assert helper.index("pthread_mutex_lock(&g_adv_lock);") < helper.index("if (!recovery_hidden(deck) &&")
        final_call = "    if (!recovery)\n        publish_stem_prefix(deck, ofs_stems_decoded(deck));"
        assert final_call in function(pb, "load_stems_ogg")
        constants = {"azstem_internal.h": ["NDECK", "NSTEM", "AZW_N"], "ep147rate.h": ["AZ_EP147_RATE"],
                     "stem_envelopes.c": ["XF_ORIG", "ENV_MAGIC", "ENV_MAX_FRAMES"],
                     "stem_playback.c": ["OFS_N", "AZSTEM_MEM_FLOOR_MB", "PROG_CHUNK", "EJECT_EMPTY_TICKS"],
                     "key_shift.c": ["POS_SAMPLE_OFF", "PLAYHEAD_MAX"]}
        defs = "".join(re.search(r"^#define " + n + r"\s+[^\n]+", src[f], re.M).group() + "\n"
                       for f, values in constants.items() for n in values)
        defs += re.search(r"enum \{\s*OFS_IDLE,[^}]*\};", pb).group() + "\n"
        (root / "constants.inc").write_text(defs)
        (root / "types.inc").write_text(typedef(src["azstem_internal.h"], "deck_stems") +
                                       typedef(src["azstem_internal.h"], "Float2") + typedef(src["azstem.c"], "Pos32"))
        fixture_source = HERE.parent / "stems-retry-publication/test_publication.c"
        fixture = fixture_source.read_text().split("#include PRODUCTION_INC")[0]
        for old, new in [("static void env_progress(int deck) { (void)deck; }", "static void env_progress(int deck);"),
                         ("static void free_stems(int d) { g_dk[d].stem_frames = 0; }", "static void free_stems(int d);")]:
            fixture = once(fixture, old, new)
        declarations = pb[pb.index("static deck_stems g_recovery_dk"):pb.index("static int recovery_hidden")]
        declarations += typedef(pb, "pjob_t") + typedef(pb, "loadjob_t")
        (root / "declarations.inc").write_text(declarations)
        groups = [(pb, ["recovery_hidden", "stem_decode_target", "recovery_free_buffers"]),
                  (env, ["free_stems", "env_cache_path", "env_cache_save", "env_progress", "env_cache_load", "build_env",
                         "azstem_env_gen", "azstem_env_final", "azstem_frames", "azstem_wave"]),
                  (pb, ["publish_stem_prefix", "azstem_present", "advance_stem_frames", "decode_stem_progressive", "dth_prog"]),
                  (ofs, ["ofs_stems_decoded"]), (key, ["ofs_stem_mono_lim"])]
        production = "".join(function(s, n) for s, ns in groups for n in ns)
        tail = function(ofs, "ofs_load_measure")
        tail = tail[tail.index("out:\n") + len("out:\n"):tail.rfind("}")]
        production += "static void ofs_load_measure(int deck,const char *track,const char *src) {\n" + \
                      "fixture_offset_boundary(deck); (void)src;deck_stems *dk=stem_decode_target(deck);\n" + \
                      "int apply=1,rate=44100;long lag=17,t0=0;const char *kind=\"fixture\",*vendor=\"fixture\",*why=NULL;\n" + \
                      "long long base=0;double fine=1,coarse=1,second=0;float *x=NULL,*nat=NULL;\n" + tail + "}\n"
        rest = [(pb, ["load_stems_ogg", "loader_thread", "deck_track_changed", "drop_stems", "start_load_mode", "start_load"]),
                (key, ["stem_play_observe", "stem_retry_play_activity"]), (pb, ["memory_retry_ready", "stems_tick"]),
                (env, ["stem_gains", "stems_play_original"]), (key, ["pbread_hook"])]
        production += "".join(function(s, n) for s, ns in rest for n in ns)
        exact_production = production
        if args.control in ["helper-guard-removed", "helper-check-before-lock"]:
            old = function(production, "publish_stem_prefix")
            new = once(old, "if (!recovery_hidden(deck) &&\n        !atomic_load_explicit", "if (!atomic_load_explicit")
            if args.control == "helper-check-before-lock":
                new = once(new, "    pthread_mutex_lock(&g_adv_lock);", "    if (recovery_hidden(deck))\n        return;\n    pthread_mutex_lock(&g_adv_lock);")
            production = once(production, old, new)
        elif args.control == "callsite-guard-removed":
            production = once(production, final_call, "    publish_stem_prefix(deck, ofs_stems_decoded(deck));")
        manifest = {"base": BASE, "patch_order": [{"path": str(p.relative_to(HERE.parent)), "sha256": digest(p.read_bytes())} for p in patches],
                    "standalone_reference_sha256": digest(standalone.read_bytes()),
                    "source_sha256": {n: digest(src[n].encode()) for n in names},
                    "functions": [n for _, ns in groups + rest for n in ns],
                    "production_sha256": digest(exact_production.encode()),
                    "partial_function": "ofs_load_measure actual out tail only; no codec/calibration work",
                    "fixture_dependency_sha256": digest(fixture_source.read_bytes()),
                    "original_restart_test_sha256": digest(original_restart.encode()),
                    "control": args.control, "suites": {}}
        print(json.dumps({k: manifest[k] for k in ["base", "patch_order", "production_sha256", "control"]}), flush=True)
        for suite in (["ordinary", "staging"] if args.suite == "both" else [args.suite]):
            suite_fixture = fixture
            if suite == "ordinary":
                suite_fixture = once(suite_fixture, "#define FRAMES 4096", "#define FRAMES (3 * PROG_CHUNK + 137)")
            (root / "fixture.inc").write_text(suite_fixture)
            suite_production = production
            if suite == "staging":
                suite_production = once(suite_production, "static void publish_stem_prefix(int deck, long frames) {",
                    "static void publish_stem_prefix_body(int deck, long frames);\n"
                    "static void publish_stem_prefix(int deck, long frames) {\n"
                    "    fixture_publication_call(deck);\n"
                    "    publish_stem_prefix_body(deck, frames);\n}\n"
                    "static void publish_stem_prefix_body(int deck, long frames) {")
            (root / "production.inc").write_text(suite_production)
            source = HERE / ("test_" + suite + ".c")
            manifest["suites"][suite] = {"source_sha256": digest(source.read_bytes()),
                                          "fixture_sha256": digest(suite_fixture.encode()),
                                          "instrumented_production_sha256": digest(suite_production.encode())}
            if args.manifest:
                args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
            flags = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-O1", "-g", "-pthread", "-DCANDIDATE=1"]
            for k, f in {"CONSTANTS_INC": "constants.inc", "TYPES_INC": "types.inc", "FIXTURE_INC": "fixture.inc", "RECOVERY_DECLARATIONS_INC": "declarations.inc", "PRODUCTION_INC": "production.inc"}.items():
                flags.append('-D' + k + '="' + str(root / f) + '"')
            modes = [("plain", [])] if args.plain_only else [("plain", []), ("asan-ubsan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]), ("tsan", ["-fsanitize=thread", "-fno-omit-frame-pointer"])]
            for mode, extra in modes:
                exe = root / (suite + "-" + mode)
                subprocess.run(flags + extra + [str(source), "-lm", "-o", str(exe)], check=True)
                print(suite + " " + mode + ":", flush=True)
                result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=90)
                output = (result.stdout + result.stderr).replace(str(root), "<TEMP>").replace(str(HERE.parent.parent), "<REPO>")
                print(output, end="", flush=True)
                if args.control != "none":
                    expected = "FAIL: private finalization skips public helper" if args.control == "callsite-guard-removed" else "FAIL: direct helper rejects private prefix"
                    expected_count = {"helper-guard-removed": 12, "helper-check-before-lock": 4, "callsite-guard-removed": 12}[args.control]
                    failures = re.findall(r"^FAIL:.*$", output, re.M)
                    totals = re.findall(r"(\d+) passed, (\d+) failed", output)
                    if result.returncode != 1 or failures != [expected] * expected_count or \
                            len(totals) != 1 or int(totals[0][1]) != expected_count:
                        raise RuntimeError("negative control did not exercise its intended assertion")
                    print("Named guard control rejected as expected; this is not the candidate.", flush=True)
                elif result.returncode:
                    raise RuntimeError(suite + " " + mode + " failed: " + str(result.returncode))
        if args.manifest:
            args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()

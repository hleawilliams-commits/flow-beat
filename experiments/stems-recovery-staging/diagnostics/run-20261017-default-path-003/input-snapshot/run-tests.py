#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Apply staging patch in a temporary tree and test extracted production code."""
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", type=Path, required=True)
    ap.add_argument("--plain-only", action="store_true")
    ap.add_argument("--manifest", type=Path)
    ap.add_argument("--baseline-ordinary", action="store_true",
                    help="reproduce ordinary-path TSan finding with only the preceding two patches")
    ap.add_argument("--asan-repeat", type=int, default=0,
                    help="diagnose a flaky fixture by repeating its ASan/UBSan executable")
    ap.add_argument("--restart-diagnostic", type=int, default=0,
                    help="bounded focused restart cases with per-thread traces; ASan/UBSan only")
    ap.add_argument("--diagnostic-empty-ordinary", action="store_true",
                    help="negative control: ordinary codec returns EOF; original restart assertion must fail")
    args = ap.parse_args()
    if args.diagnostic_empty_ordinary and not args.restart_diagnostic:
        ap.error("--diagnostic-empty-ordinary requires --restart-diagnostic")
    files = ["src/azstem/" + p for p in ["azstem.c", "stem_playback.c", "key_shift.c", "stem_envelopes.c",
             "ofsload.c", "azstem_internal.h", "ep147rate.h"]]
    files += ["tests/unit/" + p for p in ["run.sh", "trackgen_test.c", "check.h"]]
    patches = [HERE.parent / "stems-memory-retry/memory-retry.patch",
               HERE.parent / "stems-retry-activity/activity-followup.patch", HERE / "staging-only.patch"]
    if args.baseline_ordinary: patches = patches[:2]
    with tempfile.TemporaryDirectory(prefix="stem-stage-test-") as directory:
        root = Path(directory)
        for rel in files:
            p = root / rel; p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(subprocess.check_output(["git", "show", BASE + ":" + rel], cwd=args.upstream))
        (root / "tests/unit/run.sh").chmod(0o755)
        for patch in patches:
            subprocess.run(["git", "apply", str(patch)], cwd=root, check=True)
        src = {Path(p).name: (root / p).read_text() for p in files}
        pb, key, env, ofs = [src[p] for p in ["stem_playback.c", "key_shift.c", "stem_envelopes.c", "ofsload.c"]]
        constants_by_file = {"azstem_internal.h": ["NDECK", "NSTEM", "AZW_N"], "ep147rate.h": ["AZ_EP147_RATE"],
                             "stem_envelopes.c": ["XF_ORIG", "ENV_MAGIC", "ENV_MAX_FRAMES"],
                             "stem_playback.c": ["OFS_N", "AZSTEM_MEM_FLOOR_MB", "PROG_CHUNK", "EJECT_EMPTY_TICKS"],
                             "key_shift.c": ["POS_SAMPLE_OFF", "PLAYHEAD_MAX"]}
        constants = "".join(re.search(r"^#define " + n + r"\s+[^\n]+", src[p], re.M).group() + "\n"
                            for p, names in constants_by_file.items() for n in names)
        constants += re.search(r"enum \{\s*OFS_IDLE,[^}]*\};", pb).group() + "\n"
        (root / "constants.inc").write_text(constants)
        (root / "types.inc").write_text(typedef(src["azstem_internal.h"], "deck_stems") +
                                        typedef(src["azstem_internal.h"], "Float2") + typedef(src["azstem.c"], "Pos32"))
        # Reuse only the existing fixture declarations/native stubs, not its
        # tests or implementation logic. Exact replacements fail if it drifts.
        fixture = (HERE.parent / "stems-retry-publication/test_publication.c").read_text().split("#include PRODUCTION_INC")[0]
        for old, new in [("static void env_progress(int deck) { (void)deck; }", "static void env_progress(int deck);"),
                         ("static void free_stems(int d) { g_dk[d].stem_frames = 0; }", "static void free_stems(int d);")]:
            assert fixture.count(old) == 1
            fixture = fixture.replace(old, new)
        (root / "fixture.inc").write_text(fixture)
        declarations = ("static deck_stems g_recovery_dk[NDECK];\n"
                        "static _Atomic int g_recovery_mode[NDECK],g_recovery_ready[NDECK];\n"
                        "static _Atomic unsigned long long g_recovery_token[NDECK];\n"
                        "static unsigned long long g_recovery_next[NDECK];\n") if args.baseline_ordinary else \
                       pb[pb.index("static deck_stems g_recovery_dk"):pb.index("static int recovery_hidden")]
        declarations += typedef(pb, "pjob_t") + typedef(pb, "loadjob_t")
        (root / "recovery-declarations.inc").write_text(declarations)
        ordered = [(pb, [] if args.baseline_ordinary else ["recovery_hidden", "stem_decode_target", "recovery_free_buffers"]),
                   (env, ["free_stems", "env_cache_path", "env_cache_save", "env_progress", "env_cache_load", "build_env",
                          "azstem_env_gen", "azstem_env_final", "azstem_frames", "azstem_wave"]),
                   (pb, ["azstem_present", "advance_stem_frames", "decode_stem_progressive", "dth_prog"]),
                   (ofs, ["ofs_stems_decoded"])]
        production = "".join(function(s, n) for s, names in ordered for n in names)
        if args.baseline_ordinary:
            production = ("static int recovery_hidden(int d){(void)d;return 0;}\n"
                          "static deck_stems *stem_decode_target(int d){return &g_dk[d];}\n"
                          "static void recovery_free_buffers(int d){(void)d;}\n") + production
        # Actual offset completion tail; codec/correlation earlier in this
        # function is represented by fixed fixture locals, never claimed tested.
        tail = function(ofs, "ofs_load_measure")
        tail = tail[tail.index("out:\n") + len("out:\n"):tail.rfind("}")]
        production += "static void ofs_load_measure(int deck, const char *track, const char *src) {\n" + \
                      "fixture_offset_boundary(deck);\n" + \
                      "(void)src; deck_stems *dk=stem_decode_target(deck); int apply=1,rate=44100; long lag=17,t0=0;\n" + \
                      "const char *kind=\"fixture\",*vendor=\"fixture\",*why=NULL; long long base=0;\n" + \
                      "double fine=1,coarse=1,second=0; float *x=NULL,*nat=NULL;\n" + tail + "}\n"
        ordered += [(pb, ["load_stems_ogg", "loader_thread", "deck_track_changed", "drop_stems"] +
                          ([] if args.baseline_ordinary else ["start_load_mode"]) + ["start_load"]),
                    (key, ["stem_play_observe", "stem_retry_play_activity"]),
                    (pb, ["memory_retry_ready", "stems_tick"]),
                    (env, ["stem_gains", "stems_play_original"]), (key, ["pbread_hook"])]
        production += "".join(function(s, n) for s, names in ordered[4:] for n in names)
        if args.baseline_ordinary:
            production += "static void start_load_mode(int d,const char *t,int r){(void)r;start_load(d,t);}\n"
        exact_production = production
        test_source = HERE / "test_staging.c"
        if args.restart_diagnostic:
            assert not args.baseline_ordinary and args.restart_diagnostic > 0
            def replace_once(value, old, new):
                assert value.count(old) == 1, old
                return value.replace(old, new)
            production = replace_once(production, "static void advance_stem_frames(int deck) {",
                "static void advance_stem_frames_body(int deck);\n"
                "static void advance_stem_frames(int deck) {\n"
                "diag_event(DIAG_ADV_BEGIN,deck,0,0);advance_stem_frames_body(deck);\n"
                "diag_event(DIAG_ADV_END,deck,0,0);}\n"
                "static void advance_stem_frames_body(int deck) {")
            production = replace_once(production, "dk->stem_frames = m;", "dk->stem_frames = m;diag_event(DIAG_PUBLISH,deck,0,m);")
            production = replace_once(production, "long minfr = recovery ? ofs_stems_decoded(deck) : dk->stem_frames;",
                "long minfr = recovery ? ofs_stems_decoded(deck) : dk->stem_frames;diag_result(deck,recovery,minfr);")
            harness = test_source.read_text()
            (root / "restart-trace.h").write_bytes((HERE / "diagnostics/restart-trace.h").read_bytes())
            harness = replace_once(harness, "static int az_thread_create(", '#include "restart-trace.h"\nstatic int az_thread_create(')
            harness = replace_once(harness, "int result = pthread_create(thread, attr, fn, arg);", "int result = diag_create(thread, attr, fn, arg);")
            harness = replace_once(harness, "int rc=pthread_join(thread,result);", "int rc=pthread_join(thread,result);diag_join(thread,rc);")
            harness = replace_once(harness, "static void after_store(const void *p,long long value) {",
                "static void after_store(const void *p,long long value) {diag_atomic(DIAG_STORE,p,value);")
            harness = replace_once(harness, "#include PRODUCTION_INC", "#undef atomic_load_explicit\n"
                "#define atomic_load_explicit(P,O) ({ __auto_type diag_value=__c11_atomic_load((P),(O)); diag_atomic(DIAG_LOAD,(P),(long long)diag_value);diag_value; })\n"
                "#include PRODUCTION_INC")
            harness = replace_once(harness, 'printf("deck %d: completion boundary %s passed\\n",d+1,restart?"restart":"drop");', "/* Diagnostic summary is emitted after independent join verification. */")
            harness = replace_once(harness, "int main(void) {", "static int inherited_main(void) {")
            harness += "\n" + (HERE / "diagnostics/restart-main.c").read_text()
            test_source = root / "diagnostic-harness.c"; test_source.write_text(harness)
        (root / "production.inc").write_text(production)
        print("Pinned upstream: " + BASE, flush=True)
        print(("Baseline: no staging patch applied" if args.baseline_ordinary else
               "Staging patch SHA256: " + hashlib.sha256(patches[-1].read_bytes()).hexdigest()), flush=True)
        print("Production extraction SHA256: " + hashlib.sha256(production.encode()).hexdigest(), flush=True)
        if args.restart_diagnostic:
            print("Exact uninstrumented extraction SHA256: " + hashlib.sha256(exact_production.encode()).hexdigest(), flush=True)
            print("Diagnostic harness SHA256: " + hashlib.sha256(harness.encode()).hexdigest(), flush=True)
            print("Diagnostic control: " + ("ordinary codec EOF; original assertion expected to fail" if args.diagnostic_empty_ordinary else "none"), flush=True)
        if args.manifest:
            args.manifest.write_text(json.dumps({"upstream_commit": BASE,
                "patches": [{"path": str(p.relative_to(HERE.parent.parent)), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in patches],
                "source_sha256": {p: hashlib.sha256((root/p).read_bytes()).hexdigest() for p in files},
                "extracted_functions": [n for _, names in ordered for n in names],
                "production_sha256": hashlib.sha256(production.encode()).hexdigest(),
                "exact_production_sha256": hashlib.sha256(exact_production.encode()).hexdigest(),
                "test_source_sha256": hashlib.sha256(test_source.read_bytes()).hexdigest(),
                "diagnostic_inputs_sha256": {p: hashlib.sha256((HERE/p).read_bytes()).hexdigest()
                    for p in ["run-tests.py", "test_staging.c", "diagnostics/restart-trace.h", "diagnostics/restart-main.c"]}
                    if args.restart_diagnostic else {},
                "fixture_dependency_sha256": hashlib.sha256(fixture.encode()).hexdigest(),
                "partial_function": "ofs_load_measure actual out tail; earlier codec/correlation represented by fixture locals"}, indent=2) + "\n")
        flags = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-O1", "-g", "-pthread"]
        if args.baseline_ordinary: flags += ["-DBASELINE_ORDINARY"]
        defines = ['-D' + key + '="' + str(root / file) + '"' for key, file in {
            "CONSTANTS_INC": "constants.inc", "TYPES_INC": "types.inc", "FIXTURE_INC": "fixture.inc",
            "RECOVERY_DECLARATIONS_INC": "recovery-declarations.inc", "PRODUCTION_INC": "production.inc"}.items()]
        modes = [("plain", [])] if args.plain_only else [("plain", []), ("asan-ubsan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]), ("tsan-private-recovery", ["-DFIXTURE_SERIAL_NORMAL", "-fsanitize=thread", "-fno-omit-frame-pointer"])]
        if args.baseline_ordinary and not args.plain_only:
            modes = [("tsan-ordinary-baseline", ["-fsanitize=thread", "-fno-omit-frame-pointer"])]
        if args.asan_repeat:
            modes = [("asan-ubsan-repeat", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]
        if args.restart_diagnostic:
            modes = [("asan-ubsan-restart-diagnostic", ["-DDIAGNOSTIC_ATTEMPTS="+str(args.restart_diagnostic), "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]
            if args.diagnostic_empty_ordinary:modes[0][1].append("-DDIAGNOSTIC_EMPTY_ORDINARY")
        for mode, extra in modes:
            exe = root / mode
            subprocess.run(flags + extra + defines + [str(test_source), "-lm", "-o", str(exe)], check=True)
            print(mode + ":", flush=True)
            if args.baseline_ordinary and not args.plain_only:
                result = subprocess.run([str(exe)], capture_output=True, text=True)
                output = (result.stdout + result.stderr).replace("/private" + str(root), "<TEMP>").replace(str(root), "<TEMP>")
                print(output, end="")
                if result.returncode == 0 or "ThreadSanitizer: data race" not in output or "0 failed" not in output:
                    raise RuntimeError("baseline ordinary-path race was not reproduced as expected")
                print("Expected ordinary-path TSan finding reproduced without staging patch.")
            else:
                for attempt in range(args.asan_repeat or 1):
                    if args.asan_repeat: print("Attempt " + str(attempt + 1), flush=True)
                    subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()

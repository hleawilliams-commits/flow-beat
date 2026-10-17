#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise exact pinned logging functions and drain block in temporary files.

Does not edit the core checkout or contact a deck/emulator. No persistent binaries.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

BASE = "3e0c5cb010454228f4ee63226b60d060494eeaae"
HERE = Path(__file__).resolve().parent
REL = "src/azmod/community.c"


def balanced_block(text, start):
    """Return the pinned block, ignoring braces in strings and comments."""
    begin = text.index("{", start)
    token = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', re.S)
    depth = 0
    for m in token.finditer(text, begin):
        if m.group() == "{":
            depth += 1
        elif m.group() == "}":
            depth -= 1
            if depth == 0:
                return text[start:m.end()]
    raise RuntimeError("unclosed source block")


def function(text, name):
    m = re.search(r"^static [^\n]*\b" + re.escape(name) + r"\([^;]*?\)\s*\{", text, re.M)
    if not m:
        raise RuntimeError("missing production function: " + name)
    return balanced_block(text, m.start()) + "\n"


def define(text, name):
    m = re.search(r"^#define\s+" + re.escape(name) + r"\s+[^\n]+", text, re.M)
    if not m:
        raise RuntimeError("missing production define: " + name)
    return m.group() + "\n"


def mod_type(text):
    start = text.index("typedef struct {", text.index("/* states shown in the UI"))
    end = text.index("} cm_mod;", start) + len("} cm_mod;")
    return text[start:end]


def production_include(text, sdk):
    tick = function(text, "community_tick")
    marker = "    for (int i = 0; i < g_cm_n; i++) {   /* on_frame's queued log lines and refusals, written here */"
    assert tick.count(marker) == 1
    drain = balanced_block(tick, tick.index(marker))
    # Include full actual type/queue declaration, logging, drain and strike code.
    # Only unrelated SDK type definitions, global initialization and tick scheduling are absent.
    return (
        ''.join(define(text, n) for n in ["CM_MAX", "CM_CFG_MAX", "CM_CFGS_MAX", "CM_LOG_CAP"])
        + '#include "azlogcap.h"\n'
        + mod_type(text) + "\n"
        + "static cm_mod g_cm[CM_MAX];\nstatic int g_cm_n;\n"
        + re.search(r"^static __thread int g_cm_in_frame;[^\n]*", text, re.M).group() + "\n"
        + re.search(r"^static _Atomic int g_cm_logs_on[^\n]*", text, re.M).group() + "\n"
        + function(text, "cm_vlog") + function(text, "cm_logf")
        + "static void drain_actual(void) {\n" + drain + "\n}\n"
        + define(sdk, "SDK_FILTER_BUDGET_US") + define(sdk, "SDK_FILTER_STRIKES")
        + function(sdk, "sdk_strike")
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", required=True, type=Path)
    args = ap.parse_args()
    upstream = args.upstream.resolve()
    with tempfile.TemporaryDirectory(prefix="frame-log-force-") as tmp:
        root = Path(tmp)
        def source(rel):
            return subprocess.check_output(["git", "show", BASE + ":" + rel], cwd=upstream, text=True)
        original = source(REL)
        sdk = source("src/azmod/sdk_input.c")
        p = root / REL
        p.parent.mkdir(parents=True)
        p.write_text(original)
        (root / "azlogcap.h").write_text(source("src/common/azlogcap.h"))
        subprocess.run(["git", "apply", "--check", str(HERE / "frame-log-force.patch")], cwd=root, check=True)
        subprocess.run(["git", "apply", str(HERE / "frame-log-force.patch")], cwd=root, check=True)
        patched = p.read_text()
        # The production timing/strike protection and queue layout must not change.
        strip_comments = lambda s: re.sub(r"/\*.*?\*/", "", s, flags=re.S)
        assert strip_comments(mod_type(original)) == strip_comments(mod_type(patched))
        assert "char line[8][160];" in patched and "int ready[8];" in patched
        baseline = root / "baseline.inc"
        updated = root / "updated.inc"
        baseline.write_text(production_include(original, sdk))
        updated.write_text(production_include(patched, sdk))
        cc = os.environ.get("CC", "cc")
        flags = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g"]
        def run(label, inc, extra=()):
            exe = root / label
            subprocess.run(flags + list(extra) + ['-DPRODUCTION_INC="' + str(inc) + '"',
                           str(HERE / "test_frame_log.c"), "-o", str(exe)], check=True)
            run_dir = root / (label + "-files")
            run_dir.mkdir()
            result = subprocess.run([str(exe)], cwd=run_dir, capture_output=True, text=True)
            print(label + ":", flush=True)
            print(result.stdout + result.stderr, end="", flush=True)
            return result
        control = run("original-control", baseline)
        required = ["FAIL: forced line survives logs-off drain",
                    "FAIL: production overrun diagnostic survives logs-off drain"]
        if control.returncode != 1 or not all(x in control.stderr for x in required):
            raise RuntimeError("original control did not reproduce the actual forced-log loss")
        for label, extra in [("patched-plain", []),
                             ("patched-asan-ubsan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]:
            result = run(label, updated, extra)
            if result.returncode:
                raise RuntimeError(label + " failed")
        print("Pinned-source control and patched checks passed; temporary artifacts removed on exit.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Capture pinned upstream host sources plus an inactive observation module.

No checkout mutation, application execution, native endpoint, or dependency update.
Raw source snapshots and receipts remain in a fresh ignored private directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import uuid

HERE = Path(__file__).resolve().parent
COMMIT = "06fc981707e69982c0554bb508e0c646c005a979"
INPUTS = ("observation.rs", "tests.rs", "base-source-pins.json",
          "candidate-pins.json", "prepare-candidate.py")
BASE_FILES = {"az-preview/Cargo.toml", "az-preview/build.rs", "az-preview/layout/xdj-az.tsv",
              *("az-preview/src/" + n + ".rs" for n in
                ("app", "boot", "config", "controls", "layout", "main", "meter", "panel", "parts", "screen"))}
ANCHOR = "use crate::config::repo_root;"
DECLARATION = '#[cfg(unix)]\n#[path = "supervisor_observation.rs"]\npub(crate) mod owned_observation;\n\n'
LOCK = HERE.parent/"emulator-preview-gate/Cargo.lock"

def require(value, message):
    if not value:
        raise RuntimeError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def read(path):
    require(stat.S_ISREG(path.lstat().st_mode), "not a regular input: " + path.name)
    return path.read_bytes()

def git(args, cwd):
    env = {k:v for k,v in os.environ.items() if not k.startswith("GIT_")}
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
               GIT_CONFIG_SYSTEM=os.devnull, GIT_TERMINAL_PROMPT="0")
    result = subprocess.run(["git", *args], cwd=cwd, env=env, capture_output=True, timeout=60)
    require(result.returncode == 0, "local Git export failed: " + result.stderr.decode(errors="replace"))
    return result.stdout

def prepare(emu_source):
    captured = {n:read(HERE/n) for n in INPUTS}
    base = json.loads(captured["base-source-pins.json"])
    require(base["emulator_commit"] == COMMIT and set(base["files"]) == BASE_FILES,
            "base inventory differs")
    require(git(["rev-parse", "--verify", COMMIT + "^{commit}"], emu_source).decode().strip() == COMMIT,
            "pinned emulator commit unavailable")
    entries = git(["ls-tree", "-rz", COMMIT, "--", "az-preview"], emu_source).split(b"\0")[:-1]
    actual = {}
    for entry in entries:
        header, name = entry.split(b"\t", 1)
        mode, kind, oid = header.split()
        require(mode in (b"100644", b"100755") and kind == b"blob", "nonregular upstream entry")
        actual[name.decode()] = oid
    require(set(actual) == BASE_FILES, "upstream full crate inventory changed")
    blobs = {n:git(["show", COMMIT + ":" + n], emu_source) for n in sorted(BASE_FILES)}
    require({n:sha(b) for n,b in blobs.items()} == base["files"], "upstream source hash mismatch")
    lock = read(LOCK)
    require(sha(lock) == base["cargo_lock_sha256"], "retained lock mismatch")
    boot = "az-preview/src/boot.rs"
    text = blobs[boot].decode()
    require(text.count(ANCHOR) == 1, "module declaration anchor is not unique")
    candidate = dict(blobs)
    candidate[boot] = text.replace(ANCHOR, DECLARATION + ANCHOR).encode()
    candidate["az-preview/Cargo.lock"] = lock
    candidate["az-preview/src/supervisor_observation.rs"] = captured["observation.rs"]
    candidate["az-preview/src/tests.rs"] = captured["tests.rs"]
    pins = json.loads(captured["candidate-pins.json"])
    require({n:sha(b) for n,b in candidate.items()} == pins, "candidate inventory/hash mismatch")
    private = HERE/"private"
    private.mkdir(exist_ok=True)
    require(stat.S_ISDIR(private.lstat().st_mode), "private output is not a real directory")
    output = private/("observation-" + uuid.uuid4().hex)
    output.mkdir(mode=0o700)
    for group, contents in (("inputs", captured), ("upstream", blobs), ("tree", candidate)):
        for name,data in contents.items():
            dest = output/group/name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
    (output/"inputs"/"retained-Cargo.lock").write_bytes(lock)
    require(all(read(HERE/n) == b for n,b in captured.items()) and read(LOCK) == lock,
            "input changed during capture")
    receipt = {"emulator_commit": COMMIT, "upstream_files_sha256": base["files"],
               "candidate_files_sha256": pins, "cargo_lock_sha256": sha(lock),
               "inputs_sha256": {n:sha(b) for n,b in captured.items()},
               "changed_upstream_files": [boot], "status": "verified",
               "scope": "Inactive Unix module declaration only; legacy supervisor loop unchanged and still active. Separate from prior OwnedStartup composition."}
    (output/"preparation.json").write_text(json.dumps(receipt,indent=2,sort_keys=True)+"\n")
    return output

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--emu-source", type=Path, required=True)
    args = p.parse_args()
    print(prepare(args.emu_source.resolve()))

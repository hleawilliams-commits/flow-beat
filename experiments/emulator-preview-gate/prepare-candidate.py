#!/usr/bin/env python3
"""Prepare this offline preview candidate in a new ignored private directory.

Exports exactly the pinned Git blobs, never the emulator's working tree. Applies
only the reviewed local patch, copies its pinned lockfile, and checks the entire
candidate file inventory. Does not build, launch, connect, or enable any backend.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parent
COMMIT = "06fc981707e69982c0554bb508e0c646c005a979"
BASE_FILES = frozenset([
    "az-preview/Cargo.toml", "az-preview/build.rs", "az-preview/layout/xdj-az.tsv",
    *["az-preview/src/" + name + ".rs" for name in
      ("app", "boot", "config", "controls", "layout", "main", "meter", "panel", "parts", "screen")],
])
ADDED_FILES = frozenset(["az-preview/Cargo.lock", "az-preview/src/mixer_producer.rs",
                         "az-preview/src/mixer_route.rs"])
HEX64 = re.compile(r"[0-9a-f]{64}\Z")

def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def regular_bytes(path: Path) -> bytes:
    require(stat.S_ISREG(path.lstat().st_mode), "input is not a regular file: " + path.name)
    return path.read_bytes()

def unique_object(pairs):
    out = {}
    for key, value in pairs:
        require(key not in out, "duplicate JSON key: " + key)
        out[key] = value
    return out

def load_json(path: Path):
    return json.loads(regular_bytes(path), object_pairs_hook=unique_object)

def file_pins(value, expected: frozenset[str]) -> dict[str, str]:
    require(isinstance(value, dict) and set(value) == expected, "unexpected pinned file inventory")
    for name, digest in value.items():
        path = PurePosixPath(name)
        require(not path.is_absolute() and ".." not in path.parts and str(path) == name,
                "unsafe pinned path")
        require(isinstance(digest, str) and HEX64.fullmatch(digest) is not None, "invalid file hash")
    return value

def git(args: list[str], cwd: Path) -> bytes:
    # Ignore ambient Git path/config overrides; every command names its source
    # or runs inside the fresh disposable repository. No remote command exists.
    env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
               GIT_CONFIG_SYSTEM=os.devnull, GIT_TERMINAL_PROMPT="0")
    result = subprocess.run(["git", *args], cwd=cwd, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=60, check=False)
    if result.returncode:
        raise RuntimeError("git command failed: " + result.stderr.decode(errors="replace"))
    return result.stdout

def inventory(tree: Path) -> dict[str, str]:
    result = {}
    for current, dirs, files in os.walk(tree, followlinks=False):
        here = Path(current)
        if here == tree:
            dirs[:] = [name for name in dirs if name != ".git"]
        for name in dirs:
            require(stat.S_ISDIR((here/name).lstat().st_mode), "candidate contains a non-directory")
        for name in files:
            path = here/name
            require(stat.S_ISREG(path.lstat().st_mode), "candidate contains a non-regular file")
            result[path.relative_to(tree).as_posix()] = sha(path.read_bytes())
    return result

def prepare(emu_source: Path) -> Path:
    base_document = load_json(ROOT/"base-source-pins.json")
    require(isinstance(base_document, dict) and set(base_document) == {"emulator_commit", "files"},
            "invalid base pin schema")
    require(base_document["emulator_commit"] == COMMIT, "unexpected emulator commit")
    base = file_pins(base_document["files"], BASE_FILES)
    candidate_document = load_json(ROOT/"candidate-pins.json")
    required = {"files", "patch_sha256", "cargo_lock_sha256"}
    require(isinstance(candidate_document, dict) and required <= set(candidate_document)
            and set(candidate_document) <= required | {"emulator_commit"}, "invalid candidate pin schema")
    require(candidate_document.get("emulator_commit", COMMIT) == COMMIT, "candidate commit mismatch")
    candidate = file_pins(candidate_document["files"], BASE_FILES | ADDED_FILES)
    patch = regular_bytes(ROOT/"preview-gate.patch")
    lock = regular_bytes(ROOT/"Cargo.lock")
    require(candidate_document["patch_sha256"] == sha(patch), "patch hash mismatch")
    require(candidate_document["cargo_lock_sha256"] == sha(lock), "lockfile hash mismatch")
    require(candidate["az-preview/Cargo.lock"] == sha(lock), "candidate lockfile pin mismatch")
    require(emu_source.is_dir(), "emulator source is not a directory")
    require(git(["rev-parse", "--verify", COMMIT + "^{commit}"], emu_source).strip().decode() == COMMIT,
            "pinned commit is unavailable")
    private = ROOT/"private"
    private.mkdir(exist_ok=True)
    require(stat.S_ISDIR(private.lstat().st_mode), "private output must be a real directory")
    output = private/("prepare-" + uuid.uuid4().hex)
    output.mkdir(mode=0o700, exist_ok=False)
    tree = output/"tree"
    tree.mkdir()
    receipt = {"emulator_commit": COMMIT, "base_files": len(base),
               "candidate_files": len(candidate), "patch_sha256": sha(patch),
               "cargo_lock_sha256": sha(lock), "status": "started"}
    (output/"preparation.json").write_text(json.dumps(receipt, indent=2, sort_keys=True)+"\n")
    try:
        # This fresh .git is only an apply context; no checkout/reset of either
        # source checkout or the shared candidate/target is performed.
        git(["-c", "init.templateDir=", "init", "--quiet"], tree)
        for name, digest in sorted(base.items()):
            metadata = git(["ls-tree", "-z", COMMIT, "--", name], emu_source).split(b"\0")
            require(len(metadata) == 2 and metadata[1] == b"", "unexpected Git entry count")
            header, path = metadata[0].split(b"\t", 1)
            mode, kind, _oid = header.split(b" ")
            require(path.decode() == name and mode in (b"100644", b"100755") and kind == b"blob",
                    "pinned path is not a regular Git blob")
            data = git(["show", COMMIT + ":" + name], emu_source)
            require(sha(data) == digest, "base blob mismatch: " + name)
            destination = tree/name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            destination.chmod(0o755 if mode == b"100755" else 0o644)
        require(inventory(tree) == base, "full exported base differs from pins")
        # Apply the captured bytes, not a second read of mutable public inputs.
        saved_patch = output/"preview-gate.patch"
        saved_patch.write_bytes(patch)
        git(["apply", "--check", str(saved_patch)], tree)
        git(["apply", str(saved_patch)], tree)
        (tree/"az-preview/Cargo.lock").write_bytes(lock)
        actual = inventory(tree)
        require(actual == candidate, "patched candidate differs from exact inventory or hashes")
        receipt.update(status="verified", files=actual)
    except BaseException as exc:
        receipt.update(status="failed", error=type(exc).__name__ + ": " + str(exc))
        (output/"preparation.json").write_text(json.dumps(receipt, indent=2, sort_keys=True)+"\n")
        print("Retained failed preparation: " + str(output), file=sys.stderr)
        raise
    (output/"preparation.json").write_text(json.dumps(receipt, indent=2, sort_keys=True)+"\n")
    return output

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emu-source", type=Path, required=True,
                        help="existing local emulator Git checkout containing the pinned commit")
    args = parser.parse_args()
    print(prepare(args.emu_source.resolve()))

if __name__ == "__main__":
    main()

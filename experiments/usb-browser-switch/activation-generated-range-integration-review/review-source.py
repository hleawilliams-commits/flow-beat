#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Source-only integration packet: reuse retained blobs; read only new src/Makefile."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import uuid

HERE = Path(__file__).resolve().parent
PIN = "3e0c5cb010454228f4ee63226b60d060494eeaae"
OLD = {
    "source-ownership-result.json": "dafddcc8f9f42565803890bcd627b53310ef96bf97c6eb99b99db4f68c982a27",
    "source-tree-stdout.bin": "f1c074afdab88d7ac12ffb63a1c7f2e84b347f7c7facae2679691958e5290661",
    "source-objects-stdout.bin": "148ee2f4f9c70e49e30d60e805f0f0c6b49df2801ec3b3277e8d1f42c59bf2d8",
}
FROZEN = {
    "fixture/registry.h": "078290d7bb21d2c702c0c31edb5f5fe6318ebca47fd6a71ed60c9baed6188498",
    "fixture/registry.cpp": "418ceba1881fdb7cbd79bd0d8c96791a674ea37455e6870a5ab7094f38d78df8",
    "fixture/README.md": "72e688acd8314cb2e60f30933b6472da035ec798bd4c9f0b4098880497fc5d86",
    "fixture/verification.json": "5b5b564d1b7b669f1d3d0671d028601e7c004b1ff942c122a194d893f91b956c",
    "contract/README.md": "dac814afceb53820b0b7a86dc5339fd2a9ca3234b6e15d8ea64f59ba48f789f4",
}
EXTRA = (
    "src/azmod/sdk_vslot.c", "src/azmod/mod_menu_overlay.c",
    "src/azmod/ep_mem.h", "src/azmod/azmod-mod.h",
    "src/azmod/azmod_internal.h", "src/azstem/azstem.c",
    "src/common/azprot.h",
)
PATTERNS = {
    "installer_mentions": r"\bazm_install_hook(?:_at)?\b",
    "protection_mentions": r"\bazprot_(?:unlock|relock)\b",
    "vtable_mentions": r"\bazm_patch_vtable\b",
    "owner_ledger_mentions": r"\bg_hook_owner\b|\bg_hooked(?:_n)?\b",
}
def sha(b):
    return hashlib.sha256(b).hexdigest()
def jb(x):
    return (json.dumps(x, indent=2, sort_keys=True) + "\n").encode()
def save(p, b):
    p.parent.mkdir(parents=True, exist_ok=True)
    with p.open("xb") as f:
        f.write(b)
def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--prior", type=Path, required=True)
    ap.add_argument("--upstream", type=Path, required=True)
    args = ap.parse_args()
    run = HERE / "private" / ("review-" + uuid.uuid4().hex)
    run.mkdir(parents=True)
    inputs, commands, artifacts = {}, [], {}
    result = {"schema": 1, "sdk_commit": PIN, "status": "retaining",
              "scope": "Source-only common writer, owner, emission and module-input integration review",
              "old_captures_or_matrices_replayed": False, "compiler_or_runtime_execution": False,
              "firmware_or_device_access": False}
    def retain(name, b, origin):
        if name in FROZEN and sha(b) != FROZEN[name]:
            raise ValueError("frozen input changed: " + name)
        save(run / "inputs" / name, b)
        inputs[name] = {"bytes": len(b), "sha256": sha(b), "origin": origin}
    def artifact(name, b):
        save(run / name, b)
        artifacts[name] = {"bytes": len(b), "sha256": sha(b)}
    try:
        retain("review-source.py", Path(__file__).read_bytes(), "new-review")
        for name, expected in OLD.items():
            b = (args.prior / name).read_bytes()
            if sha(b) != expected:
                raise ValueError("prior byte mismatch: " + name)
            retain("prior/" + name, b, "reused-retained-capture")
        prior = json.loads((run / "inputs/prior/source-ownership-result.json").read_bytes())
        if prior["commit"] != PIN:
            raise ValueError("prior revision")
        tree = (run / "inputs/prior/source-tree-stdout.bin").read_text().splitlines()
        names = [n for n in tree if n.endswith((".c", ".h", ".cpp", ".inc", ".S"))
                 and "/test/" not in n and "/tests/" not in n]
        raw = (run / "inputs/prior/source-objects-stdout.bin").read_bytes()
        body, pos = {}, 0
        for name in names:
            end = raw.index(b"\n", pos)
            fields = raw[pos:end].split()
            if len(fields) != 3 or fields[1] != b"blob":
                raise ValueError("batch header")
            size = int(fields[2])
            b = raw[end + 1:end + 1 + size]
            if len(b) != size or raw[end + 1 + size:end + 2 + size] != b"\n":
                raise ValueError("batch length")
            pos = end + size + 2
            old = prior["source_files"][name]
            git_blob = hashlib.sha1(b"blob " + str(size).encode() + b"\0" + b).hexdigest()
            if (sha(b), len(b), fields[0].decode(), git_blob) != (
                    old["sha256"], old["bytes"], old["git_blob"], old["git_blob"]):
                raise ValueError("blob identity: " + name)
            body[name] = b
        if pos != len(raw) or set(body) != set(prior["source_files"]) or len(body) != 110:
            raise ValueError("closed corpus")
        index = {}
        for label, regex in PATTERNS.items():
            index[label] = [{"path": name, "line": line, "text": text}
                for name, b in sorted(body.items())
                for line, text in enumerate(b.decode().splitlines(), 1)
                if re.search(regex, text)]
        selected = sorted(set(EXTRA) | {x["path"] for x in index["installer_mentions"]})
        for name in selected:
            retain("sdk/" + name, body[name], "reused:" + PIN)
        for name in ("registry.h", "registry.cpp", "README.md", "verification.json"):
            retain("fixture/" + name, (HERE.parent / "activation-generated-range-fixture" / name).read_bytes(),
                   "frozen-cd48d522da0f9c2b6ff0d5c8e04733c900928d7b")
        retain("contract/README.md", (HERE.parent / "activation-generated-range-contract/README.md").read_bytes(),
               "frozen-53e0b13abab00828439939a5ef131d871faf988b")
        new = "src/Makefile"
        if new not in tree or new in body:
            raise ValueError("new source is not genuinely new")
        argv = ["git", "-C", str(args.upstream.resolve()), "show", PIN + ":" + new]
        spec = {"argv": argv, "timeout_seconds": 30}
        save(run / "command/spec.json", jb(spec))
        failure = None
        try:
            c = subprocess.run(argv, capture_output=True, check=False, timeout=30)
            out, err, rc = c.stdout, c.stderr, c.returncode
        except subprocess.TimeoutExpired as exc:
            out, err, rc, failure = exc.stdout or b"", exc.stderr or b"", None, "timeout"
        except OSError as exc:
            out, err, rc, failure = b"", b"", None, type(exc).__name__ + ": " + str(exc)
        save(run / "command/stdout.bin", out)
        save(run / "command/stderr.bin", err)
        receipt = {"argv": argv, "returncode": rc, "failure": failure,
                   "stdout_bytes": len(out), "stderr_bytes": len(err),
                   "stdout_sha256": sha(out), "stderr_sha256": sha(err)}
        save(run / "command/receipt.json", jb(receipt))
        commands.append({**receipt, "receipt_sha256": sha(jb(receipt))})
        if failure or rc != 0 or err:
            raise ValueError("new pinned Makefile read failed")
        retain("sdk/" + new, out, "new:" + PIN)
        artifact("lexical-index.json", jb({"scope": "Lexical mentions, including comments/declarations; not a resolved call graph or whole-program absence proof",
                                         "corpus_files": 110, "patterns": PATTERNS, "hits": index}))
        result.update(status="retained", reused_corpus_files=110,
                      selected_reused_sdk_files=len(selected), new_sdk_files=1,
                      lexical_counts={k: len(v) for k, v in index.items()})
    except BaseException as exc:
        result.update(status="failed", error=type(exc).__name__ + ": " + str(exc))
    result.update(inputs=inputs, commands=commands, artifacts=artifacts)
    save(run / "result.json", jb(result))
    print(json.dumps({"status": result["status"], "review": run.name,
                      "result_sha256": sha(jb(result))}))
    return 0 if result["status"] == "retained" else 1
if __name__ == "__main__":
    raise SystemExit(main())

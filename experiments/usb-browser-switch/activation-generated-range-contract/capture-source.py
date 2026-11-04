#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain only new SDK sources for the generated-range contract review.
Reuse the pinned prior capture as byte inputs; never replay its Git reads or tests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import uuid

HERE = Path(__file__).resolve().parent
COMMIT = "3e0c5cb010454228f4ee63226b60d060494eeaae"
PRIOR_SHA = "e22d851e0c72f080e05b8f7e0a5db5994f8f68b3fc6ae8eeb864aee248616bfd"
NEW = ("src/azmod/sdk_proc.c", "src/azmod/mod_menu_overlay.c",
       "src/azmod/screen_community.c", "deck/stick/deck/lib/az-community",
       "deck/payload/usr/sbin/az-usbload")

def sha(b):
    return hashlib.sha256(b).hexdigest()

def jb(obj):
    return (json.dumps(obj, indent=2, sort_keys=True) + "\n").encode()

def save(p, b):
    p.parent.mkdir(parents=True, exist_ok=True)
    with p.open("xb") as f:
        f.write(b)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--upstream", type=Path, required=True)
    ap.add_argument("--prior-capture", type=Path, required=True)
    a = ap.parse_args()
    run = HERE / "private" / ("capture-" + uuid.uuid4().hex)
    run.mkdir(parents=True)
    inputs, commands = {}, []
    result = {"schema": 1, "sdk_commit": COMMIT, "status": "capturing",
              "scope": "Source-only generated-range contract; eight reused SDK inputs and five newly captured SDK files.",
              "compiler_or_test_execution": False, "firmware_or_device_access": False,
              "old_capture_replayed": False}
    def retain(name, data, origin):
        save(run / "inputs" / name, data)
        inputs[name] = {"bytes": len(data), "sha256": sha(data),
                        "lines": len(data.splitlines()), "origin": origin}
    try:
        retain("capture-source.py", Path(__file__).read_bytes(), "new-review")
        prior = (a.prior_capture / "result.json").read_bytes()
        if sha(prior) != PRIOR_SHA:
            raise RuntimeError("prior capture receipt mismatch")
        retain("prior-result.json", prior, "reused-private-receipt")
        p = json.loads(prior)
        if p["status"] != "captured" or p["sdk_commit"] != COMMIT:
            raise RuntimeError("prior capture status or commit")
        reused = sorted(k for k in p["inputs"] if k.startswith("sdk/"))
        if len(reused) != 8:
            raise RuntimeError("prior SDK input set")
        for key in reused:
            raw = (a.prior_capture / "inputs" / key).read_bytes()
            if sha(raw) != p["inputs"][key]["sha256"]:
                raise RuntimeError("prior input changed: " + key)
            retain(key, raw, "reused:" + COMMIT)
        for path in NEW:
            folder = run / "commands" / ("%02d-" % (len(commands) + 1) + Path(path).name)
            argv = ["git", "-C", str(a.upstream.absolute()), "show", COMMIT + ":" + path]
            save(folder / "command.json", jb({"argv": argv, "timeout_seconds": 30}))
            failure = None
            try:
                c = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   check=False, timeout=30)
                out, err, rc = c.stdout, c.stderr, c.returncode
            except subprocess.TimeoutExpired as exc:
                out, err, rc, failure = exc.stdout or b"", exc.stderr or b"", None, "timeout"
            except OSError as exc:
                out, err, rc, failure = b"", b"", None, type(exc).__name__ + ": " + str(exc)
            save(folder / "stdout.bin", out)
            save(folder / "stderr.bin", err)
            item = {"directory": folder.name, "argv": argv, "returncode": rc, "failure": failure,
                    "stdout_bytes": len(out), "stderr_bytes": len(err),
                    "stdout_sha256": sha(out), "stderr_sha256": sha(err)}
            save(folder / "receipt.json", jb(item))
            item["receipt_sha256"] = sha((folder / "receipt.json").read_bytes())
            commands.append(item)
            if failure or rc != 0 or err:
                raise RuntimeError("new source capture failed: " + path)
            retain("sdk/" + path, out, "new:" + COMMIT)
        result["status"] = "captured"
    except BaseException as exc:
        result.update(status="failed", error=type(exc).__name__ + ": " + str(exc))
    result.update(inputs=inputs, commands=commands)
    save(run / "result.json", jb(result))
    print(json.dumps({"status": result["status"], "capture": run.name,
                      "result_sha256": sha((run / "result.json").read_bytes())}))
    return 0 if result["status"] == "captured" else 1

if __name__ == "__main__":
    raise SystemExit(main())

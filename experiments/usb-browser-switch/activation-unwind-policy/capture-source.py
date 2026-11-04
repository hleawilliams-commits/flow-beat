#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain exact source for the bounded USB unwind/attribution policy review.
No compiler, test, firmware, device, signal or hook execution is performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid

SDK_COMMIT = "3e0c5cb010454228f4ee63226b60d060494eeaae"
LOCAL_COMMIT = "11703ad30f00d28beeae3f1849fe2b2514d88272"
SDK_FILES = (
    "src/azmod/inline_hooks.c", "src/common/azprot.h",
    "src/azmod/azmod-mod.h", "src/azmod/community.c",
    "src/azmod/crash_guards.c", "src/azmod/azmod.c",
    "src/common/azaltstack.h", "src/azmod/ep_mem.h",
)
LOCAL_FILES = tuple("experiments/usb-browser-switch/activation-binding-composition/" + n
                    for n in ("entry.S", "observer.cpp", "binding.cpp"))
SEARCHES = {
    "named_unwind_nonlocal_exit_calls":
        r"\b(?:_Unwind_\w+|__register_frame\w*|__deregister_frame\w*|backtrace|"
        r"sigsetjmp|siglongjmp|setjmp|longjmp|pthread_cancel|pthread_setcancel\w*)\s*\(",
    "signal_attribution_and_install_sites":
        r"\b(?:cm_owner_of|cm_blame_fault|cm_crash|cr_handler|cr_chain|cr_rearm|"
        r"cm_claim_hook|azm_install_hook_at|azm_note_hooked|cr_read8|crash_peek8|"
        r"az_altstack_install|az_install_segv)\s*\(",
}
HERE = Path(__file__).resolve().parent

def sha(data):
    return hashlib.sha256(data).hexdigest()

def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as out:
        out.write(data)

def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    args = parser.parse_args()
    run = HERE / "private" / ("capture-" + uuid.uuid4().hex)
    run.mkdir(parents=True)
    inputs, commands = {}, []
    result = {"schema": 1, "status": "capturing",
              "sdk_commit": SDK_COMMIT, "local_commit": LOCAL_COMMIT,
              "scope": "Eight exact SDK source files plus three frozen observer sources; lexical calls are not whole-program/runtime evidence.",
              "compiler_or_test_execution": False, "firmware_or_device_access": False}
    def command(name, argv):
        folder = run / "commands" / ("%02d-" % (len(commands)+1) + name)
        save(folder / "command.json", json_bytes({"argv": argv, "timeout_seconds": 30}))
        failure = None
        try:
            proc = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  timeout=30, check=False)
            stdout, stderr, returncode = proc.stdout, proc.stderr, proc.returncode
        except subprocess.TimeoutExpired as exc:
            stdout, stderr, returncode = exc.stdout or b"", exc.stderr or b"", None
            failure = "timeout"
        except OSError as exc:
            stdout, stderr, returncode = b"", b"", None
            failure = type(exc).__name__ + ": " + str(exc)
        save(folder / "stdout.bin", stdout)
        save(folder / "stderr.bin", stderr)
        item = {"name": name, "directory": folder.name, "argv": argv,
                "returncode": returncode, "failure": failure,
                "stdout_bytes": len(stdout), "stderr_bytes": len(stderr),
                "stdout_sha256": sha(stdout), "stderr_sha256": sha(stderr)}
        save(folder / "receipt.json", json_bytes(item))
        item["receipt_sha256"] = sha((folder / "receipt.json").read_bytes())
        commands.append(item)
        if failure is not None or returncode != 0 or stderr:
            raise RuntimeError("source command failed: " + name)
        return stdout
    def retain(name, data, origin):
        save(run / "inputs" / name, data)
        inputs[name] = {"bytes": len(data), "sha256": sha(data),
                        "lines": len(data.splitlines()), "origin": origin}
    try:
        retain("capture-source.py", Path(__file__).read_bytes(), "new-review-capture")
        for origin, repository, commit, files in (
            ("sdk", args.upstream.absolute(), SDK_COMMIT, SDK_FILES),
            ("local", HERE.parents[2], LOCAL_COMMIT, LOCAL_FILES),
        ):
            for path in files:
                raw = command(origin + "-" + Path(path).name,
                    ["git", "-C", str(repository), "show", commit + ":" + path])
                retain(origin + "/" + path, raw, commit)
        result["searches"] = {}
        for label, pattern in SEARCHES.items():
            regex = re.compile(pattern)
            matches = []
            for path in SDK_FILES:
                data = (run / "inputs/sdk" / path).read_text()
                for line, text in enumerate(data.splitlines(), 1):
                    if regex.search(text):
                        matches.append({"file": path, "line": line,
                                        "line_sha256": sha(text.encode())})
            result["searches"][label] = {"pattern": pattern,
                "scope_files": list(SDK_FILES), "matches": matches}
        result["status"] = "captured"
    except BaseException as exc:
        result.update(status="failed", error=type(exc).__name__ + ": " + str(exc))
    result.update(inputs=inputs, commands=commands)
    save(run / "result.json", json_bytes(result))
    print(json.dumps({"status": result["status"], "capture": run.name,
                      "result_sha256": sha((run / "result.json").read_bytes())}))
    return 0 if result["status"] == "captured" else 1

if __name__ == "__main__":
    raise SystemExit(main())

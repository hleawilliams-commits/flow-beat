#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Three new compile-only SDK type probes; never link, execute, or install them."""
import sys
if not __debug__ or sys.flags.optimize:
    raise RuntimeError("optimized Python refused before output creation")
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import struct
import subprocess
import traceback
import uuid

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
IMAGE = "azmod-build:18.04"
IMAGE_ID = "sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90"
SDK_COMMIT = "3e0c5cb010454228f4ee63226b60d060494eeaae"
SDK_PATH = "src/azmod/azmod-mod.h"
SDK_SHA = "b94e2ba6656c2b852f18691d32225486f29265953c8bf09dcac9a18ce561a630"
OBSERVER_COMMIT = "c44f127a4e1137a834d321828379f2d4a4ae1878"
OBSERVER_PATH = "experiments/usb-browser-switch/activation-entry-observer/observer.h"
OBSERVER_SHA = "0d8cefb8a41f7af1a74c8e1897369eebb0d8c1ec0d44a7dbe57574d8434ca089"
OWN_INPUTS = (".gitignore", "type-probe.cpp", "run-compile.py")
FLAGS = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic-errors",
         "-fno-permissive", "-fdiagnostics-color=never", "-fno-diagnostics-show-caret", "-fPIC"]
ASSERT_MARKER = "SDK_CLAIM_HOOK_EXACT_VOID_DOUBLE_POINTER"
ATOMIC_CALL = "    return core->claim_hook(id, address, replacement, &az_usb_action_original);"
CASES = (("plain-slot", 0), ("direct-atomic-slot", 1), ("wrong-expected-signature", 2))

class CheckError(RuntimeError):
    pass

def require(ok, message):
    if not ok:
        raise CheckError(message)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def json_file(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")

class Run:
    def __init__(self):
        self.directory = HERE / "private" / ("run-" + uuid.uuid4().hex)
        self.directory.mkdir(parents=True)
        self.inputs = self.directory / "inputs"
        self.inputs.mkdir()
        (self.directory / "commands").mkdir()
        self.commands = []
        self.manifest = {}
        json_file(self.directory / "started.json", {"schema": 1, "compile_only": True,
                  "image_id": IMAGE_ID, "python": sys.version})

    def command(self, name, argv, container=None):
        dest = self.directory / "commands" / ("%02d-" % (len(self.commands) + 1) + name)
        dest.mkdir()
        json_file(dest / "command.json", {"argv": argv, "cwd": str(self.directory),
                  "timeout_seconds": 60, "container": container})
        proc = None
        stdout, stderr = b"", b""
        error, cleanup = None, None
        try:
            proc = subprocess.Popen(argv, cwd=self.directory, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, start_new_session=True)
            stdout, stderr = proc.communicate(timeout=60)
        except BaseException as exc:
            error = exc
            if isinstance(exc, subprocess.TimeoutExpired):
                stdout, stderr = exc.output or b"", exc.stderr or b""
            (dest / "interrupted-stdout.bin").write_bytes(stdout)
            (dest / "interrupted-stderr.bin").write_bytes(stderr)
            if proc is not None and proc.poll() is None:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            if container:
                cleanup = {"argv": ["docker", "rm", "-f", container]}
                try:
                    result = subprocess.run(cleanup["argv"], stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, timeout=20, check=False)
                    (dest / "cleanup-stdout.bin").write_bytes(result.stdout)
                    (dest / "cleanup-stderr.bin").write_bytes(result.stderr)
                    cleanup.update(returncode=result.returncode, stdout_sha256=digest(result.stdout),
                                   stderr_sha256=digest(result.stderr))
                except BaseException as cleanup_error:
                    cleanup["error"] = type(cleanup_error).__name__ + ": " + str(cleanup_error)
                    if isinstance(cleanup_error, subprocess.TimeoutExpired):
                        (dest / "cleanup-interrupted-stdout.bin").write_bytes(cleanup_error.output or b"")
                        (dest / "cleanup-interrupted-stderr.bin").write_bytes(cleanup_error.stderr or b"")
            if proc is not None:
                try:
                    stdout, stderr = proc.communicate(timeout=10)
                except BaseException as drain_error:
                    cleanup = {**(cleanup or {}), "drain_error": type(drain_error).__name__ + ": " + str(drain_error)}
        finally:
            (dest / "stdout.bin").write_bytes(stdout)
            (dest / "stderr.bin").write_bytes(stderr)
            receipt = {"argv": argv, "returncode": None if proc is None else proc.returncode,
                       "stdout_sha256": digest(stdout), "stderr_sha256": digest(stderr),
                       "stdout_bytes": len(stdout), "stderr_bytes": len(stderr),
                       "exception": None if error is None else type(error).__name__ + ": " + str(error),
                       "cleanup": cleanup}
            json_file(dest / "receipt.json", receipt)
            self.commands.append({"name": name, "directory": dest.name,
                                  "receipt_sha256": digest((dest / "receipt.json").read_bytes()), **receipt})
        if error is not None:
            raise error
        return proc.returncode, stdout, stderr

    def docker(self, name, args):
        container = "az-sdk-type-" + self.directory.name[4:] + "-" + str(len(self.commands) + 1)
        argv = ["docker", "run", "--name", container, "--rm", "--pull=never",
                "--network", "none", "--platform", "linux/arm64", "--env", "LC_ALL=C",
                "--mount", "type=bind,source=" + str(self.inputs) + ",target=/src,readonly",
                "--mount", "type=bind,source=" + str(self.directory) + ",target=/out",
                IMAGE_ID] + args
        return self.command(name, argv, container)

    def capture(self, upstream):
        for name in OWN_INPUTS:
            source = HERE / name
            require(source.is_file() and not source.is_symlink(), "regular own source " + name)
            data = source.read_bytes()
            (self.inputs / name).write_bytes(data)
            self.manifest[name] = digest(data)
        for label, repo, commit, path, expected, filename in (
            ("sdk-header", upstream, SDK_COMMIT, SDK_PATH, SDK_SHA, "azmod-mod.h"),
            ("observer-header", REPO, OBSERVER_COMMIT, OBSERVER_PATH, OBSERVER_SHA, "observer.h")):
            rc, data, _ = self.command(label, ["git", "-C", str(repo), "show", commit + ":" + path])
            require(rc == 0 and digest(data) == expected, "exact frozen " + label)
            (self.inputs / filename).write_bytes(data)
            self.manifest[filename] = digest(data)
        json_file(self.directory / "input-manifest.json", self.manifest)
        require({p.name: digest(p.read_bytes()) for p in self.inputs.iterdir()} == self.manifest,
                "all five captured inputs match")

def validate_diagnostic(name, stderr, source):
    text = stderr.decode("utf-8", "strict")
    lines = text.splitlines()
    error_lines = [line for line in lines if ": error: " in line]
    require(len(error_lines) == 1, name + " exactly one compiler error")
    if name == "direct-atomic-slot":
        require(source.splitlines().count(ATOMIC_CALL) == 1, "unique atomic call anchor")
        line = source.splitlines().index(ATOMIC_CALL) + 1
        pattern = (r"/src/type-probe\.cpp:" + str(line) + r":\d+: error: cannot convert "
                   r"'std::atomic<[^\n]+>\*' to 'void\*\*' in argument passing")
        require(re.fullmatch(pattern, error_lines[0]) is not None, "exact direct atomic-to-void** refusal")
        other = [s for s in lines if s != error_lines[0]]
        require(len(other) == 1 and re.fullmatch(
            r"/src/type-probe\.cpp: In function 'int sdk_type_probe\([^\n]+\)':", other[0]) is not None,
            "only expected enclosing-function diagnostic context")
        return {"oracle": "atomic-slot-not-void-double-pointer", "line": line,
                "error_count": 1, "diagnostic_sha256": digest(stderr)}
    require(name == "wrong-expected-signature", "known diagnostic case")
    anchors = [i + 1 for i, s in enumerate(source.splitlines()) if '"' + ASSERT_MARKER + '"' in s]
    require(len(anchors) == 1, "unique named static_assert anchor")
    pattern = (r"/src/type-probe\.cpp:" + str(anchors[0]) + r":\d+: error: static assertion failed: " + ASSERT_MARKER)
    require(len(lines) == 1 and re.fullmatch(pattern, lines[0]) is not None,
            "exact named SDK signature static_assert failure")
    return {"oracle": ASSERT_MARKER, "line": anchors[0], "error_count": 1,
            "diagnostic_sha256": digest(stderr)}

def object_metadata(path):
    require(path.is_file() and not path.is_symlink(), "positive object retained")
    data = path.read_bytes()
    require(len(data) >= 64 and data[:7] == b"\x7fELF\x02\x01\x01", "ELF64 little-endian object")
    kind, machine, version = struct.unpack_from("<HHI", data, 16)
    require((kind, machine, version) == (1, 183, 1), "AArch64 ET_REL object")
    require(struct.unpack_from("<Q", data, 24)[0] == 0, "relocatable object has no entry address")
    return {"sha256": digest(data), "bytes": len(data), "elf_type": "ET_REL",
            "machine": "AArch64", "class": 64, "endianness": "little", "executed": False}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, default=REPO / "../../../work/xdj-az-mods")
    args = parser.parse_args()
    run = Run()
    result = {"schema": 1, "status": "running", "compile_only": True,
              "candidate_linked": False, "candidate_executed": False, "hook_called": False,
              "firmware_or_device_access": False, "old_tests_replayed": False,
              "sdk_commit": SDK_COMMIT, "sdk_path": SDK_PATH,
              "observer_commit": OBSERVER_COMMIT, "observer_path": OBSERVER_PATH,
              "cases": [], "flags": FLAGS}
    try:
        run.capture(args.upstream.resolve())
        rc, out, err = run.command("image", ["docker", "image", "inspect", IMAGE,
            "--format", "{{.Id}} {{.Architecture}}"])
        require(rc == 0 and err == b"" and out == (IMAGE_ID + " arm64\n").encode(), "exact local image identity")
        rc, out, err = run.docker("target", ["g++", "-dumpmachine"])
        require(rc == 0 and err == b"" and out == b"aarch64-linux-gnu\n", "actual compiler target")
        rc, out, err = run.docker("compiler", ["g++", "--version"])
        require(rc == 0 and err == b"" and out.splitlines()[0] == b"g++ (Ubuntu/Linaro 7.5.0-3ubuntu1~18.04) 7.5.0", "reviewed compiler version")
        result["compiler"] = out.splitlines()[0].decode()
        source = (run.inputs / "type-probe.cpp").read_text()
        for name, case in CASES:
            dest = run.directory / name
            dest.mkdir()
            obj = dest / "probe.o"
            rc, out, err = run.docker(name, ["g++"] + FLAGS + ["-DTYPE_CASE=" + str(case),
                "-I/src", "-c", "/src/type-probe.cpp", "-o", "/out/" + name + "/probe.o"])
            require(out == b"", name + " no compiler stdout")
            if case == 0:
                require(rc == 0 and err == b"", "positive plain-slot compile without diagnostics")
                detail = {"oracle": "plain-slot-compiles", "object": object_metadata(obj)}
            else:
                require(rc == 1 and not obj.exists(), name + " compiler refusal and no object")
                detail = validate_diagnostic(name, err, source)
            result["cases"].append({"name": name, "returncode": rc, **detail})
        require({n: digest((HERE / n).read_bytes()) for n in OWN_INPUTS} ==
                {n: run.manifest[n] for n in OWN_INPUTS}, "own public inputs unchanged")
        require({p.name: digest(p.read_bytes()) for p in run.inputs.iterdir()} == run.manifest,
                "captured headers and sources unchanged")
        result["status"] = "passed"
    except BaseException as exc:
        result.update(status="failed", failure_type=type(exc).__name__, failure=str(exc))
        (run.directory / "failure.txt").write_text(traceback.format_exc())
    finally:
        result.update(input_sha256=run.manifest, commands=run.commands, image_id=IMAGE_ID)
        json_file(run.directory / "result.json", result)
        print(json.dumps({"status": result["status"], "private_run": run.directory.name,
                          "result_sha256": digest((run.directory / "result.json").read_bytes())}), flush=True)
    return 0 if result["status"] == "passed" else 1

if __name__ == "__main__":
    raise SystemExit(main())

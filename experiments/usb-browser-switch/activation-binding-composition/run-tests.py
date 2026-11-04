#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""New offline USB binding schedules only; no old fixture or native execution."""
import sys
if not __debug__ or sys.flags.optimize:
    raise RuntimeError("optimized Python refused before output creation")
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import traceback
import uuid

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
IMAGE = "azmod-build:18.04"
IMAGE_ID = "sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90"
PARENT_COMMIT = "f0dc732c9e27618f284bf340bfc0bab96fd8de04"
PARENT_PREFIX = "experiments/usb-browser-switch/"
PARENTS = {
    "activation-entry-observer/entry.S": "95c4936e1e2530f9e0d60ea6642b7ce8ca1203480986050d96a130c581da22d1",
    "activation-entry-observer/observer.cpp": "bd2f05ffd1e048652510c974eb890416cb39d974f05ad09696671e31a6595f2e",
    "activation-entry-observer/observer.h": "0d8cefb8a41f7af1a74c8e1897369eebb0d8c1ec0d44a7dbe57574d8434ca089",
    "activation-entry-observer/result.json": "7d519552f88e95d04dd0d6af1435ae7570d62385005d516673fad6111be98769",
    "activation-entry-observer/verification.json": "227aa2550b0288af47d0178e296d2e1fbc072ede9f762e9c32868f6207c10ec4",
    "activation-sdk-binding-review/README.md": "d3f9192b89f359eaca2578c75cc134a96712f898f909901f86d112135417ce03",
    "activation-sdk-binding-review/result.json": "0d2277d1a4019c21b30dc4609f41dea4286e4d5523239e60998d3571a1a43ebe"
}
SDK_COMMIT = "3e0c5cb010454228f4ee63226b60d060494eeaae"
SDK_PATH = "src/azmod/azmod-mod.h"
SDK_SHA = "b94e2ba6656c2b852f18691d32225486f29265953c8bf09dcac9a18ce561a630"
PARENT_OBJECT_SHA = "08a590bc165d5d29bf85467e5236be840b9e3f9b6b23b16fa55af02adf01e0d9"
SOURCES = (".gitignore", "entry.S", "observer.h", "observer.cpp", "binding.h",
           "binding.cpp", "test_binding.cpp", "run-tests.py")
FLAGS = ["-std=c++17", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-fexceptions",
         "-fasynchronous-unwind-tables", "-fno-omit-frame-pointer",
         "-fdiagnostics-color=never", "-fPIC", "-pthread"]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
SAN_ENV = ("ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=0",
           "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
# Source-reviewed new-only fixture counts and cleanup-safe exact mutations.
SCENARIOS = {
    "success": 4,
    "first-refusal": 3,
    "second-refusal": 3,
    "null-claim": 2,
    "missing-action-original": 2,
    "missing-service-original": 2,
    "reentrant-repeat": 3,
    "close-before-init": 2,
    "close-during-first": 3,
    "close-during-second": 3,
    "close-before-ready": 3,
    "close-admitted": 3,
    "close-during-first-refusal": 3,
    "close-during-second-refusal": 3
}
CONTROLS = [
    {
        "name": "temporary-first-slot",
        "file": "binding.cpp",
        "old": "    const int action_result = claim(\n        ModuleId, ActionAddress, reinterpret_cast<void*>(&az_usb_action_entry),\n        &az_usb_action_original);",
        "new": "    void* temporary_original = nullptr;\n    const int action_result = claim(\n        ModuleId, ActionAddress, reinterpret_cast<void*>(&az_usb_action_entry),\n        &temporary_original);",
        "selector": "success",
        "failures": [
            "success-sdk-contract",
            "success-claims-forward-closed",
            "success-ready-forwarding",
            "success-repeat-retains"
        ]
    },
    {
        "name": "reopen-after-close",
        "file": "binding.cpp",
        "old": "    if (!capture_state.compare_exchange_strong(\n            expected, State::Ready,\n            std::memory_order_acq_rel, std::memory_order_acquire)) {\n        return InstallResult::Closed;\n    }",
        "new": "    capture_state.store(State::Ready, std::memory_order_release);",
        "selector": "close-before-ready",
        "failures": [
            "close-before-ready-cas-wins",
            "close-before-ready-preserves-forwarding",
            "close-before-ready-terminal"
        ]
    },
    {
        "name": "failed-stage-reopens",
        "file": "binding.cpp",
        "old": "        expected, az_usb_binding::State::Failed,",
        "new": "        expected, az_usb_binding::State::Ready,",
        "selector": "second-refusal",
        "failures": [
            "second-refusal-result",
            "second-refusal-preserves-forwarding",
            "second-refusal-repeat"
        ]
    },
    {
        "name": "reset-first-after-second-refusal",
        "file": "binding.cpp",
        "old": "    if (service_result != 0) {\n        return fail(InstallResult::SecondRefused);\n    }",
        "new": "    if (service_result != 0) {\n        __atomic_store_n(&az_usb_action_original, static_cast<void*>(nullptr), __ATOMIC_RELAXED);\n        return fail(InstallResult::SecondRefused);\n    }",
        "selector": "second-refusal",
        "failures": [
            "second-refusal-preserves-forwarding",
            "second-refusal-repeat"
        ]
    },
    {
        "name": "failure-overwrites-close",
        "file": "binding.cpp",
        "old": "    auto expected = az_usb_binding::State::Installing;\n    capture_state.compare_exchange_strong(\n        expected, az_usb_binding::State::Failed,\n        std::memory_order_acq_rel, std::memory_order_acquire);",
        "new": "    capture_state.store(az_usb_binding::State::Failed, std::memory_order_release);",
        "selector": "close-during-first-refusal",
        "failures": [
            "close-first-refusal-keeps-closed",
            "close-first-refusal-terminal"
        ]
    }
]
DIAGNOSTIC = re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|"
                        r"runtime error:|DEADLYSIGNAL|Segmentation fault|"
                        r"terminate called|Aborted|FIXTURE_ABORT|warning:|error:", re.I)
PROBE = """#include <new>
int main() {
    volatile int* p = new volatile int[2];
    p[0] = 7;
    const int value = p[0];
    delete[] p;
    return value == 7 ? 0 : 1;
}
"""

class CheckError(RuntimeError):
    pass

def require(ok, message):
    if not ok:
        raise CheckError(message)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def sha(path):
    return digest(path.read_bytes())

def save_json(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")

def replace_once(source, before, after):
    require(source.count(before) == 1, "unique control anchor " + repr(before))
    return source.replace(before, after)

class Run:
    def __init__(self):
        self.directory = HERE / "private" / ("run-" + uuid.uuid4().hex)
        self.inputs = self.directory / "inputs"
        self.logs = self.directory / "commands"
        self.number = 0
        self.commands = []
        self.artifacts = []
        self.manifest = {}
        self.own_manifest = {}
        self.phase = "preparation"

    def prepare(self):
        self.directory.mkdir(parents=True)
        save_json(self.directory / "started.json", {"schema": 1, "status": "preparing",
                  "python": sys.version, "image": IMAGE, "image_id": IMAGE_ID,
                  "synthetic_only": True, "runtime_or_firmware_access": False})
        self.inputs.mkdir()
        self.logs.mkdir()

    def command(self, name, argv, *, container=None, timeout=60):
        self.number += 1
        log = self.logs / ("%03d-" % self.number + name)
        log.mkdir()
        save_json(log / "command.json", {"argv": argv, "timeout_seconds": timeout,
                  "container": container, "cwd": str(self.directory)})
        proc = None
        stdout = b""
        stderr = b""
        exceptional = None
        cleanup = None
        try:
            proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    cwd=self.directory, start_new_session=True)
            stdout, stderr = proc.communicate(timeout=timeout)
        except BaseException as exc:
            exceptional = exc
            if isinstance(exc, subprocess.TimeoutExpired):
                stdout, stderr = exc.output or b"", exc.stderr or b""
            try:
                (log / "interrupted-stdout.bin").write_bytes(stdout)
                (log / "interrupted-stderr.bin").write_bytes(stderr)
            except BaseException as ce:
                cleanup = {"interrupted_capture_error": type(ce).__name__ + ": " + str(ce)}
            finally:
                if proc is not None and proc.poll() is None:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                if container is not None:
                    cleanup_argv = ["docker", "rm", "-f", container]
                    cleanup = {**(cleanup or {}), "argv": cleanup_argv}
                    try:
                        cr = subprocess.run(cleanup_argv, stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE, timeout=20, check=False)
                        (log / "cleanup-stdout.bin").write_bytes(cr.stdout)
                        (log / "cleanup-stderr.bin").write_bytes(cr.stderr)
                        cleanup.update(returncode=cr.returncode,
                                       stdout_sha256=digest(cr.stdout), stderr_sha256=digest(cr.stderr))
                    except BaseException as ce:
                        cleanup["error"] = type(ce).__name__ + ": " + str(ce)
                        if isinstance(ce, subprocess.TimeoutExpired):
                            prefix_out, prefix_err = ce.output or b"", ce.stderr or b""
                            (log / "cleanup-interrupted-stdout.bin").write_bytes(prefix_out)
                            (log / "cleanup-interrupted-stderr.bin").write_bytes(prefix_err)
                            cleanup.update(interrupted_stdout_sha256=digest(prefix_out),
                                           interrupted_stderr_sha256=digest(prefix_err))
                if proc is not None:
                    try:
                        stdout, stderr = proc.communicate(timeout=10)
                    except BaseException as de:
                        if cleanup is None:
                            cleanup = {}
                        cleanup["drain_error"] = type(de).__name__ + ": " + str(de)
        finally:
            (log / "stdout.bin").write_bytes(stdout)
            (log / "stderr.bin").write_bytes(stderr)
            receipt = {"argv": argv, "returncode": proc.returncode if proc is not None else None,
                       "stdout_bytes": len(stdout), "stderr_bytes": len(stderr),
                       "stdout_sha256": digest(stdout), "stderr_sha256": digest(stderr),
                       "exception": None if exceptional is None else
                                    type(exceptional).__name__ + ": " + str(exceptional),
                       "cleanup": cleanup}
            save_json(log / "receipt.json", receipt)
            self.commands.append({"name": name, "directory": log.name,
                                  "receipt_sha256": sha(log / "receipt.json"), **receipt})
        if exceptional is not None:
            raise exceptional
        return proc.returncode, stdout, stderr

    def docker(self, name, command, env=()):
        container = "az-usb-binding-" + self.directory.name[4:] + "-" + str(self.number + 1)
        argv = ["docker", "run", "--name", container, "--rm", "--pull=never",
                "--network", "none", "--platform", "linux/arm64", "--env", "LC_ALL=C",
                "--mount", "type=bind,source=" + str(self.inputs) + ",target=/src,readonly",
                "--mount", "type=bind,source=" + str(self.directory) + ",target=/out",
                *sum((["--env", value] for value in env), []), IMAGE_ID] + command
        return self.command(name, argv, container=container)

    def zero(self, name, command):
        rc, stdout, stderr = self.docker(name, command)
        require(rc == 0, name + " returned " + str(rc))
        require(not DIAGNOSTIC.search((stdout + stderr).decode("utf-8", "replace")),
                name + " unexpected diagnostic")
        return stdout, stderr

    def artifact(self, relative):
        p = self.directory / relative
        require(p.is_file() and not p.is_symlink(), "missing regular artifact " + relative)
        item = {"path": relative, "bytes": p.stat().st_size, "sha256": sha(p)}
        self.artifacts.append(item)
        return item


    def put_input(self, name, data, role):
        target = self.inputs / name
        target.parent.mkdir(parents=True, exist_ok=True)
        require(not target.exists(), "input path already exists " + name)
        target.write_bytes(data)
        self.manifest[name] = {"sha256": digest(data), "bytes": len(data), "role": role}

    def capture(self, upstream, parent_object):
        for name in SOURCES:
            p = HERE / name
            require(p.is_file() and not p.is_symlink(), "regular new source " + name)
            data = p.read_bytes()
            self.put_input(name, data, "new")
            self.own_manifest[name] = digest(data)
        rc, sdk, _ = self.command("sdk-header", ["git", "-C", str(upstream), "show",
                                 SDK_COMMIT + ":" + SDK_PATH])
        require(rc == 0 and digest(sdk) == SDK_SHA, "exact SDK header")
        self.put_input("azmod-mod.h", sdk, "sdk")
        for relative, expected in PARENTS.items():
            rc, data, _ = self.command("parent-" + relative.replace("/", "-").replace(".", "-"),
                                      ["git", "-C", str(REPO), "show",
                                       PARENT_COMMIT + ":" + PARENT_PREFIX + relative])
            require(rc == 0 and digest(data) == expected, "exact tracked parent " + relative)
            self.put_input("parents/" + relative, data, "parent")
        require((self.inputs / "entry.S").read_bytes() ==
                (self.inputs / "parents/activation-entry-observer/entry.S").read_bytes(),
                "entry.S remains byte-identical to tracked parent")
        require(parent_object.is_file() and not parent_object.is_symlink(), "regular retained parent object")
        parent_bytes = parent_object.read_bytes()
        require(digest(parent_bytes) == PARENT_OBJECT_SHA, "exact retained parent entry object")
        previous = json.loads((self.inputs / "parents/activation-entry-observer/result.json").read_bytes())
        recorded = [a for a in previous["artifacts"] if a["path"] == "build/candidate/entry.o"]
        require(len(recorded) == 1 and recorded[0]["sha256"] == PARENT_OBJECT_SHA
                and recorded[0]["bytes"] == len(parent_bytes), "parent receipt object binding")
        self.put_input("parents/entry.o", parent_bytes, "retained-parent-object-not-executed")
        save_json(self.directory / "input-manifest.json", self.manifest)
        self.check_snapshot()
        provenance = self.directory / "provenance"
        provenance.mkdir()
        for name in ("observer.h", "observer.cpp"):
            before = (self.inputs / "parents/activation-entry-observer" / name).read_text()
            after = (self.inputs / name).read_text()
            diff = "".join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                               fromfile="frozen/" + name, tofile="candidate/" + name))
            (provenance / (name + ".diff")).write_text(diff)
            self.artifact("provenance/" + name + ".diff")

    def check_snapshot(self):
        actual = {}
        for p in self.inputs.rglob("*"):
            if p.is_file():
                require(not p.is_symlink(), "snapshot symlink")
                actual[p.relative_to(self.inputs).as_posix()] = (sha(p), p.stat().st_size)
        require(actual == {n: (v["sha256"], v["bytes"]) for n, v in self.manifest.items()},
                "complete immutable snapshot inventory")

    def production(self):
        dest = self.directory / "build/production"
        dest.mkdir(parents=True)
        out = "/out/build/production/"
        self.zero("production-entry-compile", ["g++", "-g", "-fPIC", "-c",
                  "/src/entry.S", "-o", out + "entry.o"])
        for name in ("observer", "binding"):
            self.zero("production-" + name + "-compile", ["g++"] + FLAGS +
                      ["-I/src", "-c", "/src/" + name + ".cpp", "-o", out + name + ".o"])
        for name in ("entry", "observer", "binding"):
            self.artifact("build/production/" + name + ".o")
        return check_production(self)

    def sanitizer_available(self):
        libraries = {}
        for library in ("libasan.so", "libubsan.so"):
            stdout, stderr = self.zero("locate-" + library, ["g++", "-print-file-name=" + library])
            require(stderr == b"", "runtime path stderr")
            path = stdout.decode("utf-8", "strict").strip()
            require(path and "\n" not in path, "single runtime path")
            rc, out, err = self.docker("readable-" + library, ["test", "-r", path])
            require(rc in (0, 1) and out == b"" and err == b"", "runtime file probe")
            libraries[library] = {"reported_path": path, "readable": rc == 0 and path != library}
        if not all(v["readable"] for v in libraries.values()):
            return {"available": False, "reason": "compiler runtime library absent",
                    "libraries": libraries, "instrumented_tests_executed": False}
        dest = self.directory / "build/sanitizer-probe"
        dest.mkdir()
        (dest / "probe.cpp").write_text(PROBE)
        self.artifact("build/sanitizer-probe/probe.cpp")
        self.zero("sanitizer-probe-compile-link", ["g++"] + FLAGS + SAN_FLAGS +
                  ["/out/build/sanitizer-probe/probe.cpp", "-o", "/out/build/sanitizer-probe/probe"])
        self.artifact("build/sanitizer-probe/probe")
        rc, out, err = self.docker("sanitizer-probe-execute",
                                   ["/out/build/sanitizer-probe/probe"], SAN_ENV)
        require(rc == 0 and out == b"" and err == b"",
                "sanitizer probe must pass; a runtime failure is not an unavailable skip")
        return {"available": True, "libraries": libraries, "probe_passed": True,
                "flags": SAN_FLAGS, "environment": SAN_ENV}

    def build_fixture(self, variant, mode, source="/src"):
        dest = self.directory / "build" / variant / mode
        dest.mkdir(parents=True)
        out = "/out/build/" + variant + "/" + mode + "/"
        flags = FLAGS + (SAN_FLAGS if mode == "asan-ubsan" else [])
        for name in ("observer", "binding", "test_binding"):
            self.zero(variant + "-" + mode + "-" + name + "-compile", ["g++"] + flags +
                      ["-DAZ_USB_BINDING_FIXTURE", "-I" + source, "-I/src", "-c",
                       source + "/" + name + ".cpp", "-o", out + name + ".o"])
        self.zero(variant + "-" + mode + "-link", ["g++"] + flags +
                  ["/out/build/production/entry.o"] +
                  [out + n + ".o" for n in ("observer", "binding", "test_binding")] +
                  ["-o", out + "test-binding"])
        for name in ("observer.o", "binding.o", "test_binding.o", "test-binding"):
            self.artifact("build/" + variant + "/" + mode + "/" + name)
        return out + "test-binding"

    def execute(self, variant, mode, binary, selector, failures=()):
        require(selector in SCENARIOS, "known selector")
        env = SAN_ENV if mode == "asan-ubsan" else ()
        rc, out, err = self.docker(variant + "-" + mode + "-" + selector, [binary, selector], env)
        text = out.decode("utf-8", "strict")
        require(err == b"" and not DIAGNOSTIC.search(text), "no fixture/sanitizer diagnostics")
        lines = text.splitlines()
        found = [line[5:] for line in lines if line.startswith("FAIL ")]
        result = [line for line in lines if line.startswith("RESULT ")]
        require(len(result) == 1, "one result for " + selector)
        match = re.fullmatch(r"RESULT " + re.escape(selector) + r" checks=(\d+) failures=(\d+)", result[0])
        require(match is not None, "exact result syntax")
        checks, count = map(int, match.groups())
        require(checks == SCENARIOS[selector], "source-reviewed check count")
        require(count == len(found) and sorted(found) == sorted(failures), "exact named failure set")
        require(len(lines) == 1 + len(found), "no unexpected fixture output")
        require(rc == (1 if failures else 0), "exact semantic exit code")
        return {"selector": selector, "checks": checks, "expected_failures": list(failures),
                "observed_failures": found, "returncode": rc,
                "stdout_sha256": digest(out), "stderr_sha256": digest(err)}


class Object:
    """Minimal read-only ELF64 REL metadata reader; never executes an object."""
    def __init__(self, data):
        self.data = data
        require(len(data) >= 64 and data[:7] == b"\x7fELF\x02\x01\x01", "ELF64 little-endian object")
        hdr = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
        require(hdr[1] == 1 and hdr[2] == 183 and hdr[3] == 1, "AArch64 REL object")
        shoff, shentsize, shnum, shstrndx = hdr[6], hdr[11], hdr[12], hdr[13]
        require(shentsize == 64 and 0 < shnum < 65535 and shstrndx < shnum,
                "ordinary complete section table")
        require(shoff + shnum * 64 <= len(data), "section table bounds")
        self.sections = []
        for i in range(shnum):
            row = struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * 64)
            s = dict(zip(("nameoff", "type", "flags", "address", "offset", "size",
                          "link", "info", "align", "entsize"), row))
            require(s["type"] == 8 or s["offset"] + s["size"] <= len(data), "section data bounds")
            s["data"] = b"" if s["type"] == 8 else data[s["offset"]:s["offset"] + s["size"]]
            self.sections.append(s)
        names = self.sections[shstrndx]["data"]
        for s in self.sections:
            s["name"] = self.string(names, s["nameoff"])
        self.byname = {}
        for i, s in enumerate(self.sections):
            require(s["name"] not in self.byname, "unique section name")
            self.byname[s["name"]] = (i, s)
        self.symbol_tables = {}
        for i, s in enumerate(self.sections):
            if s["type"] != 2:
                continue
            require(s["entsize"] == 24 and s["size"] % 24 == 0 and s["link"] < shnum,
                    "symbol table layout")
            strings = self.sections[s["link"]]["data"]
            table = []
            for at in range(0, s["size"], 24):
                name, info, other, index, value, size = struct.unpack_from("<IBBHQQ", s["data"], at)
                table.append({"name": self.string(strings, name), "type": info & 15,
                              "bind": info >> 4, "other": other, "section": index,
                              "value": value, "size": size})
            self.symbol_tables[i] = table
        require(len(self.symbol_tables) == 1, "one complete regular symbol table")
        self.symbols = next(iter(self.symbol_tables.values()))

    @staticmethod
    def string(data, at):
        require(0 <= at < len(data), "string table offset")
        end = data.find(b"\0", at)
        require(end >= at, "terminated string")
        return data[at:end].decode("utf-8", "strict")

    def section(self, name):
        require(name in self.byname, "required object section " + name)
        return self.byname[name][1]

    def relocations(self, target):
        target_index = self.byname[target][0]
        result = []
        for s in self.sections:
            if s["type"] != 4 or s["info"] != target_index:
                continue
            require(s["entsize"] == 24 and s["size"] % 24 == 0
                    and s["link"] in self.symbol_tables, "RELA layout")
            symbols = self.symbol_tables[s["link"]]
            for at in range(0, s["size"], 24):
                offset, info, addend = struct.unpack_from("<QQq", s["data"], at)
                index, kind = info >> 32, info & 0xffffffff
                require(index < len(symbols) and offset < self.section(target)["size"],
                        "relocation target bounds")
                symbol = symbols[index]
                name = symbol["name"]
                if symbol["type"] == 3: # A section symbol has an empty string name.
                    require(symbol["section"] < len(self.sections), "section symbol index")
                    name = "@section:" + self.sections[symbol["section"]]["name"]
                result.append({"offset": offset, "type": kind, "symbol": name,
                               "symbol_value": symbol["value"], "addend": addend})
        return sorted(result, key=lambda r: (r["offset"], r["type"], r["symbol"], r["addend"]))

def check_production(run):
    directory = run.directory / "build/production"
    current_bytes = (directory / "entry.o").read_bytes()
    parent_bytes = (run.inputs / "parents/entry.o").read_bytes()
    current, parent = Object(current_bytes), Object(parent_bytes)
    compared = {}
    for name in (".text", ".eh_frame"):
        now, old = current.section(name), parent.section(name)
        require(now["data"] == old["data"] and now["align"] == old["align"],
                "entry object section parity " + name)
        new_rel, old_rel = current.relocations(name), parent.relocations(name)
        require(new_rel == old_rel, "entry object relocation parity " + name)
        compared[name] = {"bytes": now["size"], "sha256": digest(now["data"]),
                          "alignment": now["align"], "relocations": new_rel}
    executable = [s["name"] for s in current.sections if s["flags"] & 4 and s["size"]]
    require(executable == [".text"], "only reviewed entry machine code")
    entries = [s for s in current.symbols if s["type"] == 2 and s["section"] != 0]
    require({s["name"] for s in entries} == {"az_usb_action_entry", "az_usb_service_entry"}
            and all(s["size"] == 348 for s in entries), "two frozen entry extents")
    objects = {name: Object((directory / (name + ".o")).read_bytes())
               for name in ("observer", "binding")}
    for name, obj in objects.items():
        forbidden = [s["name"] for s in obj.symbols if "fixture_" in s["name"]]
        require(not forbidden, "production " + name + " has no fixture hook reference/definition")
    observed = objects["observer"]
    require(any(s["name"] == "az_usb_entry_observe" and s["type"] == 2 and s["section"] != 0
                for s in observed.symbols), "production helper definition")
    slots = {}
    for name in ("az_usb_action_original", "az_usb_service_original"):
        found = [s for s in observed.symbols if s["name"] == name]
        require(len(found) == 1, "one original slot " + name)
        s = found[0]
        require(s["type"] == 1 and s["bind"] == 1 and s["size"] == 8
                and 0 < s["section"] < len(observed.sections), "global eight-byte pointer object")
        section = observed.sections[s["section"]]
        require(section["type"] == 8 and section["align"] >= 8 and s["value"] % 8 == 0,
                "pointer slot BSS size/alignment")
        slots[name] = {"section": section["name"], "section_alignment": section["align"],
                       "section_offset": s["value"], "bytes": s["size"]}
    summary = {"helper_binding_compiled_only": True,
               "entry_object_reused_by_new_fixture": True,
               "fixture_defines": [], "original_slots": slots,
               "entry_source_identical": True, "parent_object_sha256": digest(parent_bytes),
               "entry_object_sha256": digest(current_bytes),
               "entry_object_whole_bytes_equal": current_bytes == parent_bytes,
               "sections_and_relocations_equal": compared,
               "parent_fixture_or_object_executed": False}
    save_json(directory / "metadata.json", summary)
    run.artifact("build/production/metadata.json")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True,
                        help="local repository containing the pinned SDK commit")
    parser.add_argument("--parent-entry-object", type=Path, required=True,
                        help="retained frozen parent candidate entry.o; read-only comparison")
    args = parser.parse_args()
    run = Run()
    report = {"schema": 1, "status": "running", "synthetic_only": True,
              "native_firmware_emulator_device_access": False, "parent_suites_replayed": False,
              "tsan_executed": False, "production_configuration_executed": False,
              "fixture_macros": ["AZ_USB_BINDING_FIXTURE"],
              "fixture_scheduling_seams": ["before-ready", "after-admission"],
              "candidate": [], "controls": []}
    try:
        run.prepare()
        run.capture(args.upstream.absolute(), args.parent_entry_object.absolute())
        require(SCENARIOS and 3 <= len(CONTROLS) <= 5,
                "source-reviewed scenario/control inventory must be finalized")
        test = (run.inputs / "test_binding.cpp").read_text()
        require(len(SCENARIOS) == 14 and sum(SCENARIOS.values()) == 39,
                "reviewed fourteen-selector, thirty-nine-check inventory")
        selected = re.findall(r'if \(name == "([^"]+)"\)', test)
        require(len(selected) == len(SCENARIOS) and set(selected) == set(SCENARIOS),
                "exact fixture selector inventory")
        require(len({c["name"] for c in CONTROLS}) == len(CONTROLS), "unique control names")
        for control in CONTROLS:
            require(control["selector"] in SCENARIOS and control["failures"],
                    "control selector/oracles")
            require(control["file"] in ("observer.cpp", "binding.cpp"),
                    "controls mutate only new helper/binder source")
            replace_once((run.inputs / control["file"]).read_text(),
                         control["old"], control["new"])
            for oracle in control["failures"]:
                require(test.count('"' + oracle + '"') == 1, "unique final oracle " + oracle)
        report["scenario_inventory"] = SCENARIOS
        run.phase = "image"
        rc, out, err = run.command("image", ["docker", "image", "inspect", IMAGE,
                                            "--format", "{{.Id}} {{.Architecture}}"])
        require(rc == 0 and err == b"" and out.decode().strip() == IMAGE_ID + " arm64",
                "exact locally available ARM64 image")
        out, err = run.zero("platform", ["uname", "-m"])
        require(out == b"aarch64\n" and err == b"", "actual ARM64 container")
        out, err = run.zero("compiler", ["g++", "--version"])
        require(err == b"", "compiler version stderr")
        report["compiler_version"] = out.decode()
        run.phase = "production"
        report["production"] = run.production()
        run.phase = "sanitizer-availability"
        report["sanitizer"] = run.sanitizer_available()
        modes = ["plain"] + (["asan-ubsan"] if report["sanitizer"]["available"] else [])
        report["modes_executed"] = modes
        for mode in modes:
            run.phase = "candidate:" + mode
            binary = run.build_fixture("candidate", mode)
            for selector in SCENARIOS:
                outcome = run.execute("candidate", mode, binary, selector)
                report["candidate"].append({"mode": mode, **outcome})
        for control in CONTROLS:
            run.phase = "control:" + control["name"]
            dst = run.directory / "variants" / control["name"]
            dst.mkdir(parents=True)
            for name in SOURCES:
                shutil.copyfile(run.inputs / name, dst / name)
            shutil.copyfile(run.inputs / "azmod-mod.h", dst / "azmod-mod.h")
            target = dst / control["file"]
            target.write_text(replace_once(target.read_text(), control["old"], control["new"]))
            source_manifest = {p.name: sha(p) for p in dst.iterdir() if p.is_file()}
            save_json(dst / "source-manifest.json", source_manifest)
            detail = {**control, "source_sha256": source_manifest, "outcomes": []}
            report["controls"].append(detail)
            for mode in modes:
                run.phase = "control:" + control["name"] + ":" + mode
                binary = run.build_fixture(control["name"], mode, "/out/variants/" + control["name"])
                outcome = run.execute(control["name"], mode, binary,
                                      control["selector"], control["failures"])
                detail["outcomes"].append({"mode": mode, **outcome})
        require({n: sha(HERE / n) for n in SOURCES} == run.own_manifest, "live new inputs unchanged")
        run.check_snapshot()
        report["status"] = "passed"
        report["selected_checks_per_mode"] = sum(SCENARIOS.values())
        report["selected_scenarios_per_mode"] = len(SCENARIOS)
        report["strict_controls_per_mode"] = len(CONTROLS)
    except BaseException as exc:
        report.update(status="failed", failure_type=type(exc).__name__, failure=str(exc))
        if run.directory.exists():
            try:
                (run.directory / "failure.txt").write_text(traceback.format_exc())
            except BaseException as capture_error:
                report["failure_capture_error"] = type(capture_error).__name__ + ": " + str(capture_error)
    finally:
        report.update(phase=run.phase, image=IMAGE, image_id=IMAGE_ID,
                      sdk_commit=SDK_COMMIT, parent_commit=PARENT_COMMIT,
                      inputs=run.manifest, commands=run.commands, artifacts=run.artifacts)
        result_sha = None
        if run.directory.exists():
            save_json(run.directory / "result.json", report)
            result_sha = sha(run.directory / "result.json")
        print(json.dumps({"status": report["status"], "private_run": run.directory.name,
                          "result_sha256": result_sha}), flush=True)
    return 0 if report["status"] == "passed" else 1

if __name__ == "__main__":
    raise SystemExit(main())

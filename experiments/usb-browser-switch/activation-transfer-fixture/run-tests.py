#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""New authored-target SDK transfer integration only; no firmware/device execution."""
import sys
if not __debug__ or sys.flags.optimize:
    raise RuntimeError("optimized Python refused before output creation")
import argparse
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
PARENT_COMMIT = "f6033a026737169c8476f988c9701a8625d4f702"
PARENT_PREFIX = "experiments/usb-browser-switch/activation-binding-composition/"
PARENTS = {
    "entry.S": "95c4936e1e2530f9e0d60ea6642b7ce8ca1203480986050d96a130c581da22d1",
    "observer.h": "999b8bebd58d37912f41e37330ecee60175a8f6a4d93d137d96892e13d7d14f6",
    "observer.cpp": "0969955fb87f47f2fc909a1a4419bf0ec9ad2ac1b91244c60df8908f5c4ebd03",
    "binding.h": "b3c2b1304b8cd0d0a83a69e9508aa32edfcabdcf67dd329a15c2f76d245f1ce1",
    "binding.cpp": "de9e83210510523cca7be6096eac3b53e32af73442167546b7469a72a5287163",
}
SDK_COMMIT = "3e0c5cb010454228f4ee63226b60d060494eeaae"
SDK = {
    "src/azmod/azmod-mod.h": ("azmod-mod.h", "b94e2ba6656c2b852f18691d32225486f29265953c8bf09dcac9a18ce561a630"),
    "src/azmod/inline_hooks.c": ("sdk/src/azmod/inline_hooks.c", "f141b383c25e66c8acb1839280832a612a6ee0e25e9ada59f374ccbef8f81908"),
    "src/common/azprot.h": ("sdk/src/common/azprot.h", "0f1cc9c5d4ca212f798bf2e9d74a69507a82ce5cc59bd328bacc46816a119b33"),
}
SOURCES = (".gitignore", "run-tests.py", "bridge.h", "bridge.c", "fixture.S", "test_transfer.cpp")
COMMON = ["-O2", "-g", "-Wall", "-Wextra", "-Werror", "-fexceptions",
          "-fasynchronous-unwind-tables", "-fno-omit-frame-pointer",
          "-fdiagnostics-color=never", "-fno-pie", "-pthread"]
CFLAGS = ["-std=c11"] + COMMON
CXXFLAGS = ["-std=c++17"] + COMMON
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
SAN_ENV = ("ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=0",
           "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
# Must be finalized against the independently reviewed fixture before any Docker call.
SCENARIOS = {"action-return": 8, "service-return": 8, "action-throw": 8, "service-throw": 8}
SCRATCH_FAILURES = {name: ["emitted-route", "body-scratch"] for name in SCENARIOS}
NATIVE_TARGETS = {"fixture_action_target": 144, "fixture_service_target": 272}
CFI_CONTROLS = [
    {"name": "wrong-action-cfa", "file": "fixture.S",
     "old": ".cfi_def_cfa_offset 144", "new": ".cfi_def_cfa_offset 160",
     "target": "fixture_action_target", "failures": ["fixture_action_target:post-stp-cfa"]},
    {"name": "wrong-service-cfa", "file": "fixture.S",
     "old": ".cfi_def_cfa_offset 272", "new": ".cfi_def_cfa_offset 288",
     "target": "fixture_service_target", "failures": ["fixture_service_target:post-stp-cfa"]},
]
SCRATCH_CONTROL = {
    "name": "installer-x16-scratch",
    "file": "sdk/src/azmod/inline_hooks.c",
    "old": "    dst[0] = 0x58000051u;\n    dst[1] = 0xD61F0220u;",
    "new": "    dst[0] = 0x58000050u;\n    dst[1] = 0xD61F0200u;",
}
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
        self.execution_attempts = []
        self.phase = "preparation"

    def prepare(self):
        self.directory.mkdir(parents=True)
        save_json(self.directory / "started.json", {"schema": 1, "status": "preparing",
                  "python": sys.version, "image": IMAGE, "image_id": IMAGE_ID,
                  "synthetic_only": True, "firmware_or_device_access": False})
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
        container = "az-usb-transfer-" + self.directory.name[4:] + "-" + str(self.number + 1)
        argv = ["docker", "run", "--name", container, "--rm", "--pull=never",
                "--network", "none", "--platform", "linux/arm64", "--env", "LC_ALL=C",
                "--mount", "type=bind,source=" + str(self.inputs) + ",target=/src,readonly",
                "--mount", "type=bind,source=" + str(self.directory) + ",target=/out",
                *sum((["--env", value] for value in env), []), IMAGE_ID] + command
        return self.command(name, argv, container=container)

    def zero(self, name, command):
        rc, stdout, stderr = self.docker(name, command)
        require(rc == 0, name + " returned " + str(rc))
        require(stderr == b"" and not DIAGNOSTIC.search(stdout.decode("utf-8", "replace")),
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


    def capture(self, upstream):
        for name in SOURCES:
            p = HERE / name
            require(p.is_file() and not p.is_symlink(), "regular new source " + name)
            data = p.read_bytes()
            self.put_input(name, data, "new-fixture")
            self.own_manifest[name] = digest(data)
        for name, expected in PARENTS.items():
            rc, data, err = self.command("parent-" + name.replace(".", "-"),
                ["git", "-C", str(REPO), "show", PARENT_COMMIT + ":" + PARENT_PREFIX + name])
            require(rc == 0 and err == b"" and digest(data) == expected, "exact frozen production " + name)
            self.put_input(name, data, "unchanged-production")
        for relative, (name, expected) in SDK.items():
            rc, data, err = self.command("sdk-" + Path(relative).name.replace(".", "-"),
                ["git", "-C", str(upstream), "show", SDK_COMMIT + ":" + relative])
            require(rc == 0 and err == b"" and digest(data) == expected, "exact SDK " + relative)
            self.put_input(name, data, "unchanged-sdk")
        save_json(self.directory / "input-manifest.json", self.manifest)
        self.check_snapshot()

    def check_snapshot(self):
        actual = {}
        for p in self.inputs.rglob("*"):
            require(not p.is_symlink(), "snapshot symlink")
            if p.is_file():
                actual[p.relative_to(self.inputs).as_posix()] = (sha(p), p.stat().st_size)
        require(actual == {n: (v["sha256"], v["bytes"]) for n, v in self.manifest.items()},
                "complete immutable snapshot inventory")

    def sanitizer_available(self):
        libraries = {}
        for library in ("libasan.so", "libubsan.so"):
            stdout, stderr = self.zero("locate-" + library, ["g++", "-print-file-name=" + library])
            path = stdout.decode("utf-8", "strict").strip()
            require(path and "\n" not in path, "single runtime path")
            rc, out, err = self.docker("readable-" + library, ["test", "-r", path])
            require(rc in (0, 1) and out == b"" and err == b"", "runtime file probe")
            libraries[library] = {"reported_path": path, "readable": rc == 0 and path != library}
        require(all(v["readable"] for v in libraries.values()),
                "required ASan/UBSan runtime unavailable; not a passing skip: " + repr(libraries))
        dest = self.directory / "build/sanitizer-probe"
        dest.mkdir(parents=True)
        (dest / "probe.cpp").write_text(PROBE)
        self.artifact("build/sanitizer-probe/probe.cpp")
        self.zero("sanitizer-probe-compile-link", ["g++"] + CXXFLAGS + SAN_FLAGS +
                  ["-no-pie", "/out/build/sanitizer-probe/probe.cpp", "-o",
                   "/out/build/sanitizer-probe/probe"])
        self.artifact("build/sanitizer-probe/probe")
        rc, out, err = self.docker("sanitizer-probe-execute",
                                  ["/out/build/sanitizer-probe/probe"], SAN_ENV)
        require(rc == 0 and out == b"" and err == b"", "sanitizer smoke must pass")
        return {"available": True, "libraries": libraries, "probe_passed": True,
                "flags": SAN_FLAGS, "environment": SAN_ENV}

    def build_fixture(self, variant, mode, source="/src"):
        relative = "build/" + variant + "/" + mode
        dest = self.directory / relative
        dest.mkdir(parents=True)
        out = "/out/" + relative + "/"
        san = SAN_FLAGS if mode == "asan-ubsan" else []
        for name in ("entry", "fixture"):
            self.zero(variant + "-" + mode + "-" + name + "-compile",
                      ["gcc", "-g", "-fno-pie", "-c", source + "/" + name + ".S", "-o", out + name + ".o"])
        self.zero(variant + "-" + mode + "-bridge-compile",
                  ["gcc"] + CFLAGS + san + ["-I" + source, "-c", source + "/bridge.c", "-o", out + "bridge.o"])
        for name in ("observer", "binding", "test_transfer"):
            self.zero(variant + "-" + mode + "-" + name + "-compile",
                      ["g++"] + CXXFLAGS + san +
                      ["-I" + source, "-c", source + "/" + name + ".cpp", "-o", out + name + ".o"])
        objects = ("entry", "fixture", "bridge", "observer", "binding", "test_transfer")
        self.zero(variant + "-" + mode + "-link",
                  ["g++"] + CXXFLAGS + san + ["-no-pie"] + [out + n + ".o" for n in objects] +
                  ["-o", out + "test-transfer"])
        for name in objects:
            self.artifact(relative + "/" + name + ".o")
        self.artifact(relative + "/test-transfer")
        data = (dest / "test-transfer").read_bytes()
        require(data[:7] == b"\x7fELF\x02\x01\x01" and
                struct.unpack_from("<HH", data, 16) == (2, 183), "non-PIE AArch64 executable")
        obj = Object((dest / "fixture.o").read_bytes())
        for name in ("observer", "binding"):
            compiled = Object((dest / (name + ".o")).read_bytes())
            require(not [s for s in compiled.symbols if "fixture_" in s["name"]],
                    "unmodified production " + name + " has no fixture scheduling seam")
        texts = {}
        for label, command in (
            ("fixture-disassembly", ["objdump", "-dr", out + "fixture.o"]),
            ("fixture-frames", ["readelf", "-W", "--debug-dump=frames-interp", out + "fixture.o"]),
            ("linked-metadata", ["readelf", "-W", "-h", "-l", "-s", out + "test-transfer"]),
            ("bridge-symbols", ["nm", "-a", out + "bridge.o"])):
            stdout, _ = self.zero(variant + "-" + mode + "-" + label, command)
            (dest / (label + ".txt")).write_bytes(stdout)
            self.artifact(relative + "/" + label + ".txt")
            texts[label] = stdout.decode("utf-8", "strict")
        metadata = check_native(obj, texts["fixture-frames"])
        require(metadata["failures"] == [], "authored target entry/+4 CFI agrees")
        save_json(dest / "native-metadata.json", metadata)
        self.artifact(relative + "/native-metadata.json")
        return out + "test-transfer", metadata

    def execute(self, variant, mode, binary, selector, failures=()):
        require(selector in SCENARIOS, "known selector")
        env = SAN_ENV if mode == "asan-ubsan" else ()
        relative = "evidence/" + variant + "/" + mode + "/" + selector
        dest = self.directory / relative
        dest.mkdir(parents=True)
        attempt = {"variant": variant, "mode": mode, "selector": selector,
                   "requested": True, "returned": False, "accepted": False}
        self.execution_attempts.append(attempt)
        rc, out, err = self.docker(variant + "-" + mode + "-" + selector,
                                  [binary, selector, "/out/" + relative + "/report.json"], env)
        attempt.update(returned=True, returncode=rc)
        text = out.decode("utf-8", "strict")
        require(err == b"" and not DIAGNOSTIC.search(text), "no fixture/sanitizer diagnostics")
        lines = text.splitlines()
        found = [line[5:] for line in lines if line.startswith("FAIL ")]
        results = [line for line in lines if line.startswith("RESULT ")]
        require(len(results) == 1, "one result for " + selector)
        match = re.fullmatch(r"RESULT " + re.escape(selector) + r" checks=(\d+) failures=(\d+)", results[0])
        require(match is not None, "exact result syntax")
        checks, count = map(int, match.groups())
        require(checks == SCENARIOS[selector], "source-reviewed check count")
        require(count == len(found) and sorted(found) == sorted(failures), "exact named failure set")
        require(len(lines) == 1 + len(found) and lines[-1] == results[0]
                and text == "\n".join(lines) + "\n", "exact fixture output ordering/newlines")
        require(rc == (1 if failures else 0), "exact semantic exit code")
        evidence = collect_fixture_evidence(self, relative, selector)
        attempt["accepted"] = True
        return {"selector": selector, "checks": checks, "expected_failures": list(failures),
                "observed_failures": found, "returncode": rc,
                "stdout_sha256": digest(out), "stderr_sha256": digest(err), "evidence": evidence}

    def variant(self, control):
        dst = self.directory / "variants" / control["name"]
        shutil.copytree(self.inputs, dst)
        target = dst / control["file"]
        target.write_text(replace_once(target.read_text(), control["old"], control["new"]))
        manifest = {p.relative_to(dst).as_posix(): {"sha256": sha(p), "bytes": p.stat().st_size}
                    for p in dst.rglob("*") if p.is_file()}
        save_json(dst / "source-manifest.json", manifest)
        return dst, manifest
class Object:
    """Minimal read-only ELF64 REL metadata reader; never executes an object."""
    def __init__(self, data, expected_type=1):
        self.data = data
        require(len(data) >= 64 and data[:7] == b"\x7fELF\x02\x01\x01", "ELF64 little-endian object")
        hdr = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
        require(hdr[1] == expected_type and hdr[2] == 183 and hdr[3] == 1, "expected AArch64 ELF type")
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


def decode_frame_rows(text):
    """Read retained readelf text, not an imported test helper or target execution."""
    entries = []
    current = None
    columns = None
    for line in text.splitlines():
        m = re.fullmatch(r"\s*[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+FDE\s+cie=[0-9a-f]+\s+pc=([0-9a-f]+)\.\.([0-9a-f]+)\s*", line)
        if m:
            current = {"start": int(m[1], 16), "end": int(m[2], 16), "rows": []}
            entries.append(current)
            columns = None
            continue
        if re.search(r"\bCIE\b", line):
            current, columns = None, None
            continue
        if current is None:
            continue
        fields = line.split()
        if fields and fields[0] == "LOC":
            require(len(fields) >= 2 and fields[1] == "CFA" and len(fields) == len(set(fields)),
                    "unique CFI row columns")
            columns = fields
        elif columns and fields and re.fullmatch(r"[0-9a-f]+", fields[0]):
            require(len(fields) == len(columns), "complete interpreted CFI row")
            row = dict(zip(columns, fields))
            row["LOC"] = int(row["LOC"], 16)
            require(current["start"] <= row["LOC"] <= current["end"], "CFI row PC in FDE")
            require(not current["rows"] or row["LOC"] > current["rows"][-1]["LOC"], "ordered CFI rows")
            current["rows"].append(row)
    require(entries and len({(e["start"], e["end"]) for e in entries}) == len(entries),
            "nonempty distinct interpreted FDEs")
    return entries

def check_native(obj, frames):
    require(NATIVE_TARGETS, "native target metadata inventory finalized")
    fdes = decode_frame_rows(frames)
    records, failures = {}, []
    for name, frame_size in NATIVE_TARGETS.items():
        symbols = [s for s in obj.symbols if s["name"] == name]
        require(len(symbols) == 1, "one authored target " + name)
        symbol = symbols[0]
        require(symbol["type"] == 2 and symbol["size"] >= 16 and 0 < symbol["section"] < len(obj.sections),
                "complete authored target extent")
        section = obj.sections[symbol["section"]]
        require(section["name"] == ".text" and section["flags"] & 4, "target belongs to ordinary .text")
        begin, end = symbol["value"], symbol["value"] + symbol["size"]
        require(end <= len(section["data"]), "authored symbol byte bounds")
        first = struct.unpack_from("<I", section["data"], begin)[0]
        expected = 0xa9800000 | (((-frame_size // 8) & 127) << 15) | (30 << 10) | (31 << 5) | 29
        require(first == expected, "authored first word is selected STP " + name)
        require(struct.unpack_from("<I", section["data"], begin + 4)[0] == 0x910003fd,
                "authored +4 instruction is MOV x29,sp")
        found = [f for f in fdes if (f["start"], f["end"]) == (begin, end)]
        require(len(found) == 1 and len(found[0]["rows"]) >= 2, "exact authored FDE extent " + name)
        rows = found[0]["rows"]
        require(rows[0]["LOC"] == begin and rows[1]["LOC"] == begin + 4,
                "separate entry/post-STP CFI rows " + name)
        require("x29" in rows[1] and "ra" in rows[1], "x29 and return-column rules")
        if rows[0]["CFA"] != "sp+0":
            failures.append(name + ":entry-cfa")
        if rows[1]["CFA"] != "sp+" + str(frame_size):
            failures.append(name + ":post-stp-cfa")
        if rows[1]["x29"] != "c" + str(-frame_size):
            failures.append(name + ":saved-x29")
        if rows[1]["ra"] != "c" + str(-frame_size + 8):
            failures.append(name + ":saved-x30")
        records[name] = {"section_offset": begin, "bytes": symbol["size"],
                         "first_word": "0x%08x" % first, "frame_bytes": frame_size,
                         "entry_row": rows[0], "post_stp_row": rows[1]}
    return {"scope": "Two authored native-shaped prologues and their entry/+4 CFI only; no parent wrapper CFI replay.",
            "targets": records, "failures": failures}

def metadata_control(run, control, candidate_metadata, candidate_object):
    dst, manifest = run.variant(control)
    relative = "build/" + control["name"]
    output = run.directory / relative
    output.mkdir(parents=True)
    out = "/out/" + relative + "/"
    run.zero(control["name"] + "-compile",
             ["gcc", "-g", "-fno-pie", "-c", "/out/variants/" + control["name"] +
              "/fixture.S", "-o", out + "fixture.o"])
    run.artifact(relative + "/fixture.o")
    stdout, _ = run.zero(control["name"] + "-frames",
                         ["readelf", "-W", "--debug-dump=frames-interp", out + "fixture.o"])
    (output / "fixture-frames.txt").write_bytes(stdout)
    run.artifact(relative + "/fixture-frames.txt")
    compiled = Object((output / "fixture.o").read_bytes())
    candidate = Object(candidate_object.read_bytes())
    require(compiled.section(".text")["data"] == candidate.section(".text")["data"]
            and compiled.relocations(".text") == candidate.relocations(".text"),
            "CFI-only control retains all target instructions and code relocations")
    metadata = check_native(compiled, stdout.decode("utf-8", "strict"))
    require(metadata["failures"] == control["failures"], "exact designated metadata rejection")
    other = [name for name in NATIVE_TARGETS if name != control["target"]]
    require(len(other) == 1 and metadata["targets"][other[0]] == candidate_metadata["targets"][other[0]],
            "other authored target metadata unchanged")
    save_json(output / "metadata-rejection.json", metadata)
    run.artifact(relative + "/metadata-rejection.json")
    return {**control, "source_manifest": manifest, "metadata": metadata,
            "machine_code_unchanged": True, "linked": False, "executed": False}

def metadata_baseline(run):
    """Compile only the authored assembly when no runtime section supplied it."""
    relative = "build/metadata-baseline"
    target = run.directory / relative
    target.mkdir(parents=True)
    output = "/out/" + relative + "/"
    run.zero("metadata-baseline-compile",
             ["gcc", "-g", "-fno-pie", "-c", "/src/fixture.S", "-o", output + "fixture.o"])
    run.artifact(relative + "/fixture.o")
    text, _ = run.zero("metadata-baseline-frames",
                        ["readelf", "-W", "--debug-dump=frames-interp", output + "fixture.o"])
    (target / "fixture-frames.txt").write_bytes(text)
    run.artifact(relative + "/fixture-frames.txt")
    metadata = check_native(Object((target / "fixture.o").read_bytes()), text.decode("utf-8", "strict"))
    require(metadata["failures"] == [], "metadata-only baseline entry/+4 CFI agrees")
    save_json(target / "native-metadata.json", metadata)
    run.artifact(relative + "/native-metadata.json")
    return metadata, target / "fixture.o"

def strict_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key")
        result[key] = value
    return result

def collect_fixture_evidence(run, relative, selector):
    directory = run.directory / relative
    require(sorted(p.name for p in directory.iterdir()) == ["report.json"], "exact bounded fixture output inventory")
    path = directory / "report.json"
    require(path.is_file() and not path.is_symlink() and 0 < path.stat().st_size <= 32768,
            "regular bounded report")
    data = json.loads(path.read_bytes(), object_pairs_hook=strict_object)
    top_keys = {"schema", "selector", "synthetic_only", "checks", "failures", "index", "throw_requested",
                "caught_exact", "result", "prepared", "claims", "contract_errors", "diagnostics", "routes",
                "observation", "body_calls", "caller_calls", "body_cleanup", "caller_cleanup", "body_kind",
                "body_arguments", "snapshot_ok", "capture_ready", "capture_count", "closed_retained", "record"}
    require(type(data) is dict and set(data) == top_keys, "exact report schema")
    _, variant, mode, leaf = relative.split("/")
    require(leaf == selector and variant in ("candidate", SCRATCH_CONTROL["name"]), "known report identity")
    index = 0 if selector.startswith("action-") else 1
    throws = selector.endswith("-throw")
    controlled = variant == SCRATCH_CONTROL["name"]
    numeric = top_keys - {"selector", "synthetic_only", "throw_requested", "caught_exact", "routes",
                         "observation", "body_arguments", "snapshot_ok", "capture_ready", "closed_retained", "record"}
    require(all(type(data[k]) is int and 0 <= data[k] < 1 << 64 for k in numeric), "report integer domains")
    for key in ("synthetic_only", "throw_requested", "caught_exact", "body_arguments", "snapshot_ok",
                "capture_ready", "closed_retained"):
        require(type(data[key]) is bool, "strict report boolean " + key)
    require(data["schema"] == 1 and data["selector"] == selector and data["synthetic_only"] is True
            and data["checks"] == SCENARIOS[selector] and data["failures"] == (2 if controlled else 0)
            and data["index"] == index and data["throw_requested"] == throws,
            "report identity/check count")
    require(data["prepared"] == 1 and data["claims"] == 2 and data["contract_errors"] == 0
            and data["diagnostics"] == 0, "actual late install summary")
    binary = Object((run.directory / "build" / variant / mode / "test-transfer").read_bytes(), expected_type=2)
    assembly = Object((run.directory / "build" / variant / mode / "fixture.o").read_bytes())
    def symbol(obj, name):
        matches = [x for x in obj.symbols if x["name"] == name and x["section"] != 0]
        require(len(matches) == 1, "one defined symbol " + name)
        return matches[0]
    def uints(record, keys):
        require(type(record) is dict and set(record) == set(keys), "exact field set")
        require(all(type(x) is int and 0 <= x < 1 << 64 for x in record.values()), "unsigned scalar fields")
    route_scalars = {"requested", "target", "hook", "output_slot", "original", "island", "contract_ok",
                     "internal_result", "path", "late", "target_prot_before", "target_prot_after",
                     "island_prot", "trampoline_prot", "bytes_available"}
    route_blobs = {"before": 16, "after": 16, "island_bytes": 16, "trampoline_bytes": 20}
    require(type(data["routes"]) is list and len(data["routes"]) == 2, "two actual route records")
    for i, route in enumerate(data["routes"]):
        require(type(route) is dict and set(route) == route_scalars | set(route_blobs), "exact route fields")
        require(all(type(route[k]) is int and 0 <= route[k] < 1 << 64 for k in route_scalars), "route integer domains")
        blobs = {}
        for key, size in route_blobs.items():
            require(type(route[key]) is str and re.fullmatch("[0-9a-f]{" + str(size * 2) + "}", route[key]),
                    "exact canonical owned bytes " + key)
            blobs[key] = bytes.fromhex(route[key])
        prefix = "action" if i == 0 else "service"
        target_name = "fixture_" + prefix + "_target"
        target = symbol(binary, target_name)
        require(route["requested"] == (0x1be7e38 if i == 0 else 0x20a1550)
                and route["target"] == target["value"]
                and route["hook"] == symbol(binary, "az_usb_" + prefix + "_entry")["value"]
                and route["output_slot"] == symbol(binary, "az_usb_" + prefix + "_original")["value"],
                "actual linked target/replacement/permanent slot identity")
        require(route["original"] > 0 and route["original"] % 4096 == 0
                and route["island"] > 0 and route["island"] % 16 == 0
                and all(route[k] == 1 for k in ("contract_ok", "internal_result", "path", "late", "bytes_available")),
                "actual successful late island result")
        require(all(route[k] == 5 for k in ("target_prot_before", "target_prot_after", "island_prot", "trampoline_prot")),
                "all recorded actual regions are restored/read-execute only")
        authored = symbol(assembly, target_name)
        section = assembly.sections[authored["section"]]["data"]
        require(blobs["before"] == section[authored["value"]:authored["value"] + 16],
                "original target first sixteen authored bytes")
        branch = struct.unpack_from("<I", blobs["after"])[0]
        displacement = branch & 0x3ffffff
        if displacement & (1 << 25):
            displacement -= 1 << 26
        require(branch & 0xfc000000 == 0x14000000 and route["target"] + displacement * 4 == route["island"]
                and blobs["after"][4:] == blobs["before"][4:], "one aligned branch plus unchanged following words")
        ldr, br = (0x58000050, 0xd61f0200) if controlled else (0x58000051, 0xd61f0220)
        require(struct.unpack("<IIQ", blobs["island_bytes"]) == (ldr, br, route["hook"]),
                "exact generated safe island instructions and literal")
        stp, actual_ldr, actual_br, continuation = struct.unpack("<IIIQ", blobs["trampoline_bytes"])
        require(stp == struct.unpack_from("<I", blobs["before"])[0] and (actual_ldr, actual_br) == (ldr, br)
                and continuation == route["target"] + 4, "exact one-word trampoline and +4 continuation")
    require(data["routes"][0]["original"] != data["routes"][1]["original"]
            and data["routes"][0]["island"] != data["routes"][1]["island"], "distinct actual generated storage")
    observed = data["observation"]
    observation_keys = {"caller_sp", "caller_lr", "caller_x16", "caller_x17", "body_x16", "body_x17",
                        "body_entry_sp", "body_lr", "body_x0", "body_x1", "body_x2", "body_frame_sp", "body_frame_fp"}
    uints(observed, observation_keys)
    selected = data["routes"][index]
    caller_name = "fixture_call_" + ("action" if index == 0 else "service")
    caller = symbol(binary, caller_name)
    require(observed["caller_sp"] != 0 and observed["caller_sp"] % 16 == 0
            and observed["caller_lr"] == caller["value"] + 40
            and observed["caller_x16"] == 0x1616 and observed["caller_x17"] == 0x1717
            and observed["body_entry_sp"] == observed["caller_sp"]
            and observed["body_lr"] == observed["caller_lr"]
            and observed["body_frame_sp"] + (144 if index == 0 else 272) == observed["caller_sp"]
            and observed["body_frame_fp"] == observed["body_frame_sp"], "actual body/caller frame and LR")
    expected_scratch = (selected["target"] + 4, 0x1717) if controlled else (selected["original"], selected["target"] + 4)
    require((observed["body_x16"], observed["body_x17"]) == expected_scratch, "selected actual scratch behavior")
    require(observed["body_x0"] > 0 and observed["body_x1"] == 0xa1b2c3d4e5f60718
            and observed["body_x2"] == 0x1234567890abcdef and data["body_arguments"], "actual forwarded argument values")
    require(all(data[k] == 1 for k in ("body_calls", "caller_calls", "body_cleanup", "caller_cleanup"))
            and data["body_kind"] == index + 1 and data["closed_retained"], "single body/caller cleanup with retained forwarding")
    require(data["caught_exact"] == throws and data["result"] ==
            (0 if throws else (0xfedcba9801234567 if index == 0 else 0x87654321fedcba98)),
            "actual selected ordinary return or typed body exception")
    require(data["snapshot_ok"] and data["capture_ready"] and data["capture_count"] == 1, "one actual entry observation")
    record = data["record"]
    scalar_keys = {"id", "boundary", "reserved_12", "original_sp", "original_lr", "x0", "x1", "x2"}
    require(type(record) is dict and set(record) == scalar_keys | {"source", "reserved_68"}, "exact entry record")
    require(all(type(record[k]) is int and 0 <= record[k] < 1 << 64 for k in scalar_keys), "entry scalar domains")
    require(record["id"] == 1 and record["boundary"] == index + 1 and record["reserved_12"] == 0
            and record["reserved_68"] == "00000000" and record["original_sp"] == observed["caller_sp"]
            and record["original_lr"] == observed["caller_lr"]
            and all(record[k] == observed["body_" + k] for k in ("x0", "x1", "x2"))
            and record["source"] == ("00" * 12 if index == 0 else "1807f6e5d4c3b2a1efcdab90"),
            "complete actual register-only entry record")
    return run.artifact(relative + "/report.json")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True,
                        help="local repository containing the pinned SDK commit")
    parser.add_argument("--modes", nargs="+", choices=("plain", "asan-ubsan"),
                        default=["plain", "asan-ubsan"],
                        help="run only selected new runtime modes after a diagnosed failure")
    parser.add_argument("--sections", nargs="+",
                        choices=("candidate", "scratch-control", "metadata-controls"),
                        default=["candidate", "scratch-control", "metadata-controls"],
                        help="selected new branches; default is the complete matrix")
    args = parser.parse_args()
    require(len(args.modes) == len(set(args.modes)) and len(args.sections) == len(set(args.sections)),
            "selection has no duplicates")
    run = Run()
    report = {"schema": 1, "status": "running", "authored_target_fixture_only": True,
              "firmware_execution": False, "emulator_or_device_access": False,
              "parent_suites_replayed": False, "tsan_executed": False,
              "production_configuration_executed": False, "fixture_macros": [],
              "selection": {"modes": args.modes, "sections": args.sections},
              "full_matrix_requested": set(args.modes) == {"plain", "asan-ubsan"} and
                  set(args.sections) == {"candidate", "scratch-control", "metadata-controls"},
              "candidate": [], "controls": [], "metadata_controls": []}
    try:
        run.prepare()
        run.capture(args.upstream.absolute())
        require(SCENARIOS and set(SCENARIOS) == {"action-return", "service-return", "action-throw", "service-throw"},
                "source-reviewed four-selector inventory must be finalized before Docker")
        require(set(SCRATCH_FAILURES) == set(SCENARIOS) and all(SCRATCH_FAILURES.values()),
                "source-reviewed scratch oracles must be finalized")
        require(len(NATIVE_TARGETS) == 2 and len(CFI_CONTROLS) == 2, "two target metadata controls")
        require(all(type(n) is int and n > 0 for n in SCENARIOS.values()), "positive exact check counts")
        test = (run.inputs / "test_transfer.cpp").read_text()
        for failures in SCRATCH_FAILURES.values():
            for oracle in failures:
                require(test.count('"' + oracle + '"') == 1, "unique expected oracle " + oracle)
        for control in [SCRATCH_CONTROL] + CFI_CONTROLS:
            replace_once((run.inputs / control["file"]).read_text(), control["old"], control["new"])
        report["scenario_inventory"] = SCENARIOS
        run.phase = "image"
        rc, out, err = run.command("image", ["docker", "image", "inspect", IMAGE,
                                            "--format", "{{.Id}} {{.Architecture}}"])
        require(rc == 0 and err == b"" and out.decode().strip() == IMAGE_ID + " arm64",
                "exact locally available ARM64 image")
        out, _ = run.zero("platform", ["uname", "-m"])
        require(out == b"aarch64\n", "actual ARM64 container")
        for compiler in ("gcc", "g++"):
            out, _ = run.zero(compiler + "-version", [compiler, "--version"])
            report[compiler + "_version"] = out.decode("utf-8", "strict")
        runtime_selected = bool(set(args.sections) & {"candidate", "scratch-control"})
        if runtime_selected and "asan-ubsan" in args.modes:
            run.phase = "sanitizer-availability"
            report["sanitizer"] = run.sanitizer_available()
        else:
            report["sanitizer"] = {"requested": False, "instrumented_tests_executed": False}
        modes = args.modes if runtime_selected else []
        report["modes_requested_for_runtime"] = modes
        baseline_metadata, baseline_object = None, None
        if "candidate" in args.sections:
            for mode in modes:
                run.phase = "candidate:" + mode
                binary, metadata = run.build_fixture("candidate", mode)
                report.setdefault("native_metadata", {})[mode] = metadata
                baseline_metadata = metadata
                baseline_object = run.directory / "build/candidate" / mode / "fixture.o"
                for selector in SCENARIOS:
                    run.phase = "candidate:" + mode + ":" + selector
                    outcome = run.execute("candidate", mode, binary, selector)
                    report["candidate"].append({"mode": mode, **outcome})
        if "scratch-control" in args.sections:
            dst, manifest = run.variant(SCRATCH_CONTROL)
            detail = {**SCRATCH_CONTROL, "source_manifest": manifest, "outcomes": []}
            report["controls"].append(detail)
            for mode in modes:
                run.phase = "control:" + SCRATCH_CONTROL["name"] + ":" + mode
                binary, metadata = run.build_fixture(SCRATCH_CONTROL["name"], mode,
                                                      "/out/variants/" + SCRATCH_CONTROL["name"])
                if baseline_metadata is None:
                    baseline_metadata = metadata
                    baseline_object = run.directory / "build" / SCRATCH_CONTROL["name"] / mode / "fixture.o"
                for selector in SCENARIOS:
                    outcome = run.execute(SCRATCH_CONTROL["name"], mode, binary, selector,
                                          SCRATCH_FAILURES[selector])
                    detail["outcomes"].append({"mode": mode, **outcome})
        if "metadata-controls" in args.sections:
            if baseline_metadata is None:
                run.phase = "metadata-baseline"
                baseline_metadata, baseline_object = metadata_baseline(run)
                report["metadata_only_baseline"] = baseline_metadata
            for control in CFI_CONTROLS:
                run.phase = "metadata-control:" + control["name"]
                report["metadata_controls"].append(metadata_control(
                    run, control, baseline_metadata, baseline_object))
        require({n: sha(HERE / n) for n in SOURCES} == run.own_manifest, "live new inputs unchanged")
        run.check_snapshot()
        report.update(status="passed",
                      candidate_checks_executed=sum(x["checks"] for x in report["candidate"]),
                      candidate_executions=len(report["candidate"]),
                      control_executions=sum(len(c["outcomes"]) for c in report["controls"]),
                      metadata_rejections=len(report["metadata_controls"]))
    except BaseException as exc:
        report.update(status="failed", failure_type=type(exc).__name__, failure=str(exc))
        if run.directory.exists():
            try:
                (run.directory / "failure.txt").write_text(traceback.format_exc())
            except BaseException as capture_error:
                report["failure_capture_error"] = type(capture_error).__name__ + ": " + str(capture_error)
    finally:
        report.update(production_configuration_execution_requested=bool(run.execution_attempts),
                      production_configuration_executed=any(x["accepted"] for x in run.execution_attempts),
                      fixture_execution_attempts=run.execution_attempts)
        report.update(phase=run.phase, image=IMAGE, image_id=IMAGE_ID, sdk_commit=SDK_COMMIT,
                      parent_commit=PARENT_COMMIT, inputs=run.manifest,
                      commands=run.commands, artifacts=run.artifacts)
        result_sha = None
        if run.directory.exists():
            save_json(run.directory / "result.json", report)
            result_sha = sha(run.directory / "result.json")
        print(json.dumps({"status": report["status"], "private_run": run.directory.name,
                          "result_sha256": result_sha}), flush=True)
    return 0 if report["status"] == "passed" else 1

if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run only the new synthetic USB entry fixture in a pinned, networkless image."""
import sys
if not __debug__ or sys.flags.optimize:
    raise RuntimeError("optimized Python is refused before any output is created")
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import traceback
import uuid

HERE = Path(__file__).resolve().parent
IMAGE = "azmod-build:18.04"
IMAGE_ID = "sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90"
SOURCES = ("entry.S", "observer.h", "observer.cpp", "fixture.S", "test_entry.cpp", "run-tests.py")
FLAGS = ["-std=c++17", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-fexceptions",
         "-fasynchronous-unwind-tables", "-pthread", "-fPIC"]
# Source-reviewed inventory; filled before the first build, never learned from a passing run.
SCENARIOS = {
    "register-state": 6,
    "value-records": 4,
    "return-channels": 7,
    "native-exception": 3,
    "helper-unwind": 3,
    "disabled": 2,
    "capacity": 3,
    "contention": 3
}
CONTROLS = [
    {
        "name": "wrong-observed-lr",
        "file": "observer.cpp",
        "old": "next.original_lr = frame->x[30];",
        "new": "next.original_lr = frame->x[29];",
        "selector": "register-state",
        "failures": [
            "record-caller-identity"
        ]
    },
    {
        "name": "wrong-observed-sp",
        "file": "observer.cpp",
        "old": "next.original_sp = reinterpret_cast<std::uintptr_t>(frame) + sizeof(*frame);",
        "new": "next.original_sp = reinterpret_cast<std::uintptr_t>(frame) + sizeof(*frame) - 16;",
        "selector": "register-state",
        "failures": [
            "record-caller-identity"
        ]
    },
    {
        "name": "omit-x1-restore",
        "file": "entry.S",
        "old": "    ldp x0, x1, [sp, #0]\n",
        "new": "    ldr x0, [sp, #0]\n",
        "selector": "register-state",
        "failures": [
            "native-register-forwarding"
        ]
    },
    {
        "name": "omit-q31-restore",
        "file": "entry.S",
        "old": "    ldp q30, q31, [sp, #736]\n",
        "new": "    ldr q30, [sp, #736]\n",
        "selector": "register-state",
        "failures": [
            "native-register-forwarding"
        ]
    },
    {
        "name": "helper-clobbers-errno",
        "file": "observer.cpp",
        "old": "    ~ErrnoRestore() noexcept { errno = saved; }",
        "new": "    ~ErrnoRestore() noexcept { errno = 93; }",
        "selector": "register-state",
        "failures": [
            "native-errno"
        ]
    }
]
META_CONTROLS = [
    {
        "name": "wrong-cfa-metadata",
        "old": ".cfi_def_cfa_offset 800\n",
        "new": ".cfi_def_cfa_offset 784\n",
        "expected_error": "CFI state az_usb_action_entry +0x4"
    },
    {
        "name": "wrong-lr-metadata",
        "old": ".cfi_offset 30, -560\n",
        "new": ".cfi_offset 30, -568\n",
        "expected_error": "CFI state az_usb_action_entry +0x44"
    }
]
DIAGNOSTIC = re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|"
                        r"runtime error:|DEADLYSIGNAL|Segmentation fault|"
                        r"terminate called|Aborted|FIXTURE_ABORT|warning:|error:", re.I)

class AuditError(RuntimeError):
    pass

def require(ok, message):
    if not ok:
        raise AuditError(message)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def sha(path):
    return digest(path.read_bytes())

def save_json(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")

def replace_once(source, before, after):
    require(source.count(before) == 1, "control anchor must occur exactly once: " + repr(before))
    return source.replace(before, after)

class Run:
    def __init__(self):
        self.directory = HERE / "private" / ("run-" + uuid.uuid4().hex)
        self.directory.mkdir(parents=True)
        self.inputs = self.directory / "inputs"
        self.inputs.mkdir()
        self.logs = self.directory / "commands"
        self.logs.mkdir()
        self.number = 0
        self.commands = []
        self.artifacts = []
        self.manifest = {}
        self.phase = "preparation"
        save_json(self.directory / "started.json", {"schema": 1, "status": "preparing",
                  "python": sys.version, "image": IMAGE, "image_id": IMAGE_ID,
                  "synthetic_only": True, "runtime_or_firmware_access": False})

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

    def docker(self, name, command):
        container = "az-usb-entry-" + self.directory.name[4:] + "-" + str(self.number + 1)
        argv = ["docker", "run", "--name", container, "--rm", "--pull=never",
                "--network", "none", "--platform", "linux/arm64",
                "--mount", "type=bind,source=" + str(self.inputs) + ",target=/src,readonly",
                "--mount", "type=bind,source=" + str(self.directory) + ",target=/out",
                IMAGE_ID] + command
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

    def snapshot(self):
        for name in SOURCES:
            p = HERE / name
            require(p.is_file() and not p.is_symlink(), "source must be regular: " + name)
            data = p.read_bytes()
            (self.inputs / name).write_bytes(data)
            self.manifest[name] = digest(data)
        save_json(self.directory / "source-manifest.json", self.manifest)
        require({n: sha(self.inputs / n) for n in SOURCES} == self.manifest,
                "snapshot changed while copying")

    def build(self, variant, source_dir="/src"):
        dest = self.directory / "build" / variant
        dest.mkdir(parents=True)
        out = "/out/build/" + variant + "/"
        self.zero(variant + "-entry", ["g++", "-g", "-fPIC", "-c",
                                      source_dir + "/entry.S", "-o", out + "entry.o"])
        self.zero(variant + "-observer", ["g++"] + FLAGS +
                  ["-Daz_usb_entry_observe=az_usb_real_observe", "-DAZ_USB_ENTRY_FIXTURE",
                   "-c", source_dir + "/observer.cpp", "-o", out + "observer-fixture.o"])
        self.zero(variant + "-fixture", ["g++", "-g", "-fPIC", "-c",
                                        source_dir + "/fixture.S", "-o", out + "fixture.o"])
        self.zero(variant + "-test", ["g++"] + FLAGS +
                  ["-DAZ_USB_ENTRY_FIXTURE", "-c", source_dir + "/test_entry.cpp",
                   "-o", out + "test.o"])
        self.zero(variant + "-link", ["g++"] + FLAGS +
                  [out + n for n in ("entry.o", "observer-fixture.o", "fixture.o", "test.o")] +
                  ["-o", out + "test-entry"])
        for name in ("entry.o", "observer-fixture.o", "fixture.o", "test.o", "test-entry"):
            self.artifact("build/" + variant + "/" + name)
        return out + "test-entry"

    def execute(self, variant, binary, selector, expected_failures=()):
        require(selector in SCENARIOS, "unknown selector " + selector)
        rc, stdout, stderr = self.docker(variant + "-" + selector, [binary, selector])
        text = stdout.decode("utf-8", "strict")
        require(stderr == b"", variant + " unexpected stderr")
        require(not DIAGNOSTIC.search(text), variant + " fixture diagnostic")
        lines = text.splitlines()
        failures = [line[5:] for line in lines if line.startswith("FAIL ")]
        summaries = [line for line in lines if line.startswith("RESULT ")]
        require(len(summaries) == 1, variant + " missing/duplicate result")
        m = re.fullmatch(r"RESULT " + re.escape(selector) + r" checks=(\d+) failures=(\d+)", summaries[0])
        require(m is not None, variant + " result syntax")
        checks, failed = map(int, m.groups())
        require(checks == SCENARIOS[selector], variant + " source-reviewed assertion count")
        require(failed == len(failures), variant + " failure count")
        require(sorted(failures) == sorted(expected_failures), variant + " exact named failures")
        require(rc == (1 if expected_failures else 0), variant + " fixture exit")
        require(len(lines) == 1 + len(failures), variant + " unexpected output")
        return {"selector": selector, "checks": checks, "failures": failures, "returncode": rc,
                "stdout_sha256": digest(stdout), "stderr_sha256": digest(stderr)}

    def metadata(self, variant):
        obj = "/out/build/" + variant + "/entry.o"
        raw = {}
        for name, command in [("disassembly", ["objdump", "-dr", obj]),
                              ("frames", ["readelf", "--debug-dump=frames", obj]),
                              ("symbols", ["nm", "-S", "--defined-only", obj])]:
            stdout, stderr = self.zero(variant + "-" + name, command)
            require(stderr == b"", name + " unexpected stderr")
            p = self.directory / "build" / variant / (name + ".txt")
            p.write_bytes(stdout)
            raw[name] = stdout.decode("utf-8", "strict")
            self.artifact("build/" + variant + "/" + name + ".txt")
        return check_entry_metadata(raw)

def decode_cfi(raw):
    # Interpret only the emitted DWARF operations used by this reviewed object.
    blocks = []
    current = None
    for line in raw.splitlines():
        m = re.match(r"^([0-9a-f]+)\s+[0-9a-f]+\s+[0-9a-f]+\s+(CIE|FDE)(.*)$", line)
        if m:
            current = {"offset": int(m.group(1), 16), "kind": m.group(2),
                       "tail": m.group(3), "lines": []}
            blocks.append(current)
        elif current is not None:
            current["lines"].append(line.strip())
    cies = [b for b in blocks if b["kind"] == "CIE"]
    require(len(cies) == 1, "one reviewed CIE")
    c = "\n".join(cies[0]["lines"])
    for expression in (r"Version:\s+1\b", r'Augmentation:\s+"zR"',
                       r"Code alignment factor:\s+4\b",
                       r"Data alignment factor:\s+-8\b",
                       r"Return address column:\s+30\b"):
        require(re.search(expression, c) is not None, "CIE contract " + expression)

    def apply(state, line):
        base, offset, rules = state
        m = re.fullmatch(r"DW_CFA_def_cfa:\s+r(\d+)(?: \([^)]+\))? ofs (-?\d+)", line)
        if m:
            return int(m.group(1)), int(m.group(2)), rules
        m = re.fullmatch(r"DW_CFA_def_cfa_offset:\s+(-?\d+)", line)
        if m:
            return base, int(m.group(1)), rules
        m = re.fullmatch(r"DW_CFA_offset(?:_extended)?:\s+r(\d+)(?: \([^)]+\))? at cfa([+-]\d+)", line)
        if m:
            rules[int(m.group(1))] = int(m.group(2))
            return base, offset, rules
        m = re.fullmatch(r"DW_CFA_restore(?:_extended)?:\s+r(\d+)(?: \([^)]+\))?", line)
        if m:
            rules.pop(int(m.group(1)), None)
            return base, offset, rules
        m = re.fullmatch(r"DW_CFA_undefined:\s+r(\d+)(?: \([^)]+\))?", line)
        if m:
            rules[int(m.group(1))] = "undefined"
            return base, offset, rules
        require(line == "DW_CFA_nop", "unsupported CFI operation: " + line)
        return state

    initial = (None, None, {})
    for line in cies[0]["lines"]:
        if line.startswith("DW_CFA_"):
            initial = apply(initial, line)
    require(initial == (31, 0, {}), "CIE initial state")
    fdes = []
    for block in blocks:
        if block["kind"] != "FDE":
            continue
        m = re.search(r"cie=([0-9a-f]+) pc=([0-9a-f]+)\.\.([0-9a-f]+)", block["tail"])
        require(m is not None and int(m.group(1), 16) == cies[0]["offset"], "FDE CIE reference")
        start, end = int(m.group(2), 16), int(m.group(3), 16)
        pc, state = start, (31, 0, {})
        rows = {pc: (31, 0, {})}
        for line in block["lines"]:
            if not line.startswith("DW_CFA_"):
                continue
            m = re.fullmatch(r"DW_CFA_advance_loc(?:1|2|4)?:\s+(\d+) to ([0-9a-f]+)", line)
            if m:
                dest = int(m.group(2), 16)
                require(dest == pc + int(m.group(1)) and pc <= dest <= end, "CFI PC advance")
                pc = dest
            else:
                state = apply((state[0], state[1], dict(state[2])), line)
            rows[pc] = (state[0], state[1], dict(state[2]))
        fdes.append((start, end, rows))
    return fdes

def normalize_operands(text):
    text = text.split("//", 1)[0].strip().lower()
    text = re.sub(r"#(0x[0-9a-f]+|\d+)", lambda m: "#" + str(int(m.group(1), 0)), text)
    text = re.sub(r",\s*#0\]", "]", text)
    return re.sub(r"\s+", "", text)

def expected_instructions(boundary):
    expected = [("sub", "sp, sp, #800")]
    expected += [("stp", "x%d, x%d, [sp, #%d]" % (r, r + 1, r * 8))
                 for r in range(0, 30, 2)]
    expected += [("str", "x30, [sp, #240]"), ("str", "xzr, [sp, #248]")]
    expected += [("stp", "q%d, q%d, [sp, #%d]" % (r, r + 1, 256 + r * 16))
                 for r in range(0, 32, 2)]
    expected += [("mrs", "x9, nzcv"), ("mrs", "x10, fpcr"), ("mrs", "x11, fpsr"),
                 ("str", "x9, [sp, #768]"), ("str", "x10, [sp, #776]"),
                 ("str", "x11, [sp, #784]"), ("str", "xzr, [sp, #792]"),
                 ("mov", "x0, sp"), ("mov", "w1, #%d" % boundary), ("bl", None),
                 ("ldr", "x9, [sp, #768]"), ("ldr", "x10, [sp, #776]"),
                 ("ldr", "x11, [sp, #784]"), ("msr", "nzcv, x9"),
                 ("msr", "fpcr, x10"), ("msr", "fpsr, x11")]
    expected += [("ldp", "q%d, q%d, [sp, #%d]" % (r, r + 1, 256 + r * 16))
                 for r in range(0, 32, 2)]
    expected += [("ldp", "x%d, x%d, [sp, #%d]" % (r, r + 1, r * 8))
                 for r in range(0, 30, 2)]
    expected += [("ldr", "x30, [sp, #240]"), ("add", "sp, sp, #800"),
                 ("adrp", None), ("ldr", "x16, [x16]"), ("ldar", "x16, [x16]"),
                 ("br", "x16")]
    return expected

def check_entry_metadata(raw):
    instructions = {}
    relocations = {}
    for line in raw["disassembly"].splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+(\S+)\s*(.*)$", line)
        if m:
            at = int(m.group(1), 16)
            require(at not in instructions, "duplicate instruction")
            instructions[at] = (m.group(2), m.group(3).strip())
        m = re.match(r"^\s*([0-9a-f]+):\s+(R_AARCH64_\S+)\s+(\S+)\s*$", line)
        if m:
            at = int(m.group(1), 16)
            require(at not in relocations, "duplicate relocation")
            relocations[at] = (m.group(2), m.group(3))
    symbols = {}
    for line in raw["symbols"].splitlines():
        m = re.fullmatch(r"([0-9a-f]+)\s+([0-9a-f]+)\s+[Tt]\s+(\S+)", line)
        if m:
            symbols[m.group(3)] = (int(m.group(1), 16), int(m.group(2), 16))
    fdes = decode_cfi(raw["frames"])
    require(len(fdes) == 2, "two entry FDEs")
    expected_names = ("az_usb_action_entry", "az_usb_service_entry")
    require(set(symbols) == set(expected_names), "only two entry function bodies")
    covered = set()
    output = {}
    all_relocations = {}
    for boundary, name in enumerate(expected_names, 1):
        start, size = symbols[name]
        expected = expected_instructions(boundary)
        require(size == len(expected) * 4, "complete entry extent " + name)
        matches = [(a, b, r) for a, b, r in fdes if a == start and b == start + size]
        require(len(matches) == 1, "matching complete FDE " + name)
        rows = matches[0][2]
        # Independently derive the expected CFA/rules after each actual machine
        # save/restore. Compare state at every PC, not just selected text tokens.
        offset, rules = 0, {}
        wanted = {start: (31, offset, {})}
        entry_calls = []
        for index, (mnemonic, operands) in enumerate(expected):
            pc = start + index * 4
            require(pc in instructions, "missing instruction " + name)
            actual_mnemonic, actual_operands = instructions[pc]
            require(actual_mnemonic == mnemonic, "instruction shape " + name + " +" + hex(index * 4))
            if operands is not None:
                require(normalize_operands(actual_operands) == normalize_operands(operands),
                        "instruction operands " + name + " +" + hex(index * 4))
            elif mnemonic == "adrp":
                require(actual_operands.startswith("x16,"), "tail scratch register")
            covered.add(pc)
            if mnemonic == "bl":
                entry_calls.append(pc)
                all_relocations[pc] = ("R_AARCH64_CALL26", "az_usb_entry_observe")
            if mnemonic == "adrp":
                original = "az_usb_action_original" if boundary == 1 else "az_usb_service_original"
                all_relocations[pc] = ("R_AARCH64_ADR_GOT_PAGE", original)
                all_relocations[pc + 4] = ("R_AARCH64_LD64_GOT_LO12_NC", original)
                rules[16] = "undefined"
            if mnemonic == "sub":
                offset = 800
            if mnemonic == "add":
                offset = 0
            if operands is not None:
                m = re.fullmatch(r"([xq])(\d+)(?:, [xq](\d+))?, \[sp, #(\d+)\]", operands)
                if m and mnemonic in ("stp", "str", "ldp", "ldr"):
                    kind, first, second, saved = m.groups()
                    numbers = [int(first)] + ([] if second is None else [int(second)])
                    saved = int(saved)
                    width, base, dwarf = (8, 0, 0) if kind == "x" else (16, 256, 64)
                    for j, number in enumerate(numbers):
                        if saved + j * width != base + number * width:
                            continue  # FP/status scratch stores are not GPR save rules.
                        if kind == "x" and number > 30:
                            continue
                        register = dwarf + number
                        if mnemonic in ("stp", "str"):
                            rules[register] = base + number * width - 800
                        else:
                            rules.pop(register, None)
            wanted[pc + 4] = (31, offset, dict(rules))
        require(len(entry_calls) == 1, "one entry helper and no return callback")
        positions = sorted(rows)
        for pc, state in wanted.items():
            actual = rows[max(p for p in positions if p <= pc)]
            require(actual == state, "CFI state " + name + " +" + hex(pc - start))
        require(all(pc in wanted for pc in rows), "CFI PCs align to instructions")
        output[name] = {"address": start, "size": size, "instructions": len(expected),
                        "cfi_states_checked": len(wanted), "frame_bytes": 800,
                        "saved_lr_cfa_offset": -560, "helper_calls": 1, "tail_branches": 1,
                        "return_instructions": 0, "return_callbacks": 0,
                        "tail_x16_undefined": True}
    require(set(instructions) == covered, "no extra entry-object code")
    require(relocations == all_relocations, "exact helper/target relocations")
    return output


def check_production_object(run):
    obj = "/out/build/observer-production.o"
    nm, nmerr = run.zero("production-symbols", ["nm", "-S", "--defined-only", obj])
    sections, secerr = run.zero("production-sections", ["readelf", "-SW", obj])
    require(not nmerr and not secerr, "production metadata stderr")
    (run.directory / "build/observer-production-symbols.txt").write_bytes(nm)
    (run.directory / "build/observer-production-sections.txt").write_bytes(sections)
    for path in ("build/observer-production-symbols.txt", "build/observer-production-sections.txt"):
        run.artifact(path)
    text = nm.decode("utf-8", "strict")
    require("fixture_hold_sink" not in text and "fixture_release_sink" not in text
            and "az_usb_real_observe" not in text, "production object has no fixture API")
    require(re.search(r"^[0-9a-f]+\s+[0-9a-f]+\s+T\s+az_usb_entry_observe$", text, re.M)
            is not None, "production helper exists")
    slots = {}
    for name in ("az_usb_action_original", "az_usb_service_original"):
        matches = re.findall(r"^([0-9a-f]+)\s+([0-9a-f]+)\s+B\s+" + name + r"$", text, re.M)
        require(len(matches) == 1, "production original slot " + name)
        at, size = [int(v, 16) for v in matches[0]]
        require(size == 8 and at % 8 == 0, "original slot size/alignment")
        slots[name] = {"section_offset": at, "size": size}
    bss = [line for line in sections.decode("utf-8", "strict").splitlines()
           if re.search(r"\]\s+\.bss\s+NOBITS\s", line)]
    require(len(bss) == 1 and int(bss[0].split()[-1]) >= 8, "BSS base alignment")
    return {"slots": slots, "bss_alignment": int(bss[0].split()[-1]),
            "compiled_only": True, "fixture_defines": []}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    run = Run()
    report = {"schema": 1, "status": "running", "synthetic_only": True,
              "firmware_native_emulator_device_access": False, "sanitizers_used": False,
              "production_helper_executed_unchanged": False,
              "fixture_helper_compile_defines": ["az_usb_entry_observe=az_usb_real_observe",
                                                 "AZ_USB_ENTRY_FIXTURE"]}
    try:
        run.snapshot()
        require(len(SCENARIOS) == 8 and len(CONTROLS) == 5 and len(META_CONTROLS) == 2,
                "source-reviewed inventory must be finalized before any image/build operation")
        test_source = (run.inputs / "test_entry.cpp").read_text()
        for control in CONTROLS:
            require(control["selector"] in SCENARIOS and control["failures"],
                    "control selector/oracle inventory")
            replace_once((run.inputs / control["file"]).read_text(),
                         control["old"], control["new"])
            for oracle in control["failures"]:
                require(test_source.count('"' + oracle + '"') == 1,
                        "unique source oracle " + oracle)
        for control in META_CONTROLS:
            replace_once((run.inputs / "entry.S").read_text(),
                         control["old"], control["new"])
        report["scenario_inventory"] = SCENARIOS
        run.phase = "image"
        rc, stdout, stderr = run.command("image", ["docker", "image", "inspect", IMAGE,
                                                  "--format", "{{.Id}} {{.Architecture}}"])
        require(rc == 0 and stderr == b"" and stdout.decode().strip() == IMAGE_ID + " arm64",
                "pinned local ARM64 image identity")
        platform, platform_stderr = run.zero("platform", ["uname", "-m"])
        require(platform == b"aarch64\n" and platform_stderr == b"", "actual ARM64 container")
        version, _ = run.zero("compiler", ["g++", "--version"])
        report["compiler_version"] = version.decode()
        run.phase = "candidate"
        (run.directory / "build").mkdir()
        run.zero("production-helper", ["g++"] + FLAGS + ["-c", "/src/observer.cpp",
                  "-o", "/out/build/observer-production.o"])
        run.artifact("build/observer-production.o")
        report["production_object"] = check_production_object(run)
        binary = run.build("candidate")
        report["entry_metadata"] = run.metadata("candidate")
        report["candidate"] = []
        for selector in SCENARIOS:
            report["candidate"].append(run.execute("candidate", binary, selector))
        report["controls"] = []
        for control in CONTROLS:
            run.phase = "control:" + control["name"]
            dst = run.directory / "variants" / control["name"]
            shutil.copytree(run.inputs, dst)
            target = dst / control["file"]
            target.write_text(replace_once(target.read_text(), control["old"], control["new"]))
            variant_manifest = {n: sha(dst / n) for n in SOURCES}
            save_json(dst / "source-manifest.json", variant_manifest)
            binary = run.build(control["name"], "/out/variants/" + control["name"])
            outcome = run.execute(control["name"], binary, control["selector"], control["failures"])
            report["controls"].append({**control, "source_sha256": variant_manifest,
                                        "outcome": outcome})
        report["metadata_controls"] = []
        for control in META_CONTROLS:
            run.phase = "metadata:" + control["name"]
            dst = run.directory / "build" / control["name"]
            dst.mkdir()
            source = replace_once((run.inputs / "entry.S").read_text(), control["old"], control["new"])
            (dst / "entry.S").write_text(source)
            run.zero(control["name"] + "-compile", ["g++", "-g", "-fPIC", "-c",
                     "/out/build/" + control["name"] + "/entry.S", "-o",
                     "/out/build/" + control["name"] + "/entry.o"])
            run.artifact("build/" + control["name"] + "/entry.o")
            try:
                run.metadata(control["name"])
            except AuditError as exc:
                require(str(exc) == control["expected_error"], "wrong metadata rejection: " + str(exc))
                report["metadata_controls"].append({**control, "compiled": True, "executed": False,
                    "source_sha256": digest(source.encode()), "rejection": str(exc)})
            else:
                raise AuditError("malformed CFI accepted: " + control["name"])
        require({n: sha(HERE / n) for n in SOURCES} == run.manifest, "live inputs changed")
        require({n: sha(run.inputs / n) for n in SOURCES} == run.manifest, "snapshot inputs changed")
        report["status"] = "passed"
    except BaseException as exc:
        report.update(status="failed", failure_type=type(exc).__name__, failure=str(exc))
        (run.directory / "failure.txt").write_text(traceback.format_exc())
    finally:
        report.update(phase=run.phase, image=IMAGE, image_id=IMAGE_ID,
                      input_sha256=run.manifest, commands=run.commands, artifacts=run.artifacts)
        save_json(run.directory / "result.json", report)
        print(json.dumps({"status": report["status"], "private_run": run.directory.name,
                          "result_sha256": sha(run.directory / "result.json")}), flush=True)
    return 0 if report["status"] == "passed" else 1

if __name__ == "__main__":
    raise SystemExit(main())

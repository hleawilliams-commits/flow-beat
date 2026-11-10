#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain a new offline C11 authored fault-attempt matrix; no native providers."""
import sys
if not __debug__ or sys.flags.optimize:
    raise RuntimeError("optimized Python refused")
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import struct
import subprocess
import traceback
import uuid

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
PLAN = HERE.parent / "activation-fault-attempt-plan/admission-plan.json"
PLAN_SHA = "bc2b0da2aff87b6c15cb0772acd2f10b784cfae54ce58e8da972f09b1058abd1"
PLAN_README_SHA = "5e59440018c14f19a776b7833b135a2fcb07064d5e9118bffef3d3bc099b626b"
SCAFFOLD = HERE.parent / "activation-module-attribution-fixture/run-tests.py"
SCAFFOLD_SHA = "a410d0c50ecfb4f655b4662747cc15ced503b8a920b72a457aa5b132e034c5ee"
IMAGE_ID = "sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90"
SOURCE_NAMES = ("run-tests.py", "core.c", "core.h", "test_fault.c", "test-contract.json")
COMMON = ["-std=c11", "-O1", "-g", "-fno-omit-frame-pointer",
          "-fno-optimize-sibling-calls", "-Wall", "-Wextra", "-Werror",
          "-fdiagnostics-color=never", "-fno-pie"]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
SAN_ENV = {"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1:abort_on_error=0",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
CONTROL_MACROS = {
    "admit-second-contender": "FA_CONTROL_ADMIT_SECOND",
    "reopen-after-unknown": "FA_CONTROL_REOPEN_UNKNOWN",
    "omit-owner-id-copy": "FA_CONTROL_OMIT_ID_COPY",
    "derive-owner-from-write-success": "FA_CONTROL_OWNER_FROM_WRITE",
    "invent-unreturned-write-result": "FA_CONTROL_INVENT_WRITE_RETURN",
    "retry-short-write": "FA_CONTROL_RETRY_SHORT_WRITE",
    "skip-close-after-write-error": "FA_CONTROL_SKIP_AFTER_WRITE_ERROR",
    "retain-stale-errno-after-success": "FA_CONTROL_STALE_ERRNO"}
CONTROL_FAILURES = {
    "admit-second-contender": ["terminal-admission-single-winner"],
    "reopen-after-unknown": ["unknown-attempt-stays-terminal"],
    "omit-owner-id-copy": ["selected-owner-copy-survives-input-change"],
    "derive-owner-from-write-success": ["selected-owner-independent-of-io-outcome"],
    "invent-unreturned-write-result": ["armed-write-without-return-remains-unknown"],
    "retry-short-write": ["short-write-retains-single-write-sequence"],
    "skip-close-after-write-error": ["returned-write-error-still-fsyncs-and-closes"],
    "retain-stale-errno-after-success": ["successful-return-has-no-valid-failure-errno"]}
SAFE_NAME = re.compile(r"[a-z][a-z0-9-]{0,159}\Z")
DIAGNOSTIC = re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|runtime error:|DEADLYSIGNAL|Segmentation fault|FIXTURE_ABORT|warning:|error:", re.I)
MAX_REPORT = 1024 * 1024
SCOPE = "Offline C11 terminal fault-attempt admission with authored value providers"
EXCLUDED = ("checkpoint.json", "execution.lock")

class Refusal(RuntimeError):
    pass

def require(ok, message):
    if not ok:
        raise Refusal(message)

def digest(raw):
    return hashlib.sha256(raw).hexdigest()

def metadata(raw):
    return {"bytes": len(raw), "sha256": digest(raw)}

def encoded(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()

def save(path, value):
    with path.open("xb") as out:
        out.write(encoded(value))

def no_duplicates(pairs):
    out = {}
    for key, value in pairs:
        require(key not in out, "duplicate JSON key")
        out[key] = value
    return out

def decode(raw):
    return json.loads(raw, object_pairs_hook=no_duplicates,
                      parse_constant=lambda _: (_ for _ in ()).throw(Refusal("nonfinite JSON")))

def regular(path):
    require(path.is_file() and not path.is_symlink(), "regular file required: " + path.name)
    return path.read_bytes()

def safe_relative(value):
    p = PurePosixPath(value)
    require(type(value) is str and not p.is_absolute() and p.as_posix() == value
            and value and all(x not in ("", ".", "..") for x in p.parts),
            "canonical relative path")
    return value

def inventory(root, excludes=()):
    files, directories = {}, []
    for p in sorted(root.rglob("*")):
        require(not p.is_symlink(), "symlink in retained packet")
        name = p.relative_to(root).as_posix()
        if name in excludes:
            continue
        if p.is_file():
            files[name] = metadata(p.read_bytes())
        elif p.is_dir():
            directories.append(name)
        else:
            raise Refusal("nonregular retained entry: " + name)
    return {"files": files, "directories": directories}

def select(raw, names, label):
    selected = list(names) if raw is None else raw.split(",")
    require(selected and len(selected) == len(set(selected))
            and all(n in names for n in selected), "known unique " + label)
    return selected

def contract_from(raw):
    value = decode(raw)
    require(type(value) is dict and set(value) == {"schema", "status", "scope", "configuration", "summary_keys", "selectors", "controls", "limits"}
            and value["schema"] == 1, "closed test contract")
    require(value["configuration"] == "FA_FIXTURE" and value["summary_keys"] == ["schema", "selector", "checks", "check_names", "failures", "failed_checks"], "exact fixture summary contract")
    require(value["status"] == "source-frozen-not-executed", "frozen source contract status")
    require(type(value["selectors"]) is dict and value["selectors"], "nonempty fresh selectors")
    groups = set()
    for name, row in value["selectors"].items():
        require(SAFE_NAME.fullmatch(name) and type(row) is dict
                and set(row) == {"group", "checks", "description"}, "selector shape")
        require(row["group"] in ("G1", "G2", "G3", "G4", "G5", "G6", "G7", "G8"), "known case group")
        groups.add(row["group"])
        checks = row["checks"]
        require(type(checks) is list and checks and len(checks) == len(set(checks))
                and all(type(x) is str and SAFE_NAME.fullmatch(x) for x in checks), "ordered unique named checks")
        require(type(row["description"]) is str and row["description"], "selector description")
    require(groups == {"G1", "G2", "G3", "G4", "G5", "G6", "G7", "G8"}, "all eight planned groups represented")
    require(type(value["controls"]) is dict and set(value["controls"]) == set(CONTROL_MACROS), "eight exact controls")
    for name, row in value["controls"].items():
        require(type(row) is dict and set(row) == {"macro", "selector", "expected_failures", "safety"}, "closed control")
        require(row["macro"] == CONTROL_MACROS[name]
                and row["expected_failures"] == CONTROL_FAILURES[name], "exact planned control mutation and failure")
        require(row["selector"] in value["selectors"]
                and all(x in value["selectors"][row["selector"]]["checks"] for x in row["expected_failures"]),
                "control failures belong to selected oracle")
        require(type(row["safety"]) is str and row["safety"], "control safety scope")
    require(type(value["scope"]) is str and type(value["limits"]) is list
            and value["limits"] and all(type(x) is str for x in value["limits"]), "scope and limitations")
    return value

class Elf:
    """Bounded ELF64 metadata only; no invocation or disassembler imports."""
    def __init__(self, raw, kind):
        require(len(raw) >= 64 and raw[:7] == b"\x7fELF\x02\x01\x01", "ELF64 little-endian identity")
        h = struct.unpack_from("<16sHHIQQQIHHHHHH", raw)
        require(h[1] == kind and h[2] == 183 and h[3] == 1 and h[8] == 64, "AArch64 ELF type/header")
        off, width, count, names_index = h[6], h[11], h[12], h[13]
        require(width == 64 and 0 < count < 65535 and names_index < count
                and off + width * count <= len(raw), "complete ordinary ELF section table")
        sections = []
        for i in range(count):
            row = struct.unpack_from("<IIQQQQIIQQ", raw, off + i * width)
            s = dict(zip(("name_offset", "type", "flags", "address", "offset", "size",
                          "link", "info", "alignment", "entry_size"), row))
            require(s["type"] == 8 or s["offset"] + s["size"] <= len(raw), "section bounds")
            s["data"] = b"" if s["type"] == 8 else raw[s["offset"]:s["offset"] + s["size"]]
            sections.append(s)
        names = sections[names_index]["data"]
        for s in sections:
            s["name"] = self.string(names, s["name_offset"])
        self.symbols = []
        tables = 0
        for s in sections:
            if s["type"] != 2:
                continue
            tables += 1
            require(s["entry_size"] == 24 and s["size"] % 24 == 0 and s["link"] < count, "symbol table bounds")
            strings = sections[s["link"]]["data"]
            for at in range(0, s["size"], 24):
                name, info, other, index, address, size = struct.unpack_from("<IBBHQQ", s["data"], at)
                self.symbols.append({"name": self.string(strings, name), "bind": info >> 4,
                                     "type": info & 15, "other": other, "section": index,
                                     "value": address, "size": size})
        require(tables == 1, "one regular symbol table")
        self.sections = sections
        self.kind = kind

    @staticmethod
    def string(raw, at):
        require(at < len(raw), "ELF string offset")
        end = raw.find(b"\0", at)
        require(end >= at, "ELF string terminator")
        return raw[at:end].decode("utf-8", "strict")

    def function(self, name):
        rows = [s for s in self.symbols if s["name"] == name and 0 < s["section"] < len(self.sections)]
        require(len(rows) == 1 and rows[0]["type"] == 2 and rows[0]["size"] > 0, "defined function: " + name)
        row = rows[0]
        section = self.sections[row["section"]]
        require(section["flags"] & 6 == 6 and section["address"] <= row["value"]
                and row["value"] + row["size"] <= section["address"] + section["size"], "allocated executable function extent")
        return row

    def summary(self):
        return {"type": self.kind, "machine": "AArch64", "elf_class": 64, "little_endian": True,
                "sections": [{k: v for k, v in s.items() if k != "data"} for s in self.sections],
                "symbols": self.symbols}

class Run:
    def __init__(self, args):
        self.args = args
        self.path = HERE / "private" / (args.packet or ("run-" + uuid.uuid4().hex))
        require(self.path.parent == HERE / "private" and re.fullmatch(r"run-[0-9a-f]{32}", self.path.name), "owned packet name")
        self.inputs = self.path / "inputs"
        self.work = self.path / "work"
        self.logs = self.path / "commands"
        self.run_id = uuid.uuid4().hex
        self.commands, self.attempts, self.manifest = [], [], {}
        self.results = {"builds": {}, "candidate": {}, "controls": {}}
        self.phase = "preparation"
        self.contract = None
        self.lock_owned = False
        self.packet_owned = False
        self.docker_path = str(Path(args.docker).resolve())
        self.base_environment = {"PATH": "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
                                 "LANG": "C", "LC_ALL": "C", "TMPDIR": str(self.path / "tmp"),
                                 "PYTHONDONTWRITEBYTECODE": "1"}
        for key in ("HOME", "DOCKER_CONFIG", "DOCKER_CONTEXT", "DOCKER_HOST", "DOCKER_TLS_VERIFY", "DOCKER_CERT_PATH"):
            if key in os.environ:
                self.base_environment[key] = os.environ[key]

    def acquire(self):
        save(self.path / "execution.lock", {"schema": 1, "invocation": self.run_id})
        self.lock_owned = True

    def put(self, name, raw, role):
        safe_relative(name)
        require(name not in self.manifest, "no repeated input")
        p = self.inputs / name
        p.parent.mkdir(parents=True, exist_ok=True)
        with p.open("xb") as out:
            out.write(raw)
        self.manifest[name] = dict(metadata(raw), role=role)

    def check_inputs(self):
        require(inventory(self.inputs)["files"] == {
            n: {k: v[k] for k in ("bytes", "sha256")} for n, v in self.manifest.items()},
            "immutable input closure")
        require(regular(HERE / "run-tests.py") == regular(self.inputs / "new/run-tests.py"), "runner unchanged")

    def check_artifacts(self):
        for build in self.results["builds"].values():
            for name, pin in build["artifacts"].items():
                require(metadata(regular(self.work / safe_relative(name))) == pin, "unchanged build artifact")

    def prepare(self):
        require(not self.path.exists(), "exclusive fresh packet")
        self.path.mkdir(parents=True)
        self.packet_owned = True
        self.acquire()
        for p in (self.inputs, self.work, self.logs, self.path / "tmp"):
            p.mkdir()
        save(self.path / "started.json", {"schema": 1, "image": IMAGE_ID, "runtime_gate": "CLOSED", "python": sys.version})
        lockraw = regular(HERE / "review-lock.json")
        lock = decode(lockraw)
        require(type(lock) is dict and set(lock) == {"schema", "status", "files"}
                and lock["schema"] == 1 and lock["status"] == "reviewed-before-build", "review lock")
        require(type(lock["files"]) is dict
                and set(lock["files"]) == set(SOURCE_NAMES) | {"source-ledger.json"}, "closed reviewed source inventory")
        self.put("review-lock.json", lockraw, "reviewed source fence")
        for name, pin in lock["files"].items():
            raw = regular(HERE / name)
            require(metadata(raw) == pin, "reviewed exact source: " + name)
            self.put("new/" + name, raw, "new authored fault-attempt fixture")
        ledger = decode(regular(self.inputs / "new/source-ledger.json"))
        require(ledger["runtime_gate"] == "CLOSED"
                and ledger["source_files"] == {n: lock["files"][n] for n in SOURCE_NAMES}, "ledger exact noncircular source pins")
        raw = regular(PLAN)
        require(digest(raw) == PLAN_SHA, "frozen admission plan")
        plan = decode(raw)
        self.put("plan/admission-plan.json", raw, "reviewed source-only plan")
        raw = regular(PLAN.parent / "README.md")
        require(digest(raw) == PLAN_README_SHA, "frozen plan README")
        self.put("plan/README.md", raw, "reviewed source-only plan clarification")
        for row in plan["sources"]:
            raw = regular(REPO / safe_relative(row["path"]))
            require(metadata(raw) == {k: row[k] for k in ("bytes", "sha256")}, "retained predecessor input pin")
            self.put("frozen/" + safe_relative(row["name"]), raw, "retained source only; not compiled or executed")
        scaffold = regular(SCAFFOLD)
        require(digest(scaffold) == SCAFFOLD_SHA, "pinned prior runner scaffolding")
        self.put("scaffold/prior-run-tests.py", scaffold, "source-only command/ELF scaffold reference; never executed")
        self.contract = contract_from(regular(self.inputs / "new/test-contract.json"))
        require({c["id"]: c["expected_failure_set"] for c in plan["safe_controls"]} == CONTROL_FAILURES,
                "plan exact eight singleton control sets")
        ast.parse(regular(self.inputs / "new/run-tests.py"), filename="run-tests.py")
        save(self.path / "input-manifest.json", self.manifest)
        save(self.path / "source-preparation.json", {
            "schema": 1, "source_files": ledger["source_files"], "plan": metadata(regular(PLAN)),
            "review_lock": metadata(lockraw), "source_transformation_execution": False,
            "compiler_runtime_or_device_execution": False, "old_capture_or_matrix_replay": False})
        self.results["prepared"] = True
        self.check_inputs()
        self.phase = "prepared"

    def resume(self):
        require(self.path.is_dir() and not self.path.is_symlink(), "existing owned packet")
        self.acquire()
        pointer = decode(regular(self.path / "checkpoint.json"))
        require(type(pointer) is dict and set(pointer) == {"schema", "result", "result_pin", "sanitized", "sanitized_pin"}
                and pointer["schema"] == 1, "closed continuation pointer")
        require(re.fullmatch(r"result-[0-9a-f]{32}\.json", pointer["result"])
                and re.fullmatch(r"sanitized-[0-9a-f]{32}\.json", pointer["sanitized"]), "retained result names")
        raw = regular(self.path / pointer["result"])
        public = regular(self.path / pointer["sanitized"])
        require(metadata(raw) == pointer["result_pin"] and metadata(public) == pointer["sanitized_pin"], "continuation result pins")
        previous = decode(raw)
        require(previous["status"] == "passed" and previous["runtime_gate"] == "CLOSED", "failed packets are terminal")
        expected = previous["retained_inventory_before_result"]
        current = inventory(self.path, EXCLUDED)
        expected_files = dict(expected["files"])
        expected_files[pointer["result"]] = pointer["result_pin"]
        expected_files[pointer["sanitized"]] = pointer["sanitized_pin"]
        require(current == {"files": expected_files, "directories": expected["directories"]}, "complete prior packet closure")
        require(decode(public) == sanitized(previous, digest(raw)), "exact prior public projection")
        self.manifest = previous["inputs"]
        require(decode(regular(self.path / "input-manifest.json")) == self.manifest, "retained input manifest")
        self.commands, self.attempts, self.results = previous["commands"], previous["execution_attempts"], previous["results"]
        self.contract = contract_from(regular(self.inputs / "new/test-contract.json"))
        self.check_inputs()
        self.check_artifacts()
        self.packet_owned = True
        self.phase = "resumed"

    def command(self, name, argv, *, container=None, timeout=45):
        require(SAFE_NAME.fullmatch(name) and type(timeout) is int and 0 < timeout < 60, "bounded named command")
        require(name not in {c["name"] for c in self.commands}, "command replay refused: " + name)
        relative = "%03d-%s" % (len(self.commands) + 1, name)
        folder = self.logs / relative
        folder.mkdir()
        spec = {"argv": [str(a) for a in argv], "cwd": str(self.path), "environment": self.base_environment,
                "timeout_seconds": timeout, "container": container}
        save(folder / "command.json", spec)
        proc, output, error, exception, cleanup = None, b"", b"", None, {}
        try:
            proc = subprocess.Popen(spec["argv"], cwd=self.path, env=self.base_environment,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
            output, error = proc.communicate(timeout=timeout)
        except BaseException as exc:
            exception = type(exc).__name__ + ": " + str(exc)
            if isinstance(exc, subprocess.TimeoutExpired):
                output, error = exc.output or b"", exc.stderr or b""
            try:
                (folder / "interrupted-stdout.bin").write_bytes(output)
                (folder / "interrupted-stderr.bin").write_bytes(error)
            except BaseException as capture_error:
                cleanup["prefix_capture_error"] = repr(capture_error)
            finally:
                if proc is not None and proc.poll() is None:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                        cleanup["process_group_kill_requested"] = True
                    except ProcessLookupError:
                        cleanup["process_group_already_gone"] = True
                    except BaseException as kill_error:
                        cleanup["process_group_kill_error"] = repr(kill_error)
                if container is not None:
                    cleanup_argv = [self.docker_path, "rm", "-f", container]
                    cleanup["container_argv"] = cleanup_argv
                    try:
                        cr = subprocess.run(cleanup_argv, env=self.base_environment, cwd=self.path,
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15, check=False)
                        (folder / "cleanup-stdout.bin").write_bytes(cr.stdout)
                        (folder / "cleanup-stderr.bin").write_bytes(cr.stderr)
                        cleanup.update(returncode=cr.returncode, stdout=metadata(cr.stdout), stderr=metadata(cr.stderr))
                    except BaseException as cleanup_error:
                        cleanup["container_error"] = repr(cleanup_error)
                        if isinstance(cleanup_error, subprocess.TimeoutExpired):
                            (folder / "cleanup-interrupted-stdout.bin").write_bytes(cleanup_error.output or b"")
                            (folder / "cleanup-interrupted-stderr.bin").write_bytes(cleanup_error.stderr or b"")
                if proc is not None:
                    try:
                        output, error = proc.communicate(timeout=5)
                    except BaseException as drain_error:
                        cleanup["drain_error"] = repr(drain_error)
        finally:
            (folder / "stdout.bin").write_bytes(output)
            (folder / "stderr.bin").write_bytes(error)
            receipt = dict(spec, name=name, directory=relative, returncode=None if proc is None else proc.returncode,
                           exception=exception, cleanup=cleanup, stdout=metadata(output), stderr=metadata(error))
            save(folder / "receipt.json", receipt)
            self.commands.append(dict(receipt, receipt_sha256=digest(encoded(receipt))))
        require(exception is None, "command exception: " + name + ": " + str(exception))
        return receipt["returncode"], output, error

    def docker(self, name, argv, extra_environment=None, evidence=None):
        container = "az-fault-attempt-" + self.path.name[4:] + "-" + str(len(self.commands) + 1)
        env = {"PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
               "LANG": "C", "LC_ALL": "C", **(extra_environment or {})}
        command = [self.docker_path, "run", "--name", container, "--rm", "--pull=never",
                   "--network=none", "--platform=linux/arm64", "--read-only",
                   "--tmpfs", "/tmp:rw,nosuid,nodev", "--workdir", "/out",
                   "--mount", "type=bind,source=" + str(self.inputs) + ",target=/src,readonly",
                   "--mount", "type=bind,source=" + str(self.work) + ",target=/out" + (",readonly" if evidence else "")]
        if evidence is not None:
            command += ["--mount", "type=bind,source=" + str(evidence) + ",target=/evidence"]
        command += [IMAGE_ID, "env", "-i"]
        command += [k + "=" + v for k, v in env.items()] + list(argv)
        return self.command(name, command, container=container)

    def clean(self, name, argv, *, host=False, empty_stdout=False):
        rc, out, err = self.command(name, argv) if host else self.docker(name, argv)
        require(rc == 0 and err == b"" and not DIAGNOSTIC.search(out.decode("utf-8", "replace")), "clean successful command: " + name)
        if empty_stdout:
            require(out == b"", "empty compile/link stdout: " + name)
        return out

    def inspect_toolchain(self):
        if "image" not in self.results:
            raw = self.clean("image-inspect", [self.docker_path, "image", "inspect", IMAGE_ID], host=True)
            data = decode(raw)
            require(type(data) is list and len(data) == 1 and type(data[0]) is dict, "one pinned image")
            info = data[0]
            require(info.get("Id") == IMAGE_ID and info.get("Architecture") == "arm64"
                    and info.get("Os") == "linux", "pinned Linux AArch64 image")
            self.results["image"] = {k: info[k] for k in ("Id", "Architecture", "Os")}
        if "toolchain" not in self.results:
            version = self.clean("compiler-version", ["gcc", "--version"])
            target = self.clean("compiler-target", ["gcc", "-dumpmachine"])
            require(b"7.5.0" in version.splitlines()[0] and target.strip() == b"aarch64-linux-gnu", "GCC 7.5 AArch64 target")
            self.results["toolchain"] = {"version": metadata(version), "target": target.decode().strip()}

    def flags(self, mode, fixture=False, macro=None):
        flags = COMMON + (SAN_FLAGS if mode == "asan-ubsan" else [])
        if fixture:
            flags += ["-DFA_FIXTURE=1"]
        if macro:
            flags += ["-D" + macro + "=1"]
        return flags

    def build_variant(self, variant, mode):
        self.phase = "build-" + variant + "-" + mode
        key = variant + "-" + mode
        require(key not in self.results["builds"], "successful build replay refused")
        standalone = variant == "standalone"
        macro = CONTROL_MACROS.get(variant)
        flags = self.flags(mode, not standalone, macro)
        folder = self.work / "build" / variant / mode
        folder.mkdir(parents=True)
        target = "/out/" + folder.relative_to(self.work).as_posix()
        sources = ("core",) if standalone else ("core", "test_fault")
        for source in sources:
            self.clean("compile-" + key + "-" + source.replace("_", "-"),
                       ["gcc", *flags, "-I/src/new", "-c", "/src/new/" + source + ".c",
                        "-o", target + "/" + source + ".o"], empty_stdout=True)
        core = Elf(regular(folder / "core.o"), 1)
        for name in ("fa_run", "fa_observe", "fa_admit_internal", "fa_publish_internal"):
            core.function(name)
        if standalone:
            require(not any(s["section"] and s["name"].startswith("fa_fixture_") for s in core.symbols),
                    "standalone excludes fixture scheduling symbols")
        else:
            self.clean("link-" + key, ["gcc", *flags, "-no-pie", target + "/core.o",
                                      target + "/test_fault.o", "-o", target + "/test-fault"], empty_stdout=True)
            executable = Elf(regular(folder / "test-fault"), 2)
            executable.function("fa_run")
            executable.function("fa_observe")
            save(folder / "linked-symbols.json", executable.summary())
        for suffix, argv in (
                ("core-disassembly", ["objdump", "-dr", target + "/core.o"]),
                ("core-elf", ["readelf", "-W", "-h", "-s", target + "/core.o"])):
            raw = self.clean(key + "-" + suffix, argv)
            (folder / (suffix + ".txt")).write_bytes(raw)
        if not standalone:
            raw = self.clean(key + "-linked-disassembly", ["objdump", "-d", target + "/test-fault"])
            (folder / "linked-disassembly.txt").write_bytes(raw)
        save(folder / "core-symbols.json", core.summary())
        save(folder / "configuration.json", {
            "flags": flags, "macro": macro, "FA_FIXTURE": not standalone, "compile_only": standalone,
            "all_providers_authored_values": True, "disassembly_retained_without_lock_free_or_signal_safety_oracle": True})
        artifacts = {p.relative_to(self.work).as_posix(): metadata(regular(p)) for p in sorted(folder.iterdir())}
        self.results["builds"][key] = {
            "kind": "standalone" if standalone else "host", "variant": variant, "mode": mode,
            "compile_only": standalone, "macro": macro, "artifacts": artifacts,
            "binary": None if standalone else target + "/test-fault"}

    def build(self):
        modes = select(self.args.modes, ("plain", "asan-ubsan"), "modes")
        variants = select(self.args.variants, ("standalone", "candidate", *CONTROL_MACROS), "build variants")
        self.inspect_toolchain()
        for mode in modes:
            for variant in variants:
                self.build_variant(variant, mode)
        self.check_inputs()
        self.check_artifacts()
        self.phase = "built-selected-variants"

    def evidence_shape(self, facts, expected):
        # Shape/bounds only: deliberate semantic control defects must remain
        # visible to their named C oracles, not be rejected by duplicate policy.
        def closed(value, keys, label):
            require(type(value) is dict and set(value) == set(keys), label)
        def uint(value, bits=32):
            return type(value) is int and 0 <= value < (1 << bits)
        def sint(value, bits=32):
            return type(value) is int and -(1 << (bits - 1)) <= value < (1 << (bits - 1))
        def enum(value, maximum):
            return type(value) is int and 0 <= value <= maximum
        def boolean(value):
            return type(value) is bool
        def bytes33(value):
            return type(value) is str and re.fullmatch(r"[0-9a-f]{66}", value) is not None
        def owner(value):
            closed(value, ("kind", "index", "process_id", "id_hex"), "owner shape")
            require(enum(value["kind"], 1) and uint(value["index"])
                    and uint(value["process_id"], 64) and bytes33(value["id_hex"]), "owner value domains")
        def io(value):
            closed(value, ("raw", "failure_errno"), "authored IO shape")
            require(sint(value["raw"], 64) and sint(value["failure_errno"]), "authored IO value domains")
        def result(value):
            closed(value, ("selection_known", "owner", "known_failure", "short_write",
                           "violation", "event_count", "events"), "working/final result shape")
            require(all(boolean(value[n]) for n in ("selection_known", "known_failure", "short_write", "violation"))
                    and enum(value["event_count"], 6), "working/final result scalar domains")
            owner(value["owner"])
            require(type(value["events"]) is list and len(value["events"]) == 6, "all six fixed event slots")
            for event in value["events"]:
                closed(event, ("kind", "entered", "returned_recorded", "raw", "errno_valid",
                               "error_number", "requested_length", "payload_hex"), "event shape")
                require(enum(event["kind"], 5)
                        and all(boolean(event[n]) for n in ("entered", "returned_recorded", "errno_valid"))
                        and sint(event["raw"], 64) and sint(event["error_number"])
                        and uint(event["requested_length"], 64) and bytes33(event["payload_hex"]),
                        "event value domains")
        def snapshot(value):
            closed(value, ("state", "has_payload", "result"), "public snapshot shape")
            require(enum(value["state"], 2) and boolean(value["has_payload"]), "public state domains")
            result(value["result"])
        def private(value):
            closed(value, ("phase", "next_kind", "current_event", "working", "raw_oracle"), "private serial observation shape")
            require(enum(value["phase"], 6) and enum(value["next_kind"], 5)
                    and enum(value["current_event"], 5), "private serial state domains")
            result(value["working"])
            raw = value["raw_oracle"]
            closed(raw, ("available", "sequence", "kind", "selection", "io"), "raw oracle shape")
            require(boolean(raw["available"]) and uint(raw["sequence"]) and enum(raw["kind"], 5), "raw oracle domains")
            owner(raw["selection"])
            io(raw["io"])

        closed(facts, set(expected) | {"source_scope", "script", "decoy_script", "expected_owner", "steps",
                                     "requests", "observations"}, "closed evidence top-level schema")
        require(all(facts[k] == v for k, v in expected.items()), "raw/stdout exact agreement")
        require(facts["source_scope"] == {
            "providers": "authored-values-only", "native_fault_provider_functions_present": False,
            "signals": False, "threads": False, "evidence_io": "stdio-report-only"},
            "explicit source-only provider boundary")
        for source_name in ("script", "decoy_script"):
            script = facts[source_name]
            closed(script, ("selection", "open", "write", "sync", "close"), "script shape")
            owner(script["selection"])
            for name in ("open", "write", "sync", "close"):
                io(script[name])
        owner(facts["expected_owner"])
        require(enum(facts["steps"], 40), "bounded authored step count")
        require(type(facts["requests"]) is list and len(facts["requests"]) <= 96, "bounded complete request trace")
        for row in facts["requests"]:
            closed(row, ("actor", "captured", "before", "after", "result"), "request observation shape")
            require(type(row["actor"]) is str and re.fullmatch(r"[A-Za-z0-9-]{1,64}", row["actor"])
                    and all(enum(row[n], 2) for n in ("captured", "before", "after"))
                    and enum(row["result"], 3), "request observation domains")
        require(type(facts["observations"]) is list and 1 <= len(facts["observations"]) <= 160,
                "bounded complete serial observations")
        for row in facts["observations"]:
            closed(row, ("label", "public", "private"), "serial observation shape")
            require(type(row["label"]) is str and re.fullmatch(r"[A-Za-z0-9-]{1,64}", row["label"]), "observation label")
            snapshot(row["public"])
            private(row["private"])

    def execute(self, variant, mode, selector, failures):
        self.phase = "run-" + variant + "-" + mode + "-" + selector
        key = variant + "-" + mode
        require(key in self.results["builds"], "retained reviewed build")
        name = "run-" + key + "-" + selector
        require(name not in {c["name"] for c in self.commands}, "fresh selector required")
        folder = self.work / "evidence" / variant / mode / selector
        folder.mkdir(parents=True)
        build = self.results["builds"][key]
        row = {"variant": variant, "mode": mode, "selector": selector, "returned": False, "accepted": False}
        self.attempts.append(row)
        rc, raw, err = self.docker(name, [build["binary"], selector, "/evidence/report.json"],
                                   SAN_ENV if mode == "asan-ubsan" else {}, evidence=folder)
        row.update(returned=True, returncode=rc)
        checks = self.contract["selectors"][selector]["checks"]
        expected = {"schema": 1, "selector": selector, "checks": len(checks), "check_names": checks,
                    "failures": len(failures), "failed_checks": failures}
        value = decode(raw)
        require(value == expected and type(value["checks"]) is int and type(value["failures"]) is int, "exact named result")
        require(raw == (json.dumps(expected, separators=(",", ":")) + "\n").encode(), "canonical stdout bytes")
        require(rc == (1 if failures else 0) and err == b"", "exact assertion exit and no sanitizer diagnostics")
        inv = inventory(folder)
        require(set(inv["files"]) == {"report.json"} and not inv["directories"], "one evidence report")
        report = regular(folder / "report.json")
        require(0 < len(report) <= MAX_REPORT, "bounded report")
        self.evidence_shape(decode(report), expected)
        row["accepted"] = True
        out = {"report": expected, "returncode": rc, "evidence": metadata(report),
               "stdout": metadata(raw), "stderr": metadata(err), "FA_FIXTURE": True,
               "sanitizers": "address,undefined" if mode == "asan-ubsan" else "none",
               "leak_detection": mode == "asan-ubsan"}
        bucket = "candidate" if variant == "candidate" else "controls"
        self.results[bucket].setdefault(mode, {})[selector if bucket == "candidate" else variant] = out
        self.check_inputs()
        self.check_artifacts()

    def run(self):
        modes = select(self.args.modes, ("plain", "asan-ubsan"), "modes")
        variants = select(self.args.variants, ("candidate", *CONTROL_MACROS), "runtime variants")
        selectors = select(self.args.selectors, self.contract["selectors"], "selectors")
        for mode in modes:
            for variant in variants:
                if variant == "candidate":
                    for selector in selectors:
                        self.execute(variant, mode, selector, [])
                else:
                    c = self.contract["controls"][variant]
                    self.execute(variant, mode, c["selector"], c["expected_failures"])
        self.phase = "complete-selected-runtime"

    def perform(self):
        if self.args.packet:
            require(self.args.phase != "prepare", "preparation belongs to a fresh packet")
            self.resume()
        else:
            require(self.args.phase in ("prepare", "all"), "fresh packet must prepare first")
            self.prepare()
        save(self.path / ("invocation-" + self.run_id + ".json"), {
            "schema": 1, "phase": self.args.phase, "modes": self.args.modes,
            "variants": self.args.variants, "selectors": self.args.selectors})
        if self.args.phase in ("build", "all"):
            self.build()
        if self.args.phase == "all":
            selected = select(self.args.variants, ("standalone", "candidate", *CONTROL_MACROS), "all-phase variants")
            runtime = [name for name in selected if name != "standalone"]
            if runtime:
                self.args.variants = ",".join(runtime)
                self.run()
        elif self.args.phase == "run":
            self.run()
        return {"schema": 1, "status": "passed", "phase": self.phase, "scope": SCOPE,
                "runtime_gate": "CLOSED", "inputs": self.manifest, "results": self.results,
                "commands": self.commands, "execution_attempts": self.attempts,
                "old_capture_or_matrix_replay": False, "native_firmware_or_device_access": False}

def sanitized(result, private_hash):
    r = result.get("results", {})
    return {
        "schema": 1, "status": result["status"], "runtime_gate": "CLOSED", "scope": SCOPE,
        "private_result_sha256": private_hash, "inputs": result.get("inputs", {}),
        "candidate": r.get("candidate", {}), "controls": r.get("controls", {}),
        "builds": {k: {f: v[f] for f in ("kind", "variant", "mode", "compile_only", "macro", "artifacts")}
                   for k, v in r.get("builds", {}).items()},
        "commands": [{"name": c["name"], "returncode": c["returncode"], "exception_present": c["exception"] is not None,
                      "stdout": c["stdout"], "stderr": c["stderr"], "receipt_sha256": c["receipt_sha256"]}
                     for c in result.get("commands", [])],
        "execution_attempts": result.get("execution_attempts", []),
        "output_streams": 2 * len(result.get("commands", [])),
        "retained_files_including_private_result": result.get("retained_files_including_result"),
        "image": r.get("image"), "toolchain": r.get("toolchain"),
        "FA_FIXTURE": True, "providers_are_authored_values": True,
        "native_provider_safety_proved": False, "actual_signal_or_concurrency_execution": False,
        "old_capture_or_matrix_replay": False, "native_firmware_or_device_access": False}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--docker", default=shutil.which("docker") or "/opt/homebrew/bin/docker")
    p.add_argument("--phase", choices=("prepare", "build", "run", "all"), default="prepare")
    p.add_argument("--packet")
    p.add_argument("--modes")
    p.add_argument("--variants")
    p.add_argument("--selectors")
    args = p.parse_args()
    run = Run(args)
    result, status = None, 1
    try:
        result = run.perform()
        status = 0
    except BaseException as exc:
        result = {"schema": 1, "status": "failed", "phase": run.phase, "scope": SCOPE,
                  "error": type(exc).__name__ + ": " + str(exc), "traceback": traceback.format_exc(),
                  "inputs": run.manifest, "results": run.results, "commands": run.commands,
                  "execution_attempts": run.attempts, "runtime_gate": "CLOSED"}
    finally:
        try:
            if run.packet_owned:
                result["retained_inventory_excludes"] = list(EXCLUDED)
                result["retained_inventory_before_result"] = inventory(run.path, EXCLUDED)
                result["retained_files_including_result"] = len(result["retained_inventory_before_result"]["files"]) + 1
                name = "result-" + run.run_id + ".json"
                public_name = "sanitized-" + run.run_id + ".json"
                save(run.path / name, result)
                projection = sanitized(result, digest(encoded(result)))
                save(run.path / public_name, projection)
                pointer = {"schema": 1, "result": name, "result_pin": metadata(encoded(result)),
                           "sanitized": public_name, "sanitized_pin": metadata(encoded(projection))}
                (run.path / "checkpoint.json").write_bytes(encoded(pointer))
                print(json.dumps({"status": result["status"], "packet": run.path.name, "result": name,
                                  "private_result_sha256": digest(encoded(result)),
                                  "sanitized_result_sha256": digest(encoded(projection))}, sort_keys=True))
            else:
                print("refused before owned packet mutation", file=sys.stderr)
        finally:
            if run.lock_owned:
                (run.path / "execution.lock").unlink()
    return status

if __name__ == "__main__":
    sys.exit(main())

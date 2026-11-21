#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain a new offline C11 attempt/attribution composition matrix; no native providers."""
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
PLAN = HERE.parent / "activation-attempt-attribution-plan/composition-plan.json"
PLAN_SHA = "56a2cff378642dffa38a8b9bb37ccd2bd702c2c3a01632ec649c50b98cd25d43"
PLAN_README_SHA = "1cbc6758ed4054b8182cf5fbf8e763ff1fc602d3a5e3576da7e531befdc3fe2b"
SCAFFOLD = HERE.parent / "activation-fault-attempt-fixture/run-tests.py"
SCAFFOLD_SHA = "13bb85b861cd97888508a72652bc35fdaa94e7684ab274e32143f75666709af6"
IMAGE_ID = "sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90"
SOURCE_NAMES = ("run-tests.py", "core.c", "core.h", "test_attempt.c", "test-contract.json",
                "extract-readers.py", "reader-extraction.json", "standalone-adoption.json")
METHOD_NAMES = ("ac_run", "ac_observe", "ac_admit_internal", "ac_publish_internal",
                "ac_select_values", "ma_module_snapshot", "ma_generated_index", "ma_owner_index", "rc_lookup")
COMMON = ["-std=c11", "-O1", "-g", "-fno-omit-frame-pointer",
          "-fno-optimize-sibling-calls", "-Wall", "-Wextra", "-Werror",
          "-fdiagnostics-color=never", "-fno-pie"]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
SAN_ENV = {"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1:abort_on_error=0",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
CONTROL_MACROS = {
    "selection-order": "AC_CONTROL_SELECT_BEFORE_CAS",
    "single-authority": "AC_CONTROL_SECOND_ADMISSION",
    "resolved-index": "AC_CONTROL_BINDING_AS_SLOT",
    "process-handoff": "AC_CONTROL_NARROW_PROCESS",
    "frozen-selection": "AC_CONTROL_RESELECT_AT_RECORD",
    "core-fallback": "AC_CONTROL_STOP_ON_CORE_PC",
    "noowner-freeze": "AC_CONTROL_REFRESH_NEGATIVE",
    "private-selection": "AC_CONTROL_EXPOSE_RAW_SELECTION"}
CONTROL_FAILURES = {
    "selection-order": ["suppressed-request-performs-no-selection"],
    "single-authority": ["one-admission-reaches-selection-and-completion"],
    "resolved-index": ["generated-selection-uses-resolved-module-slot"],
    "process-handoff": ["selection-handoff-preserves-full-process-identity"],
    "frozen-selection": ["recorded-selection-matches-original-return"],
    "core-fallback": ["core-pc-continues-community-fallback"],
    "noowner-freeze": ["noowner-return-is-not-refreshed"],
    "private-selection": ["selection-return-remains-private-until-completion"]}
SAFE_NAME = re.compile(r"[a-z][a-z0-9-]{0,159}\Z")
DIAGNOSTIC = re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|runtime error:|DEADLYSIGNAL|Segmentation fault|FIXTURE_ABORT|warning:|error:", re.I)
MAX_REPORT = 16 * 1024 * 1024
SCOPE = "Offline C11 terminal attempt and immutable attribution composition with authored value providers"
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
    require(value["configuration"] == "AC_FIXTURE" and value["summary_keys"] == ["schema", "selector", "checks", "check_names", "failures", "failed_checks"], "exact fixture summary contract")
    require(value["status"] == "frozen-source-contract-before-first-build", "frozen source contract status")
    require(type(value["selectors"]) is dict and value["selectors"], "nonempty fresh selectors")
    groups = set()
    for name, row in value["selectors"].items():
        require(SAFE_NAME.fullmatch(name) and type(row) is dict
                and set(row) == {"group", "checks"}, "selector shape")
        require(row["group"] in ("C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "control"), "known case group")
        groups.add(row["group"])
        checks = row["checks"]
        require(type(checks) is list and 0 < len(checks) <= 16 and len(checks) == len(set(checks))
                and all(type(x) is str and SAFE_NAME.fullmatch(x) for x in checks), "ordered unique named checks")
    require(groups == {"C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "control"}, "all nine planned groups represented")
    require(type(value["controls"]) is dict and set(value["controls"]) == set(CONTROL_MACROS), "eight exact controls")
    for name, row in value["controls"].items():
        require(type(row) is dict and set(row) == {"macro", "selector", "expected_failures", "safety"}, "closed control")
        require(row["macro"] == CONTROL_MACROS[name]
                and row["expected_failures"] == CONTROL_FAILURES[name], "exact planned control mutation and failure")
        require(row["selector"] in value["selectors"]
                and all(x in value["selectors"][row["selector"]]["checks"] for x in row["expected_failures"]),
                "control failures belong to selected oracle")
        require(type(row["safety"]) is str and row["safety"], "control safety scope")
    fixed_limits = {"checks": 16, "observations": 128, "requests": 16, "setup_records": 48,
                    "step_calls": 96, "selection_trace": 51, "selector_probes": 18, "selector_cells": 32,
                    "selector_snapshots": 1, "control_scratch": 4, "diagnostics": 8, "report_bytes": MAX_REPORT}
    limit_text = {"ordinary_successful_run", "scheduling", "snapshot_refusal", "replay"}
    require(type(value["scope"]) is str and value["scope"] and type(value["limits"]) is dict
            and set(value["limits"]) == set(fixed_limits) | limit_text, "closed source limits")
    require(all(type(value["limits"][n]) is int and value["limits"][n] == v for n, v in fixed_limits.items())
            and all(type(value["limits"][n]) is str and value["limits"][n] for n in limit_text),
            "exact bounded source contract and limitations")
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

    def extent(self, name):
        row = self.function(name)
        section = self.sections[row["section"]]
        offset = row["value"] - section["address"]
        body = section["data"][offset:offset + row["size"]]
        require(len(body) == row["size"], "complete method bytes")
        return {"section": row["section"], "value": row["value"], "size": row["size"],
                "body_sha256": digest(body)}

    def reviewed_fini_warning(self, stderr, stdout):
        """Classify one exact metadata diagnostic from this object, never runtime output."""
        require(self.kind == 1, "warning classification requires relocatable object")
        rows = [(i, x) for i, x in enumerate(self.sections) if x["name"] == ".rela.fini_array.00099"]
        require(len(rows) == 1, "one named fini relocation section")
        index, rela = rows[0]
        require(rela["type"] == 4 and rela["entry_size"] == 24 and rela["size"] == 24
                and rela["flags"] == 64 and rela["alignment"] == 8
                and rela["link"] < len(self.sections) and self.sections[rela["link"]]["type"] == 2
                and rela["info"] < len(self.sections), "single RELA linked to symbol table and target")
        target = self.sections[rela["info"]]
        require(target["name"] == ".fini_array.00099" and target["type"] == 15 and target["flags"] == 3
                and target["size"] == 8 and target["alignment"] == 8 and target["entry_size"] == 8
                and target["data"] == bytes(8), "one aligned writable allocated FINI_ARRAY cell")
        offset, info, addend = struct.unpack("<QQq", rela["data"])
        symbol_index, relocation_type = info >> 32, info & 0xffffffff
        require(offset == 0 and relocation_type == 257 and symbol_index < len(self.symbols),
                "one AArch64 ABS64 relocation at array cell zero")
        symbol = self.symbols[symbol_index]
        require(symbol["type"] == 3 and symbol["value"] == 0 and symbol["size"] == 0
                and 0 < symbol["section"] < len(self.sections)
                and self.sections[symbol["section"]]["name"] == ".text",
                "relocation names the defined text section symbol")
        name = "_GLOBAL__sub_D_00099_0_rc_lookup"
        finalizer = self.function(name)
        require(finalizer["section"] == symbol["section"] and finalizer["value"] == addend
                and addend >= 0 and addend % 4 == 0 and finalizer["size"] == 32,
                "relocation exactly addresses bounded named compiler finalizer")
        expected = ("readelf: Warning: [%2d]: Info field (%d) should index a relocatable section.\n"
                    % (index, rela["info"])).encode()
        require(stderr == expected, "only exact object-derived FINI_ARRAY diagnostic")
        text = stdout.decode("utf-8", "strict")
        require(not DIAGNOSTIC.search(text) and name in text
                and "Relocation section '.rela.fini_array.00099'" in text and "FINI_ARRAY" in text,
                "retained metadata stdout contains corresponding symbol and relocation")
        keys = ("name", "type", "flags", "link", "info", "alignment", "entry_size", "size")
        return {"classification": "single FINI_ARRAY relocation to bounded named compiler finalizer",
                "section": dict(index=index, **{k: rela[k] for k in keys}),
                "target": dict(index=rela["info"], **{k: target[k] for k in keys}),
                "relocation": {"offset": offset, "type": relocation_type, "symbol_index": symbol_index,
                               "symbol_section": symbol["section"], "addend": addend},
                "finalizer": {"name": name, "extent": self.extent(name)},
                "scope": "Metadata classification only; no tool-defect cause or runtime safety inference"}

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
        if "extraction" in self.results:
            expected = self.results["extraction"]["artifacts"]
            actual = inventory(self.work / "generated")
            require(not actual["directories"] and
                    {"generated/" + n: v for n, v in actual["files"].items()} == expected,
                    "complete unchanged generated extraction")
        if "codegen_review" in self.results:
            review = self.results["codegen_review"]
            require(metadata(regular(self.work / "review/codegen-review.json")) == review["artifact"],
                    "unchanged adopted codegen review")
            require(inventory(self.work / "review") == {
                "files": {"codegen-review.json": review["artifact"]}, "directories": []},
                "closed adopted review inventory")
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
            self.put("new/" + name, raw, "new authored attempt/attribution fixture")
        ledger = decode(regular(self.inputs / "new/source-ledger.json"))
        require(ledger["runtime_gate"] == "CLOSED"
                and ledger["source_files"] == {n: lock["files"][n] for n in SOURCE_NAMES}, "ledger exact noncircular source pins")
        raw = regular(PLAN)
        require(digest(raw) == PLAN_SHA, "frozen composition plan")
        plan = decode(raw)
        self.put("plan/composition-plan.json", raw, "reviewed source-only plan")
        raw = regular(PLAN.parent / "README.md")
        require(digest(raw) == PLAN_README_SHA, "frozen plan README")
        self.put("plan/README.md", raw, "reviewed source-only plan clarification")
        for row in plan["sources"]:
            raw = regular(REPO / safe_relative(row["path"]))
            require(metadata(raw) == {k: row[k] for k in ("bytes", "sha256")}, "retained predecessor input pin")
            role = ("retained source container; only six exact extracted reader/storage fragments and the pinned types header are compiled"
                    if row["name"] in ("module/attribution.inc", "generated/receipt-body.inc", "generated/receipt-types.h")
                    else "reference-only retained source or evidence; not compiled or executed")
            self.put("frozen/" + safe_relative(row["name"]), raw, role)
        scaffold = regular(SCAFFOLD)
        require(digest(scaffold) == SCAFFOLD_SHA, "pinned prior runner scaffolding")
        self.put("scaffold/prior-run-tests.py", scaffold, "source-only command/ELF scaffold reference; never executed")
        self.contract = contract_from(regular(self.inputs / "new/test-contract.json"))
        require({c["id"]: c["expected_failures"] for c in plan["safe_controls"]} == {CONTROL_MACROS[n]: v for n, v in CONTROL_FAILURES.items()},
                "plan exact eight singleton control sets")
        ast.parse(regular(self.inputs / "new/run-tests.py"), filename="run-tests.py")
        if self.args.adopt_standalone:
            self.adopt_standalone()
            save(self.path / "input-manifest.json", self.manifest)
        else:
            save(self.path / "input-manifest.json", self.manifest)
            self.extract_readers()
        save(self.path / "source-preparation.json", {
            "schema": 1, "source_files": ledger["source_files"], "plan": metadata(regular(PLAN)),
            "review_lock": metadata(lockraw), "source_transformation_execution": not bool(self.args.adopt_standalone),
            "standalone_adoption": self.results.get("standalone_adoption"),
            "extraction": self.results["extraction"],
            "compiler_runtime_or_device_execution": False, "old_capture_or_matrix_replay": False})
        self.results["prepared"] = True
        self.check_inputs()
        self.phase = "prepared"

    def adopt_standalone(self):
        """Copy a reviewed terminal packet into fresh custody; execute no prior command."""
        self.phase = "standalone-artifact-adoption"
        specraw = regular(self.inputs / "new/standalone-adoption.json")
        spec = decode(specraw)
        require(set(spec) == {"schema", "status", "runtime_gate", "source_result", "source_error", "source_phase",
                              "command_pins", "changed_inputs", "added_input", "generated", "objects", "warning", "limits"}
                and spec["schema"] == 1 and spec["status"] == "reviewed-metadata-only-adoption"
                and spec["runtime_gate"] == "CLOSED", "closed reviewed standalone adoption specification")
        source_result_path = Path(self.args.adopt_standalone).resolve()
        require(source_result_path.name == spec["source_result"]["name"]
                and source_result_path.parent != self.path, "specific distinct terminal source packet")
        source = source_result_path.parent
        raw = regular(source_result_path)
        require(metadata(raw) == {k: spec["source_result"][k] for k in ("bytes", "sha256")}, "exact failed source result")
        result = decode(raw)
        require(result["status"] == "failed" and result["error"] == spec["source_error"]
                and result["phase"] == spec["source_phase"] and result["runtime_gate"] == "CLOSED"
                and result["execution_attempts"] == [] and result["results"]["candidate"] == {}
                and result["results"]["controls"] == {}, "metadata-only failure before any fixture execution")
        require(not (source / "execution.lock").exists(), "source packet has no active execution lock")
        pointer = decode(regular(source / "checkpoint.json"))
        require(pointer["result"] == source_result_path.name and pointer["result_pin"] == metadata(raw)
                and metadata(regular(source / safe_relative(pointer["sanitized"]))) == pointer["sanitized_pin"],
                "terminal source checkpoint and public projection pins")
        expected = dict(result["retained_inventory_before_result"]["files"])
        expected.update({source_result_path.name: metadata(raw), pointer["sanitized"]: pointer["sanitized_pin"],
                         "checkpoint.json": metadata(regular(source / "checkpoint.json"))})
        closure = inventory(source)
        require(closure["files"] == expected
                and closure["directories"] == result["retained_inventory_before_result"]["directories"],
                "full unchanged terminal source packet closure")
        old_inputs = decode(regular(source / "input-manifest.json"))
        require(old_inputs == result["inputs"] and inventory(source / "inputs")["files"] == {
            n: {k: v[k] for k in ("bytes", "sha256")} for n, v in old_inputs.items()}, "complete old immutable input closure")
        changed = {"review-lock.json", "new/run-tests.py", "new/source-ledger.json"}
        require(set(spec["changed_inputs"]) == changed and spec["added_input"] == "new/standalone-adoption.json"
                and set(self.manifest) == set(old_inputs) | {spec["added_input"]}, "only reviewed orchestration source changes")
        for name in old_inputs:
            if name not in changed:
                require(regular(self.inputs / name) == regular(source / "inputs" / name),
                        "byte-identical compiled, test, extraction and retained input: " + name)
        commands = result["commands"]
        require([c["name"] for c in commands] == list(spec["command_pins"]), "exact ten retained command sequence")
        retained_commands = []
        for command in commands:
            name = command["name"]
            base = source / "commands" / safe_relative(command["directory"])
            receipt_raw = regular(base / "receipt.json")
            receipt = decode(receipt_raw)
            require(dict(receipt, receipt_sha256=digest(receipt_raw)) == command,
                    "source result binds complete command receipt")
            invocation = decode(regular(base / "command.json"))
            require(invocation == {k: receipt[k] for k in invocation}
                    and set(invocation) == {"argv", "container", "cwd", "environment", "timeout_seconds"},
                    "retained command invocation exactly matches receipt")
            out, err = regular(base / "stdout.bin"), regular(base / "stderr.bin")
            pins = {"receipt": metadata(receipt_raw), "stdout": metadata(out), "stderr": metadata(err)}
            require(pins == spec["command_pins"][name] and command["stdout"] == pins["stdout"]
                    and command["stderr"] == pins["stderr"] and command["returncode"] == 0
                    and command["exception"] is None and command["cleanup"] == {}, "reviewed complete source command outcomes")
            if name != spec["warning"]["command"]:
                require(err == b"" and not DIAGNOSTIC.search(out.decode("utf-8", "replace")), "all other source diagnostics remain empty")
            retained_commands.append({"name": name, "directory": "adoption/packet/commands/" + command["directory"],
                                      "returncode": 0, **pins})
        require(result["results"]["image"] == {"Id": IMAGE_ID, "Architecture": "arm64", "Os": "linux"}
                and result["results"]["toolchain"]["target"] == "aarch64-linux-gnu", "retained fixed toolchain identity")
        # Preserve every source file, including both success/failure projections and original command streams.
        for name, pin in closure["files"].items():
            data = regular(source / name)
            require(metadata(data) == pin, "source closure stable during adoption")
            self.put("adoption/packet/" + name, data, "immutable prior failed packet; retained evidence only, never replayed")
        adopted = self.inputs / "adoption/packet"
        self.results["image"] = result["results"]["image"]
        self.results["toolchain"] = result["results"]["toolchain"]
        self.results["extraction"] = result["results"]["extraction"]
        require(self.results["extraction"]["artifacts"] == spec["generated"]
                and self.results["extraction"]["contract"] == metadata(regular(self.inputs / "new/reader-extraction.json")),
                "adopt exact successful extraction and unchanged contract")
        (self.work / "generated").mkdir()
        for name, pin in spec["generated"].items():
            data = regular(adopted / "work" / safe_relative(name))
            require(metadata(data) == pin, "reviewed generated output pin")
            with (self.work / name).open("xb") as stream:
                stream.write(data)
        warning = None
        for mode in ("plain", "asan-ubsan"):
            key = "standalone-" + mode
            folder = self.work / "build/standalone" / mode
            folder.mkdir(parents=True)
            source_folder = adopted / "work/build/standalone" / mode
            for name in ("core.o", "core-disassembly.txt"):
                data = regular(source_folder / name)
                require(metadata(data) == spec["objects"][mode][name], "exact adopted object/disassembly")
                with (folder / name).open("xb") as stream:
                    stream.write(data)
            command = next(c for c in commands if c["name"] == key + "-core-elf")
            base = adopted / "commands" / command["directory"]
            out, err = regular(base / "stdout.bin"), regular(base / "stderr.bin")
            core = Elf(regular(folder / "core.o"), 1)
            for name in METHOD_NAMES:
                core.function(name)
            require(not any(x["section"] and x["name"].startswith(("ac_fixture_", "ac_model_")) for x in core.symbols),
                    "adopted standalone excludes fixture code")
            review = self.metadata_review(core, regular(folder / "core.o"), out, err, 0, "adopted", key + "-core-elf")
            if mode == "asan-ubsan":
                require(review["warning"] == spec["warning"]["classification"], "initial warning exactly matches independent review")
                warning = review
            with (folder / "core-elf.txt").open("xb") as stream:
                stream.write(out)
            save(folder / "core-metadata-review.json", review)
            self.finish_build(folder, "standalone", mode, core)
        self.results["standalone_adoption"] = {
            "specification": metadata(specraw), "source_result": metadata(raw),
            "source_status": "failed", "source_closure_files": len(closure["files"]),
            "source_input_count": len(old_inputs), "retained_commands": retained_commands,
            "metadata_warning": warning, "new_extraction_compiler_disassembler_or_runtime_commands": 0,
            "scope": "Fresh custody and metadata reconstruction only; original failed packet remains terminal"}
        require(inventory(source) == closure, "source packet unchanged after adoption")
        self.check_artifacts()

    def extract_readers(self):
        self.phase = "source-extraction"
        require("extraction" not in self.results and not (self.work / "generated").exists(),
                "new exact extraction executes once")
        raw = regular(self.inputs / "new/reader-extraction.json")
        spec = decode(raw)
        require(set(spec) == {"schema", "runtime_gate", "scope", "sources", "fragments", "preamble", "outputs"}
                and spec["schema"] == 1 and spec["runtime_gate"] == "CLOSED", "closed extraction contract")
        require(set(spec["sources"]) == {"module/attribution.inc", "generated/receipt-body.inc", "generated/receipt-types.h"},
                "exact three extraction source containers")
        for name, pin in spec["sources"].items():
            require(metadata(regular(self.inputs / "frozen" / name)) == pin, "extraction source pin")
        fragments = [
            ("module-storage", "module/attribution.inc", 16, 23),
            ("generated-storage", "generated/receipt-body.inc", 35, 40),
            ("generated-reader", "generated/receipt-body.inc", 340, 351),
            ("slot-snapshot", "module/attribution.inc", 70, 80),
            ("generated-index", "module/attribution.inc", 81, 97),
            ("module-first", "module/attribution.inc", 98, 115)]
        require(len(spec["fragments"]) == len(fragments), "six exact extraction extents")
        pieces = []
        for row, expected in zip(spec["fragments"], fragments):
            require(set(row) == {"name", "source", "first_line", "last_line", "pin"}
                    and (row["name"], row["source"], row["first_line"], row["last_line"]) == expected,
                    "reviewed extraction extent")
            lines = regular(self.inputs / "frozen" / row["source"]).splitlines(keepends=True)
            piece = b"".join(lines[row["first_line"] - 1:row["last_line"]])
            require(metadata(piece) == row["pin"], "independent exact fragment pin")
            pieces.append(piece)
        expected_outputs = {"readers.inc": spec["preamble"].encode() + b"\n".join(pieces),
                            "receipt-types.h": regular(self.inputs / "frozen/generated/receipt-types.h")}
        require({n: metadata(v) for n, v in expected_outputs.items()} == spec["outputs"],
                "independently assembled reviewed extraction")
        stdout = self.clean("extract-readers", [sys.executable, str(self.inputs / "new/extract-readers.py"),
                            "--inputs", str(self.inputs / "frozen"), "--out", str(self.work / "generated")], host=True)
        require(decode(stdout) == {"status": "extracted", "fragments": 6, "outputs": spec["outputs"]},
                "exact extraction stdout")
        expected_result = {"schema": 1, "status": "extracted", "runtime_gate": "CLOSED",
                           "contract": metadata(raw), "fragments": spec["fragments"], "outputs": spec["outputs"],
                           "compiler_or_runtime_execution": False, "consumer_or_installer_body_included": False}
        expected_outputs["extraction.json"] = encoded(expected_result)
        for name, content in expected_outputs.items():
            require(regular(self.work / "generated" / name) == content, "exact generated artifact: " + name)
        artifacts = {"generated/" + n: metadata(v) for n, v in expected_outputs.items()}
        self.results["extraction"] = {"contract": metadata(raw), "artifacts": artifacts, "fragments": 6,
                                      "compiler_or_runtime_execution": False}
        self.check_artifacts()

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
        container = "az-attempt-attribution-" + self.path.name[4:] + "-" + str(len(self.commands) + 1)
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

    def metadata_review(self, core, object_raw, stdout, stderr, returncode, origin, command):
        require(returncode == 0 and not DIAGNOSTIC.search(stdout.decode("utf-8", "replace")),
                "successful diagnostic-free readelf stdout")
        warning = core.reviewed_fini_warning(stderr, stdout) if stderr else None
        return {"schema": 1, "status": "reviewed-fini-array-warning" if warning else "clean-metadata",
                "origin": origin, "command": command, "returncode": returncode,
                "object": metadata(object_raw), "stdout": metadata(stdout), "stderr": metadata(stderr),
                "warning": warning, "compiler_or_runtime_diagnostic": False}

    def readelf_metadata(self, key, folder, argv):
        before = regular(folder / "core.o")
        rc, out, err = self.docker(key + "-core-elf", argv)
        require(regular(folder / "core.o") == before, "metadata query leaves object unchanged")
        review = self.metadata_review(Elf(before, 1), before, out, err, rc, "executed", key + "-core-elf")
        save(folder / "core-metadata-review.json", review)
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

    def adopt_codegen_review(self):
        require("codegen_review" not in self.results, "manual review adoption occurs once")
        expected_builds = {"standalone-plain", "standalone-asan-ubsan"}
        require(expected_builds.issubset(self.results["builds"]), "both standalone modes precede review")
        self.check_inputs()
        self.check_artifacts()
        raw = regular(Path(self.args.codegen_review))
        review = decode(raw)
        require(set(review) == {"schema", "status", "runtime_gate", "review_lock", "generated",
                                "standalone", "method_reviews", "guard_failure_review", "limitations"}
                and review["schema"] == 1 and review["status"] == "reviewed-before-candidate-runtime"
                and review["runtime_gate"] == "CLOSED", "closed manual source/codegen review")
        require(review["review_lock"] == metadata(regular(self.inputs / "review-lock.json"))
                and review["generated"] == self.results["extraction"]["artifacts"],
                "review bound to frozen sources and generated bytes")
        require(review["standalone"] == {name: self.results["builds"][name]["artifacts"] for name in sorted(expected_builds)},
                "review binds all artifacts of both standalone modes")
        elf = Elf(regular(self.work / "build/standalone/plain/core.o"), 1)
        require(set(review["method_reviews"]) == set(METHOD_NAMES), "complete method-bound manual review")
        for name, value in review["method_reviews"].items():
            require(type(value) is dict and set(value) == {"extent", "observations"}
                    and value["extent"] == elf.extent(name)
                    and type(value["observations"]) is list and value["observations"]
                    and all(type(x) is str and x for x in value["observations"]), "exact reviewed method extent and observations")
        require(type(review["guard_failure_review"]) is str and review["guard_failure_review"]
                and type(review["limitations"]) is list and review["limitations"]
                and all(type(x) is str and x for x in review["limitations"]),
                "explicit actual guard-failure scope and limits")
        folder = self.work / "review"
        folder.mkdir()
        with (folder / "codegen-review.json").open("xb") as stream:
            stream.write(raw)
        self.results["codegen_review"] = {"artifact": metadata(raw), "review_lock": review["review_lock"],
                                         "scope": "Manually reviewed exact artifacts; no automatic canary dependency whitelist",
                                         "runtime_gate": "CLOSED"}
        self.check_artifacts()

    def require_codegen_review(self):
        require("codegen_review" in self.results, "manual standalone source/codegen review required before candidate build or runtime")
        self.check_inputs()
        self.check_artifacts()

    def flags(self, mode, fixture=False, macro=None):
        flags = COMMON + (SAN_FLAGS if mode == "asan-ubsan" else [])
        if fixture:
            flags += ["-DAC_FIXTURE=1"]
        if macro:
            flags += ["-D" + macro + "=1"]
        return flags

    def build_variant(self, variant, mode):
        self.phase = "build-" + variant + "-" + mode
        key = variant + "-" + mode
        require(key not in self.results["builds"], "successful build replay refused")
        standalone = variant == "standalone"
        macro = CONTROL_MACROS.get(variant)
        if not standalone:
            self.require_codegen_review()
        flags = self.flags(mode, not standalone, macro)
        folder = self.work / "build" / variant / mode
        folder.mkdir(parents=True)
        target = "/out/" + folder.relative_to(self.work).as_posix()
        sources = ("core",) if standalone else ("core", "test_attempt")
        for source in sources:
            self.clean("compile-" + key + "-" + source.replace("_", "-"),
                       ["gcc", *flags, "-I/src/new", "-I/out/generated", "-c", "/src/new/" + source + ".c",
                        "-o", target + "/" + source + ".o"], empty_stdout=True)
        core = Elf(regular(folder / "core.o"), 1)
        for name in METHOD_NAMES:
            core.function(name)
        if standalone:
            require(not any(s["section"] and s["name"].startswith(("ac_fixture_", "ac_model_")) for s in core.symbols),
                    "standalone excludes fixture scheduling symbols")
        else:
            self.clean("link-" + key, ["gcc", *flags, "-no-pie", target + "/core.o",
                                      target + "/test_attempt.o", "-o", target + "/test-attempt"], empty_stdout=True)
            executable = Elf(regular(folder / "test-attempt"), 2)
            executable.function("ac_run")
            executable.function("ac_observe")
            save(folder / "linked-symbols.json", executable.summary())
        for suffix, argv in (
                ("core-disassembly", ["objdump", "-dr", target + "/core.o"]),
                ("core-elf", ["readelf", "-W", "-h", "-S", "-s", "-r", target + "/core.o"])):
            raw = (self.readelf_metadata(key, folder, argv) if suffix == "core-elf"
                   else self.clean(key + "-" + suffix, argv))
            (folder / (suffix + ".txt")).write_bytes(raw)
        if not standalone:
            raw = self.clean(key + "-linked-disassembly", ["objdump", "-d", target + "/test-attempt"])
            (folder / "linked-disassembly.txt").write_bytes(raw)
        self.finish_build(folder, variant, mode, core)

    def finish_build(self, folder, variant, mode, core):
        """Derive exact metadata from owned object bytes, without external commands."""
        key = variant + "-" + mode
        standalone = variant == "standalone"
        macro = CONTROL_MACROS.get(variant)
        flags = self.flags(mode, not standalone, macro)
        target = "/out/" + folder.relative_to(self.work).as_posix()
        save(folder / "core-symbols.json", core.summary())
        save(folder / "core-methods.json", {name: core.extent(name) for name in METHOD_NAMES})
        save(folder / "core-undefined.json", {"schema": 1, "undefined_symbols":
            [s for s in core.symbols if s["section"] == 0 and s["name"]],
            "scope": "Exact unresolved object dependencies; no transitive or signal-safety proof"})
        save(folder / "configuration.json", {
            "flags": flags, "macro": macro, "AC_FIXTURE": not standalone, "compile_only": standalone,
            "all_providers_authored_values": True, "disassembly_retained_without_lock_free_or_signal_safety_oracle": True,
            "manual_codegen_review_precedes_candidate_build_and_runtime": True,
            "stack_protection_disabling_flags": False})
        artifacts = {p.relative_to(self.work).as_posix(): metadata(regular(p)) for p in sorted(folder.iterdir())}
        self.results["builds"][key] = {
            "kind": "standalone" if standalone else "host", "variant": variant, "mode": mode,
            "compile_only": standalone, "macro": macro, "artifacts": artifacts,
            "binary": None if standalone else target + "/test-attempt"}

    def build(self):
        modes = select(self.args.modes, ("plain", "asan-ubsan"), "modes")
        variants = select(self.args.variants, ("standalone", "candidate", *CONTROL_MACROS), "build variants")
        if any(variant != "standalone" for variant in variants):
            self.require_codegen_review()
        self.inspect_toolchain()
        for mode in modes:
            for variant in variants:
                self.build_variant(variant, mode)
        self.check_inputs()
        self.check_artifacts()
        self.phase = "built-selected-variants"

    def evidence_shape(self, facts, expected):
        # Shape and storage bounds only. Semantic safe-control faults belong to
        # their exact named C oracles, not a duplicate policy in this runner.
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
        def label(value):
            return type(value) is str and re.fullmatch(r"[A-Za-z0-9-]{1,64}", value) is not None
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
                    and enum(value["event_count"], 6), "working/final scalar domains")
            owner(value["owner"])
            require(type(value["events"]) is list and len(value["events"]) == 6, "six fixed event slots")
            for event in value["events"]:
                closed(event, ("kind", "entered", "returned_recorded", "raw", "errno_valid",
                               "error_number", "requested_length", "payload_hex"), "event shape")
                require(enum(event["kind"], 5)
                        and all(boolean(event[n]) for n in ("entered", "returned_recorded", "errno_valid"))
                        and sint(event["raw"], 64) and sint(event["error_number"])
                        and uint(event["requested_length"], 64) and bytes33(event["payload_hex"]),
                        "event domains")
        def snapshot(value):
            closed(value, ("state", "has_payload", "result"), "public snapshot shape")
            require(enum(value["state"], 2) and boolean(value["has_payload"]), "public state domains")
            result(value["result"])
        def module(value):
            closed(value, ("committed", "index", "process_id", "lo", "hi", "id_hex"), "module snapshot shape")
            require(boolean(value["committed"]) and uint(value["index"])
                    and all(uint(value[n], 64) for n in ("process_id", "lo", "hi"))
                    and bytes33(value["id_hex"]), "module snapshot domains")
        def selector(value):
            closed(value, ("initialized", "phase", "status", "error", "pc", "lr", "fp", "nfp", "ra",
                           "chosen_index", "frame_count", "trace_count", "probe_count", "cell_count",
                           "snapshot_count", "address_bound", "value", "trace"), "selector shape")
            require(boolean(value["initialized"]) and enum(value["phase"], 7)
                    and enum(value["status"], 2) and enum(value["error"], 5)
                    and all(uint(value[n], 64) for n in ("pc", "lr", "fp", "nfp", "ra"))
                    and sint(value["chosen_index"]) and enum(value["frame_count"], 16)
                    and enum(value["trace_count"], 51) and enum(value["probe_count"], 18)
                    and enum(value["cell_count"], 32) and enum(value["snapshot_count"], 1)
                    and boolean(value["address_bound"]), "selector bounded scalar domains")
            owner(value["value"])
            require(type(value["trace"]) is list and len(value["trace"]) == value["trace_count"],
                    "complete populated selector trace")
            for row in value["trace"]:
                closed(row, ("operation", "address", "owner_index", "bytes_returned", "cell_value",
                             "actual_success", "consumed_success", "substituted", "snapshot"), "selection call shape")
                require(enum(row["operation"], 7) and uint(row["address"], 64) and sint(row["owner_index"])
                        and sint(row["bytes_returned"], 64) and uint(row["cell_value"], 64)
                        and all(boolean(row[n]) for n in ("actual_success", "consumed_success", "substituted")),
                        "selection call domains")
                module(row["snapshot"])
        def private(value):
            closed(value, ("phase", "next_kind", "current_event", "working", "raw_oracle", "selector",
                           "adapter_error_returned", "adapter_error"), "private serial observation shape")
            require(enum(value["phase"], 6) and enum(value["next_kind"], 5) and enum(value["current_event"], 5)
                    and boolean(value["adapter_error_returned"]) and enum(value["adapter_error"], 5),
                    "private serial state domains")
            result(value["working"])
            raw = value["raw_oracle"]
            closed(raw, ("available", "sequence", "kind", "selection", "io"), "raw oracle shape")
            require(boolean(raw["available"]) and uint(raw["sequence"]) and enum(raw["kind"], 5),
                    "raw oracle domains")
            owner(raw["selection"])
            io(raw["io"])
            selector(value["selector"])
        def auxiliary(value):
            closed(value, ("scratch_count", "diagnostic_count", "core_query_count", "violation", "scratch",
                           "diagnostics", "core_queries"), "auxiliary observation shape")
            require(enum(value["scratch_count"], 4) and enum(value["diagnostic_count"], 8)
                    and enum(value["core_query_count"], 4) and boolean(value["violation"]), "auxiliary bounds")
            for key, count in (("scratch", "scratch_count"), ("diagnostics", "diagnostic_count"),
                               ("core_queries", "core_query_count")):
                require(type(value[key]) is list and len(value[key]) == value[count], "complete populated auxiliary trace")
            for row in value["scratch"]:
                selector(row)
            for row in value["diagnostics"]:
                closed(row, ("address", "index"), "diagnostic query shape")
                require(uint(row["address"], 64) and sint(row["index"]), "diagnostic value domains")
            for row in value["core_queries"]:
                closed(row, ("address", "found", "kind"), "control-only Core query shape")
                require(uint(row["address"], 64) and boolean(row["found"]) and enum(row["kind"], 2),
                        "control-only Core query domains")

        closed(facts, set(expected) | {"source_scope", "inputs", "expected_owner", "steps", "setup",
                                      "requests", "observations"}, "closed evidence top-level schema")
        require(all(facts[k] == v for k, v in expected.items()), "raw/stdout exact agreement")
        require(facts["source_scope"] == {
            "providers": "authored-values-only", "native_fault_provider_functions_present": False,
            "signals": False, "threads": False, "evidence_io": "stdio-report-only"},
            "explicit source-only provider boundary")
        require(type(facts["inputs"]) is list and len(facts["inputs"]) == 2, "both live authored requests")
        for request in facts["inputs"]:
            closed(request, ("pc", "lr", "fp", "open", "write", "sync", "close"), "request input shape")
            require(all(uint(request[n], 64) for n in ("pc", "lr", "fp")), "request address values")
            for name in ("open", "write", "sync", "close"):
                io(request[name])
        owner(facts["expected_owner"])
        require(enum(facts["steps"], 96), "bounded authored step count")
        require(type(facts["setup"]) is list and len(facts["setup"]) <= 48, "bounded complete setup trace")
        for row in facts["setup"]:
            closed(row, ("operation", "index", "step", "process_id", "value", "lo", "hi", "other_lo",
                         "other_hi", "address", "returned_bytes", "owner_kind", "id_hex", "result"), "setup shape")
            require(row["operation"] in ("stage-module", "commit-module", "stage-generated", "commit-generated",
                                         "add-cell", "consume-snapshot-refusal")
                    and uint(row["index"]) and enum(row["step"], 96)
                    and all(uint(row[n], 64) for n in ("process_id", "value", "lo", "hi", "other_lo", "other_hi", "address"))
                    and sint(row["returned_bytes"], 64) and enum(row["owner_kind"], 2)
                    and bytes33(row["id_hex"]) and boolean(row["result"]), "setup domains")
        require(type(facts["requests"]) is list and len(facts["requests"]) <= 16, "bounded request trace")
        for row in facts["requests"]:
            closed(row, ("actor", "input", "step", "before", "after", "result"), "request observation shape")
            require(label(row["actor"]) and enum(row["input"], 1) and enum(row["step"], 96)
                    and all(enum(row[n], 2) for n in ("before", "after")) and enum(row["result"], 3),
                    "request observation domains")
        require(type(facts["observations"]) is list and 1 <= len(facts["observations"]) <= 128,
                "bounded complete serial observations")
        for row in facts["observations"]:
            closed(row, ("label", "step", "public", "private", "auxiliary"), "serial observation shape")
            require(label(row["label"]) and enum(row["step"], 96), "observation label and step")
            snapshot(row["public"])
            private(row["private"])
            auxiliary(row["auxiliary"])

    def execute(self, variant, mode, selector, failures):
        self.require_codegen_review()
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
               "stdout": metadata(raw), "stderr": metadata(err), "AC_FIXTURE": True,
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
        if self.args.adopt_standalone:
            require(not self.args.packet and self.args.phase == "prepare" and not self.args.codegen_review,
                    "standalone adoption requires a fresh prepare-only invocation")
        if self.args.packet:
            require(self.args.phase != "prepare", "preparation belongs to a fresh packet")
            self.resume()
        else:
            require(self.args.phase in ("prepare", "all"), "fresh packet must prepare first")
            self.prepare()
        if self.args.codegen_review:
            self.adopt_codegen_review()
        save(self.path / ("invocation-" + self.run_id + ".json"), {
            "schema": 1, "phase": self.args.phase, "modes": self.args.modes,
            "variants": self.args.variants, "selectors": self.args.selectors,
            "codegen_review_adopted": self.args.codegen_review is not None,
            "standalone_artifacts_adopted": self.args.adopt_standalone is not None})
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
        "extraction": r.get("extraction"), "codegen_review": r.get("codegen_review"),
        "standalone_adoption": r.get("standalone_adoption"),
        "AC_FIXTURE": True, "providers_are_authored_values": True,
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
    p.add_argument("--codegen-review")
    p.add_argument("--adopt-standalone")
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

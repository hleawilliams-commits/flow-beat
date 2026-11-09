#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fresh offline receipt successor only. Never captures Git or executes parent suites."""
import sys
if not __debug__ or sys.flags.optimize:
    raise RuntimeError("optimized Python refused before output creation")
import argparse
import difflib
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
PARENT = HERE.parent
IMAGE_ID = "sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90"
SDK_COMMIT = "3e0c5cb010454228f4ee63226b60d060494eeaae"
SDK_PACKET = PARENT / "activation-generated-range-integration-review/private/review-4d5ef55feff1444e8705e12f26d3e753/inputs/sdk"
SDK = {
    "src/azmod/inline_hooks.c": "f141b383c25e66c8acb1839280832a612a6ee0e25e9ada59f374ccbef8f81908",
    "src/common/azprot.h": "0f1cc9c5d4ca212f798bf2e9d74a69507a82ce5cc59bd328bacc46816a119b33",
}
PLAN = {
    "README.md": "ac3f45ff7dfe7f7f3b6f27e90f38ac5e85c635bee30fb32b423f86ea3f7e9654",
    "adapter-plan.json": "4a9adf88a8e278beccf63dc90920c262116287d5995c764b456faa98deb8d2be",
}
SOURCES = (".gitignore", "run-tests.py", "receipt.h", "receipt.c",
           "transform-installer.py", "fixture.S", "test_receipt.c", "test-contract.json")
# Finalized after independent source/contract review, before the first execution.
REVIEWED_NEW = {
    "receipt.h": "abde4ac8f1d35fea71541f26103d5ea1b03dd44e1e475ba5b2fb65fd330dcb4d",
    "receipt.c": "19d46afc34c05bbfaf19f6b2fc091fddb3af2b671b3fdd1373b524e07709d57a",
    "transform-installer.py": "38cece95d46b82041578efd6c95420d42db95ff5f0b16084aaf7dbb40f75aeda",
    "fixture.S": "6cc196de4e7ef941411142ba335576b571c1beec7a2ef881f78fbd29f64a40f9",
    "test_receipt.c": "768dcd7f52844584f3fea5803f567089a5f46ca446688d52cb95c6eb09da1434",
    "test-contract.json": "227473b6fe3f36987b0d405c324c4d42c05d8695b25bc626487f81f88cf1931d"
}
GENERATED = ("inline-receipt.inc", "azprot-receipt.h", "inline-receipt.patch",
             "azprot-receipt.patch", "transform-ledger.json")
COMMON = ["-std=c11", "-O1", "-g", "-fno-omit-frame-pointer",
          "-Wall", "-Wextra", "-Werror", "-fdiagnostics-color=never", "-fno-pie", "-pthread"]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
SAN_ENV = {"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1:abort_on_error=0",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
CONTROL_MACROS = {
    "original-before-metadata": "RC_CONTROL_ORIGINAL_BEFORE_METADATA",
    "literal-as-instruction": "RC_CONTROL_LITERAL_AS_INSTRUCTION",
    "erased-cleanup-custody": "RC_CONTROL_ERASE_CLEANUP_FAILURE",
    "aliased-protection-evidence": "RC_CONTROL_TOKEN_EVIDENCE_ALIAS",
    "warning-as-refusal": "RC_CONTROL_WARNING_AS_REFUSAL",
}
DIAGNOSTIC = re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|"
                        r"runtime error:|DEADLYSIGNAL|Segmentation fault|"
                        r"FIXTURE_ABORT|warning:|error:", re.I)
SAFE_NAME = re.compile(r"[a-z][a-z0-9-]{0,159}\Z")
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
MAX_REPORT = 1024 * 1024

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

def inventory(root):
    files, directories = {}, []
    if not root.exists():
        return {"files": files, "directories": directories}
    for p in sorted(root.rglob("*")):
        require(not p.is_symlink(), "symlink in retained packet")
        name = p.relative_to(root).as_posix()
        if p.is_file():
            files[name] = metadata(p.read_bytes())
        elif p.is_dir():
            directories.append(name)
        else:
            raise Refusal("nonregular retained entry: " + name)
    return {"files": files, "directories": directories}

def contract_from(raw):
    value = decode(raw)
    require(type(value) is dict and set(value) == {"selectors", "controls"}, "contract fields")
    selectors, controls = value["selectors"], value["controls"]
    require(type(selectors) is dict and selectors and type(controls) is dict
            and set(controls) == set(CONTROL_MACROS), "finalized selector/control inventory")
    for name, checks in selectors.items():
        require(SAFE_NAME.fullmatch(name) and type(checks) is list and checks
                and all(type(c) is str and SAFE_NAME.fullmatch(c) for c in checks)
                and len(checks) == len(set(checks)), "unique named selector checks")
    for name, c in controls.items():
        require(type(c) is dict and set(c) == {"macro", "selector", "expected_failures"},
                "control fields")
        require(c["macro"] == CONTROL_MACROS[name] and c["selector"] in selectors,
                "reviewed control macro/selector")
        failures = c["expected_failures"]
        require(type(failures) is list and failures and len(failures) == len(set(failures))
                and all(f in selectors[c["selector"]] for f in failures),
                "nonempty unique control failures")
    return value

def select(requested, all_names, label):
    if requested is None:
        return list(all_names)
    values = requested.split(",")
    require(values and len(values) == len(set(values))
            and all(v in all_names for v in values), "explicit known " + label)
    return values

def fields(value, names, label):
    require(type(value) is dict and set(value) == set(names.split()), label + " fields")

def integer(value, lo, hi, label):
    require(type(value) is int and lo <= value <= hi, label + " integer domain")

def bit(value, label):
    integer(value, 0, 1, label)

def hex_bytes(value, length, label):
    require(type(value) is str and len(value) == length * 2
            and re.fullmatch(r"[0-9a-f]*", value) is not None, label + " exact hex bytes")

def raw_call(value):
    fields(value, "attempted returned underlying_called injected errno_valid result error", "raw call")
    for key in ("attempted", "returned", "underlying_called", "injected", "errno_valid"):
        bit(value[key], key)
    integer(value["result"], -(1 << 63), (1 << 63) - 1, "raw result")
    integer(value["error"], -(1 << 31), (1 << 31) - 1, "saved errno")

def query(value):
    fields(value, "open reads close read_count read_capacity_refused returned parser_result", "maps query")
    raw_call(value["open"])
    raw_call(value["close"])
    integer(value["read_count"], 0, 64, "bounded read count")
    require(type(value["reads"]) is list and len(value["reads"]) == value["read_count"], "query read receipts")
    for row in value["reads"]:
        raw_call(row)
    bit(value["read_capacity_refused"], "read cap")
    bit(value["returned"], "query returned")
    integer(value["parser_result"], -1, 7, "maps protection result")

def protection(value):
    fields(value, "unlock page_size mutex_lock make_writable relock mutex_release query token_valid base length saved_bits",
           "protection")
    for key in ("unlock", "page_size", "mutex_lock", "make_writable", "relock", "mutex_release"):
        raw_call(value[key])
    query(value["query"])
    bit(value["token_valid"], "token validity")
    for key in ("base", "length"):
        integer(value[key], 0, (1 << 64) - 1, key)
    integer(value["saved_bits"], 0, 7, "saved protection")

def emission(value):
    fields(value, "owner binding ncopy out target replacement original_slot island trampoline continuation "
           "target_exclusion island_instructions island_storage trampoline_instructions trampoline_storage source_bytes",
           "emission")
    fields(value["owner"], "kind process_id id", "owner")
    integer(value["owner"]["kind"], 0, 2, "owner kind")
    integer(value["owner"]["process_id"], 0, (1 << 64) - 1, "owner process")
    require(type(value["owner"]["id"]) is str and "\0" not in value["owner"]["id"]
            and len(value["owner"]["id"].encode()) < 32, "bounded owner id")
    integer(value["binding"], 0, 3, "binding index")
    integer(value["ncopy"], 0, 1, "late-only ncopy")
    integer(value["out"], 0, 4, "bounded relocation words")
    for key in ("target", "replacement", "original_slot", "island", "trampoline", "continuation"):
        integer(value[key], 0, (1 << 64) - 1, key)
    for key in ("target_exclusion", "island_instructions", "island_storage",
                "trampoline_instructions", "trampoline_storage"):
        require(type(value[key]) is list and len(value[key]) == 2, "span pair")
        for number in value[key]:
            integer(number, 0, (1 << 64) - 1, "span bound")
    hex_bytes(value["source_bytes"], 16, "original source")

ATTEMPT_BITS = ("admitted installer_entered trampoline_prewrite_validated prepared permit_validated metadata_published original_published "
                "redirect_published original_saw_metadata redirect_saw_original island_slot_burned nhooks_accounted "
                "trampoline_cache_armed trampoline_cache_returned island_cache_armed island_cache_returned "
                "target_cache_armed target_cache_returned").split()

def attempt_value(value):
    fields(value, " ".join(ATTEMPT_BITS) + " metadata_order original_order redirect_order mapping_count pool_index "
           "pool_slot pool_base pool_length status installer_result emission mappings initial_page_size seal island_protection "
           "target_protection diagnostic_before diagnostic_after diagnostic_lost", "attempt")
    for key in ATTEMPT_BITS:
        bit(value[key], key)
    for key in ("metadata_order", "original_order", "redirect_order", "pool_index", "pool_slot",
                "diagnostic_before", "diagnostic_after", "diagnostic_lost"):
        integer(value[key], 0, (1 << 32) - 1, key)
    for key in ("pool_base", "pool_length"):
        integer(value[key], 0, (1 << 64) - 1, key)
    integer(value["status"], 0, 16, "status")
    require(type(value["installer_result"]) is int and value["installer_result"] in {-3, -1, 0, 1},
            "exact late-only/unentered installer result")
    integer(value["mapping_count"], 0, 7, "bounded mapping count")
    require(type(value["mappings"]) is list and len(value["mappings"]) == value["mapping_count"],
            "exact mapping receipts")
    for m in value["mappings"]:
        fields(m, "kind hint address length protection held accepted_pool placement_rejected cleanup_uncertain allocation cleanup",
               "mapping")
        integer(m["kind"], 0, 1, "mapping kind")
        for key in ("hint", "address", "length"):
            integer(m[key], 0, (1 << 64) - 1, key)
        integer(m["protection"], 0, 7, "mapping protection")
        for key in ("held", "accepted_pool", "placement_rejected", "cleanup_uncertain"):
            bit(m[key], key)
        raw_call(m["allocation"])
        raw_call(m["cleanup"])
    emission(value["emission"])
    raw_call(value["initial_page_size"])
    raw_call(value["seal"])
    protection(value["island_protection"])
    protection(value["target_protection"])

def check_evidence(value, expected):
    """Closed value schema, not a second native-policy or memory-model proof."""
    fields(value, "schema selector page_size bindings attempts generated checkpoints target_hits replacement_hits scheduler report",
           "raw evidence")
    require(type(value["schema"]) is int and value["schema"] == 1
            and value["selector"] == expected["selector"] and value["report"] == expected, "raw report binding")
    require(type(value["report"]) is dict
            and type(value["report"]["checks"]) is int and type(value["report"]["failures"]) is int,
            "raw report integer counts")
    integer(value["page_size"], 4096, 4096, "selected real process page size")
    require(type(value["bindings"]) is list and len(value["bindings"]) == 4, "four fixed bindings")
    for i, b in enumerate(value["bindings"]):
        fields(b, "id target replacement original_slot original_value target_protection before after", "binding")
        integer(b["id"], i, i, "binding id")
        for key in ("target", "replacement", "original_slot", "original_value"):
            integer(b[key], 0, (1 << 64) - 1, key)
        integer(b["target_protection"], -1, 7, "current target protection")
        hex_bytes(b["before"], 16, "target before")
        hex_bytes(b["after"], 16, "target after")
    require(type(value["attempts"]) is list and len(value["attempts"]) <= 16, "bounded saved attempts")
    for a in value["attempts"]:
        attempt_value(a)
    require(type(value["generated"]) is list and len(value["generated"]) == len(value["attempts"]),
            "generated diagnostics for every saved attempt")
    for i, g in enumerate(value["generated"]):
        fields(g, "attempt_index island_protection trampoline_protection island_bytes trampoline_bytes", "generated bytes")
        integer(g["attempt_index"], i, i, "generated attempt")
        for key in ("island_protection", "trampoline_protection"):
            integer(g[key], -1, 7, key)
        a = value["attempts"][i]
        have_island = a["redirect_published"] and g["island_protection"] >= 0 and g["island_protection"] & 1
        have_tramp = a["redirect_published"] and g["trampoline_protection"] >= 0 and g["trampoline_protection"] & 1
        hex_bytes(g["island_bytes"], 16 if have_island else 0, "retained island")
        hex_bytes(g["trampoline_bytes"], 4 * (a["emission"]["out"] + 4) if have_tramp else 0,
                  "retained trampoline")
    require(type(value["checkpoints"]) is list and len(value["checkpoints"]) <= 64, "bounded checkpoints")
    for c in value["checkpoints"]:
        fields(c, "point binding has_attempt attempt", "checkpoint")
        integer(c["point"], 0, 5, "checkpoint point")
        integer(c["binding"], 0, 4, "checkpoint binding")
        bit(c["has_attempt"], "checkpoint attempt presence")
        if c["has_attempt"]:
            attempt_value(c["attempt"])
        else:
            require(c["attempt"] is None, "no fabricated checkpoint attempt")
    for key in ("target_hits", "replacement_hits"):
        require(type(value[key]) is list and len(value[key]) == 4, "four hit counters")
        for count in value[key]:
            integer(count, 0, (1 << 64) - 1, "hit count")
    fields(value["scheduler"], "failed held released cm_busy writer_busy", "scheduler")
    for key, number in value["scheduler"].items():
        bit(number, key)
    require(value["scheduler"]["failed"] == 0, "no fixture scheduling failure")
    return {"saved_attempts": len(value["attempts"]), "checkpoints": len(value["checkpoints"]),
            "schema_only": True}

def function_bytes(raw, name):
    text = raw.decode("utf-8")
    # Strip comments/strings without changing positions, then find one definition.
    token = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
    clean = token.sub(lambda m: "".join("\n" if c == "\n" else " " for c in m.group()), text)
    pattern = r"^static[^\n;{}]*\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{"
    matches = list(re.finditer(pattern, clean, re.M))
    require(len(matches) == 1, "one reviewed helper definition: " + name)
    start, end, depth = matches[0].start(), matches[0].end(), 1
    while end < len(clean) and depth:
        depth += (clean[end] == "{") - (clean[end] == "}")
        end += 1
    require(depth == 0, "complete helper body")
    return text[start:end].encode()

def transform_ledger(inputs, generated, stdout):
    old = {"inline_hooks.c": regular(inputs / "sdk/src/azmod/inline_hooks.c"),
           "azprot.h": regular(inputs / "sdk/src/common/azprot.h")}
    new = {"inline_hooks.c": regular(generated / "inline-receipt.inc"),
           "azprot.h": regular(generated / "azprot-receipt.h")}
    output_names = ("inline-receipt.inc", "azprot-receipt.h", "inline-receipt.patch", "azprot-receipt.patch")
    outputs = {name: regular(generated / name) for name in output_names}
    for origin, dest, patch in (("inline_hooks.c", "inline-receipt.inc", "inline-receipt.patch"),
                                ("azprot.h", "azprot-receipt.h", "azprot-receipt.patch")):
        exact = "".join(difflib.unified_diff(old[origin].decode().splitlines(keepends=True),
                          outputs[dest].decode().splitlines(keepends=True), fromfile=origin, tofile=dest)).encode()
        require(outputs[patch] == exact, "exact complete transformed diff: " + patch)
    preserved = []
    for name in ("azm_pcrel_kind", "azm_adrp_target", "azm_emit_abs_addr", "azm_adrp_reloc_ok",
                 "azm_reloc_adrp", "azm_abs_jump", "azm_reaches", "azm_ends_fn", "azm_patch_ok",
                 "azprot_parse_line"):
        source = "azprot.h" if name.startswith("azprot_") else "inline_hooks.c"
        before, after = function_bytes(old[source], name), function_bytes(new[source], name)
        require(before == after, "byte-identical selected preserved helper: " + name)
        preserved.append(dict(name=name, **metadata(before)))
    edits = []
    for source, name in (("azprot.h", "azprot_query"), ("azprot.h", "azprot_unlock"),
                         ("azprot.h", "azprot_relock"), ("inline_hooks.c", "azm_island_slot"),
                         ("inline_hooks.c", "azm_tramp_seal"), ("inline_hooks.c", "azm_install_hook_at")):
        edits.append({"source": source, "function": name,
                      "before_sha256": digest(function_bytes(old[source], name)),
                      "after_sha256": digest(function_bytes(new[source], name))})
    expected = {"schema": 1, "version": "offline-receipts-0.1",
        "scope": "Transformed orchestration, bounded syscall receipts, late-island-only; no execution",
        "sources": {n: metadata(b) for n, b in old.items()},
        "outputs": {n: metadata(b) for n, b in outputs.items()},
        "preserved_functions": preserved, "function_edits": edits,
        "other_edits": ["azprot_query_fd read boundary", "receipt header include",
                        "seal cleanup comment", "fallback removed from selected installer"]}
    actual = regular(generated / "transform-ledger.json")
    require(actual == encoded(expected), "exact reconstructed transform ledger")
    message = {"status": "transformed", "files": 5, "ledger_sha256": digest(actual)}
    require(stdout == (json.dumps(message) + "\n").encode(), "exact transform completion")
    return expected


class Elf:
    """Direct ELF64 header/section/symbol inspection only, never execution."""
    def __init__(self, raw, kind):
        require(len(raw) >= 64 and raw[:7] == b"\x7fELF\x02\x01\x01", "ELF64 LE identity")
        h = struct.unpack_from("<16sHHIQQQIHHHHHH", raw)
        require(h[1] == kind and h[2] == 183 and h[3] == 1, "AArch64 ELF type")
        off, width, count, names_index = h[6], h[11], h[12], h[13]
        require(width == 64 and 0 < count < 65535 and names_index < count
                and off + width * count <= len(raw), "complete ordinary ELF section table")
        sections = []
        for i in range(count):
            row = struct.unpack_from("<IIQQQQIIQQ", raw, off + i * width)
            section = dict(zip(("name_offset", "type", "flags", "address", "offset", "size",
                                "link", "info", "alignment", "entry_size"), row))
            require(section["type"] == 8 or section["offset"] + section["size"] <= len(raw),
                    "section extent")
            section["data"] = b"" if section["type"] == 8 else raw[
                section["offset"]:section["offset"] + section["size"]]
            sections.append(section)
        names = sections[names_index]["data"]
        for section in sections:
            section["name"] = self.string(names, section["name_offset"])
        self.sections = sections
        self.symbols = []
        tables = 0
        for section in sections:
            if section["type"] != 2:
                continue
            tables += 1
            require(section["entry_size"] == 24 and section["size"] % 24 == 0
                    and section["link"] < count, "symbol table bounds")
            strings = sections[section["link"]]["data"]
            for at in range(0, section["size"], 24):
                name, info, other, index, address, size = struct.unpack_from("<IBBHQQ", section["data"], at)
                self.symbols.append({"name": self.string(strings, name), "bind": info >> 4,
                                     "type": info & 15, "other": other, "section": index,
                                     "value": address, "size": size})
        require(tables == 1, "one regular symbol table")
        self.kind = kind

    @staticmethod
    def string(data, offset):
        require(offset < len(data), "ELF string offset")
        end = data.find(b"\0", offset)
        require(end >= offset, "ELF string terminator")
        return data[offset:end].decode("utf-8", "strict")

    def summary(self):
        return {"machine": "AArch64", "elf_class": 64, "little_endian": True,
                "type": self.kind, "section_count": len(self.sections),
                "symbols": self.symbols}

class Run:
    def __init__(self, args):
        self.args = args
        self.path = HERE / "private" / ("run-" + uuid.uuid4().hex)
        self.inputs = self.path / "inputs"
        self.generated = self.path / "generated"
        self.work = self.path / "work"
        self.logs = self.path / "commands"
        self.manifest, self.commands, self.attempts = {}, [], []
        self.phase = "preparation"
        self.base_environment = None
        self.docker_path = None
        self.contract = None
        self.results = {"standalone": None, "candidate": {}, "controls": {}}

    def prepare(self):
        self.path.mkdir(parents=True)
        save(self.path / "started.json", {"schema": 1, "status": "preparing",
             "python": sys.version, "image_id": IMAGE_ID,
             "scope": "new authored-target receipt successor only", "runtime_gate": "CLOSED"})
        for p in (self.inputs, self.work, self.logs, self.path / "tmp"):
            p.mkdir()
        self.docker_path = str(Path(self.args.docker).resolve())
        require(Path(self.docker_path).is_file(), "Docker CLI exists")
        self.base_environment = {"PATH": "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
                                 "LANG": "C", "LC_ALL": "C", "TMPDIR": str(self.path / "tmp"),
                                 "PYTHONDONTWRITEBYTECODE": "1"}
        # Docker connection configuration only; compiler flags/library paths are never inherited.
        for key in ("HOME", "DOCKER_CONFIG", "DOCKER_CONTEXT", "DOCKER_HOST",
                    "DOCKER_TLS_VERIFY", "DOCKER_CERT_PATH"):
            if key in os.environ:
                self.base_environment[key] = os.environ[key]
        for name in SOURCES:
            self.put(name, regular(HERE / name), "new-receipt-fixture")
        for path, expected in SDK.items():
            raw = regular(SDK_PACKET / path)
            require(digest(raw) == expected, "pinned retained SDK: " + path)
            self.put("sdk/" + path, raw, "reused-sdk-" + SDK_COMMIT)
        for name, expected in PLAN.items():
            raw = regular(PARENT / "activation-installer-receipt-plan" / name)
            require(digest(raw) == expected, "frozen source-only plan: " + name)
            self.put("plan/" + name, raw, "frozen-source-plan")
        save(self.path / "input-manifest.json", self.manifest)
        require(set(REVIEWED_NEW) == set(SOURCES) - {".gitignore", "run-tests.py"},
                "reviewed source pins finalized")
        for name, expected in REVIEWED_NEW.items():
            require(self.manifest[name]["sha256"] == expected, "reviewed new source pin: " + name)
        self.contract = contract_from((self.inputs / "test-contract.json").read_bytes())
        self.check_inputs()

    def put(self, name, raw, role):
        path = self.inputs / name
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as output:
            output.write(raw)
        self.manifest[name] = dict(metadata(raw), role=role)

    def check_inputs(self):
        found = inventory(self.inputs)["files"]
        require(found == {n: {k: v[k] for k in ("bytes", "sha256")} for n, v in self.manifest.items()},
                "closed immutable source inventory")

    def command(self, name, argv, *, container=None, timeout=45):
        require(SAFE_NAME.fullmatch(name) and type(timeout) is int and 0 < timeout < 60,
                "bounded named command")
        relative = "%03d-%s" % (len(self.commands) + 1, name)
        folder = self.logs / relative
        folder.mkdir()
        spec = {"argv": [str(a) for a in argv], "cwd": str(self.path),
                "environment": self.base_environment, "timeout_seconds": timeout,
                "container": container}
        save(folder / "command.json", spec)
        proc, output, error, exception, cleanup = None, b"", b"", None, {}
        try:
            proc = subprocess.Popen(spec["argv"], cwd=self.path, env=self.base_environment,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    start_new_session=True)
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
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                            timeout=15, check=False)
                        (folder / "cleanup-stdout.bin").write_bytes(cr.stdout)
                        (folder / "cleanup-stderr.bin").write_bytes(cr.stderr)
                        cleanup.update(returncode=cr.returncode, stdout=metadata(cr.stdout),
                                       stderr=metadata(cr.stderr))
                    except BaseException as cleanup_error:
                        cleanup["container_error"] = repr(cleanup_error)
                        if isinstance(cleanup_error, subprocess.TimeoutExpired):
                            (folder / "cleanup-interrupted-stdout.bin").write_bytes(cleanup_error.output or b"")
                            (folder / "cleanup-interrupted-stderr.bin").write_bytes(cleanup_error.stderr or b"")
                if proc is not None:
                    try:
                        drained_out, drained_err = proc.communicate(timeout=5)
                        output, error = drained_out, drained_err
                    except BaseException as drain_error:
                        cleanup["drain_error"] = repr(drain_error)
        finally:
            (folder / "stdout.bin").write_bytes(output)
            (folder / "stderr.bin").write_bytes(error)
            receipt = dict(spec, name=name, directory=relative,
                           returncode=None if proc is None else proc.returncode,
                           exception=exception, cleanup=cleanup,
                           stdout=metadata(output), stderr=metadata(error))
            save(folder / "receipt.json", receipt)
            self.commands.append(dict(receipt, receipt_sha256=digest(encoded(receipt))))
        require(exception is None, "command exception: " + name + ": " + str(exception))
        return receipt["returncode"], output, error

    def docker(self, name, argv, extra_environment=None):
        container = "az-receipt-" + self.path.name[4:] + "-" + str(len(self.commands) + 1)
        env = {"PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
               "LANG": "C", "LC_ALL": "C", **(extra_environment or {})}
        command = [self.docker_path, "run", "--name", container, "--rm", "--pull=never",
                   "--network=none", "--platform=linux/arm64", "--read-only",
                   "--tmpfs", "/tmp:rw,nosuid,nodev", "--workdir", "/out",
                   "--mount", "type=bind,source=" + str(self.inputs) + ",target=/src,readonly",
                   "--mount", "type=bind,source=" + str(self.generated) + ",target=/gen,readonly",
                   "--mount", "type=bind,source=" + str(self.work) + ",target=/out",
                   IMAGE_ID, "env", "-i"]
        command += [k + "=" + v for k, v in env.items()] + list(argv)
        return self.command(name, command, container=container)

    def clean(self, name, argv, *, host=False, empty_stdout=False):
        rc, out, err = self.command(name, argv) if host else self.docker(name, argv)
        require(rc == 0 and err == b"" and not DIAGNOSTIC.search(out.decode("utf-8", "replace")),
                "clean successful command: " + name)
        if empty_stdout:
            require(out == b"", "empty compile/link stdout: " + name)
        return out

    def inspect_image(self):
        raw = self.clean("image-inspect", [self.docker_path, "image", "inspect", IMAGE_ID], host=True)
        parsed = decode(raw)
        require(type(parsed) is list and len(parsed) == 1 and type(parsed[0]) is dict,
                "one Docker image")
        info = parsed[0]
        require(info.get("Id") == IMAGE_ID and info.get("Architecture") == "arm64"
                and info.get("Os") == "linux", "pinned Linux ARM64 image")
        return {key: info[key] for key in ("Id", "Architecture", "Os")}

    def transform(self):
        self.phase = "transformation"
        raw = self.clean("transform-installer", [sys.executable, str(self.inputs / "transform-installer.py"),
                         "--inline", str(self.inputs / "sdk/src/azmod/inline_hooks.c"),
                         "--azprot", str(self.inputs / "sdk/src/common/azprot.h"),
                         "--output", str(self.generated)], host=True)
        found = inventory(self.generated)
        require(set(found["files"]) == set(GENERATED) and not found["directories"],
                "exact transformed source/diff/ledger inventory")
        ledger = transform_ledger(self.inputs, self.generated, raw)
        self.check_inputs()
        return {"files": found["files"], "ledger": ledger, "stdout": metadata(raw)}

    def inspect_elf(self, folder, object_names, executable=None, standalone=False):
        records = {}
        for name in object_names:
            raw = regular(folder / (name + ".o"))
            parsed = Elf(raw, 1)
            records[name + ".o"] = dict(metadata(raw), machine="AArch64", type=1)
            if name == "receipt":
                hooks = [s["name"] for s in parsed.symbols if s["name"].startswith("rc_fixture_")]
                require(bool(hooks) != standalone, "fixture scheduling symbols match build configuration")
                records[name + ".o"]["fixture_symbols"] = hooks
                slots = [s for s in parsed.symbols if s["name"] == "rc_originals"]
                require(len(slots) == 1 and slots[0]["size"] == 4 * 8
                        and slots[0]["value"] % 8 == 0
                        and 0 < slots[0]["section"] < len(parsed.sections)
                        and parsed.sections[slots[0]["section"]]["alignment"] >= 8,
                        "four aligned permanent plain-pointer originals")
                records[name + ".o"]["originals_symbol"] = slots[0]
        if executable is not None:
            raw = regular(folder / executable)
            parsed = Elf(raw, 2)
            records[executable] = dict(metadata(raw), machine="AArch64", type=2)
            save(folder / "elf-symbols.json", parsed.summary())
        save(folder / "elf-check.json", records)
        return records

    def build(self, variant, mode, macro=None, standalone=False):
        self.phase = "build-" + variant + "-" + mode
        folder = self.work / "build" / variant / mode
        folder.mkdir(parents=True)
        target = "/out/" + folder.relative_to(self.work).as_posix()
        flags = COMMON + ([] if standalone else ["-DRC_FIXTURE=1"])
        if mode == "asan-ubsan":
            flags += SAN_FLAGS
        if macro is not None:
            flags += ["-D" + macro + "=1"]
        self.clean("compile-" + variant + "-" + mode + "-receipt",
                   ["gcc", *flags, "-I/src", "-I/gen", "-c", "/src/receipt.c", "-o", target + "/receipt.o"],
                   empty_stdout=True)
        if standalone:
            records = self.inspect_elf(folder, ["receipt"], standalone=True)
            return {"compile_only": True, "configuration": "no-RC_FIXTURE", "elf": records}
        self.clean("compile-" + variant + "-" + mode + "-test",
                   ["gcc", *flags, "-I/src", "-I/gen", "-c", "/src/test_receipt.c",
                    "-o", target + "/test_receipt.o"], empty_stdout=True)
        self.clean("compile-" + variant + "-" + mode + "-fixture",
                   ["gcc", "-g", "-fno-pie", "-c", "/src/fixture.S", "-o", target + "/fixture.o"],
                   empty_stdout=True)
        self.clean("link-" + variant + "-" + mode,
                   ["gcc", *flags, "-no-pie", target + "/receipt.o",
                    target + "/test_receipt.o", target + "/fixture.o", "-o", target + "/test-receipt"],
                   empty_stdout=True)
        records = self.inspect_elf(folder, ["receipt", "test_receipt", "fixture"], "test-receipt")
        for label, command in (
                ("elf-metadata", ["readelf", "-W", "-h", "-l", "-s", target + "/test-receipt"]),
                ("receipt-disassembly", ["objdump", "-dr", target + "/receipt.o"]),
                ("linked-disassembly", ["objdump", "-d", target + "/test-receipt"]),
                ("fixture-disassembly", ["objdump", "-dr", target + "/fixture.o"])):
            data = self.clean(variant + "-" + mode + "-" + label, command)
            (folder / (label + ".txt")).write_bytes(data)
        save(folder / "configuration.json", {"mode": mode, "variant": variant, "flags": flags,
             "fixture_macro": True, "control_macro": macro, "sources": self.manifest,
             "generated_files": inventory(self.generated)["files"]})
        return {"binary": target + "/test-receipt", "elf": records, "control_macro": macro,
                "fixture_configuration": True}

    def execute(self, variant, mode, binary, selector, failures):
        self.phase = "execute-" + variant + "-" + mode + "-" + selector
        folder = self.work / "evidence" / variant / mode / selector
        folder.mkdir(parents=True)
        report_path = "/out/" + folder.relative_to(self.work).as_posix() + "/report.json"
        attempt = {"variant": variant, "mode": mode, "selector": selector,
                   "returned": False, "accepted": False}
        self.attempts.append(attempt)
        rc, raw, err = self.docker("run-" + variant + "-" + mode + "-" + selector,
                                  [binary, selector, report_path],
                                  SAN_ENV if mode == "asan-ubsan" else {})
        attempt.update(returned=True, returncode=rc)
        checks = self.contract["selectors"][selector]
        expected = {"selector": selector, "check_names": checks, "checks": len(checks),
                    "failures": len(failures), "failed_checks": failures}
        parsed = decode(raw)
        require(type(parsed) is dict and parsed == expected
                and raw == (json.dumps(expected, separators=(",", ":")) + "\n").encode(),
                "exact ordered named report: " + selector)
        require(rc == (1 if failures else 0) and err == b"", "exact semantic exit/no stderr")
        for key in ("checks", "failures"):
            require(type(parsed[key]) is int, "integer report counts")
        evidence = inventory(folder)
        require(set(evidence["files"]) == {"report.json"} and not evidence["directories"],
                "one bounded retained evidence file")
        raw_evidence = regular(folder / "report.json")
        require(0 < len(raw_evidence) <= MAX_REPORT, "bounded raw evidence")
        value = decode(raw_evidence)
        schema_check = check_evidence(value, expected)
        attempt["accepted"] = True
        self.check_inputs()
        return {"report": expected, "returncode": rc, "evidence": metadata(raw_evidence),
                "stderr": metadata(err), "stdout": metadata(raw), "evidence_schema": schema_check}

    def perform(self):
        self.prepare()
        selectors = select(self.args.selectors, self.contract["selectors"], "selectors")
        controls = select(self.args.control_names, self.contract["controls"], "controls")
        modes = select(self.args.modes, ("plain", "asan-ubsan"), "modes")
        sections = select(self.args.sections, ("standalone", "candidate", "controls"), "sections")
        result = {"schema": 1, "status": "running", "scope": "new offline owned installer receipts",
                  "sdk_commit": SDK_COMMIT, "runtime_gate": "CLOSED",
                  "image": self.inspect_image(), "modes": modes, "sections": sections,
                  "selectors": selectors, "control_names": controls,
                  "inputs": self.manifest, "transformation": self.transform(),
                  "common_flags": COMMON, "sanitizer_flags": SAN_FLAGS, "sanitizer_environment": SAN_ENV,
                  "candidate": self.results["candidate"], "controls": self.results["controls"],
                  "standalone": None, "old_suites_replayed": False, "sdk_git_capture": False,
                  "native_firmware_or_device_access": False}
        self.phase = "toolchain"
        version = self.clean("compiler-version", ["gcc", "--version"])
        target = self.clean("compiler-target", ["gcc", "-dumpmachine"])
        require(target.strip() == b"aarch64-linux-gnu", "selected compiler target")
        result["toolchain"] = {"version": metadata(version), "target": target.decode().strip()}
        if "standalone" in sections:
            result["standalone"] = self.build("standalone", "plain", standalone=True)
            self.results["standalone"] = result["standalone"]
        for mode in modes:
            if "candidate" in sections:
                built = self.build("candidate", mode)
                result["candidate"][mode] = {}
                for selector in selectors:
                    result["candidate"][mode][selector] = self.execute(
                        "candidate", mode, built["binary"], selector, [])
            if "controls" in sections:
                result["controls"][mode] = {}
                for name in controls:
                    c = self.contract["controls"][name]
                    built = self.build(name, mode, c["macro"])
                    result["controls"][mode][name] = self.execute(
                        name, mode, built["binary"], c["selector"], c["expected_failures"])
        self.check_inputs()
        result["status"] = "passed"
        return result

def sanitized_result(result, private_digest):
    """Allowlist only: no argv, host paths, source text, addresses or raw evidence."""
    projected = {
        "schema": 1, "status": result["status"], "scope": "new offline authored-target installer receipt fixture",
        "runtime_gate": "CLOSED", "private_result_sha256": private_digest,
        "inputs": result.get("inputs", {}),
        "modes": result.get("modes", []), "sections": result.get("sections", []),
        "selectors": result.get("selectors", []), "control_names": result.get("control_names", []),
        "candidate": result.get("candidate", result.get("partial_results", {}).get("candidate", {})),
        "controls": result.get("controls", result.get("partial_results", {}).get("controls", {})),
        "commands": [{"name": c["name"], "returncode": c["returncode"],
                      "exception_present": c["exception"] is not None,
                      "stdout": c["stdout"], "stderr": c["stderr"],
                      "receipt_sha256": c["receipt_sha256"]} for c in result.get("commands", [])],
        "execution_attempts": result.get("execution_attempts", []),
        "retained_files_including_private_result": result.get("retained_files_including_result"),
        "output_streams": 2 * len(result.get("commands", [])),
        "standalone_configuration_executed": False, "all_executed_configurations_use_RC_FIXTURE": True,
        "old_suites_replayed": False, "old_sanitizer_probe_replayed": False,
        "sdk_git_capture": False, "native_firmware_or_device_access": False,
        "signal_or_atomic_codegen_proof": False,
    }
    if "image" in result:
        projected["image"] = result["image"]
    if "toolchain" in result:
        projected["toolchain"] = result["toolchain"]
    if "transformation" in result:
        projected["transformed_files"] = result["transformation"]["files"]
    if result.get("standalone") is not None:
        projected["standalone"] = {"compiled": True, "executed": False,
                                    "configuration": "no-RC_FIXTURE"}
    return projected

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--docker", default=shutil.which("docker") or "/opt/homebrew/bin/docker")
    ap.add_argument("--modes", default=None)
    ap.add_argument("--sections", default=None)
    ap.add_argument("--selectors", default=None)
    ap.add_argument("--control-names", default=None)
    args = ap.parse_args()
    run, result, status = Run(args), None, 1
    try:
        result = run.perform()
        status = 0
    except BaseException as exc:
        result = {"schema": 1, "status": "failed", "phase": run.phase,
                  "error": type(exc).__name__ + ": " + str(exc), "traceback": traceback.format_exc(),
                  "inputs": run.manifest, "partial_results": run.results, "runtime_gate": "CLOSED"}
    finally:
        # Preserve all source, transformed, build/debug companions, raw outputs and
        # incomplete artifacts even when preparation or a command fails.
        if run.path.exists():
            result["commands"] = run.commands
            result["execution_attempts"] = run.attempts
            try:
                result["retained_inventory_before_result"] = inventory(run.path)
                result["retained_files_including_result"] = len(result["retained_inventory_before_result"]["files"]) + 1
            except BaseException as exc:
                result["inventory_error"] = repr(exc)
                result["status"] = "failed"
                status = 1
            save(run.path / "result.json", result)
            result_hash = digest(encoded(result))
            projection = sanitized_result(result, result_hash)
            save(run.path / "sanitized-result.json", projection)
            print(json.dumps({"status": result["status"], "run": run.path.name,
                              "result_sha256": result_hash,
                              "sanitized_result_sha256": digest(encoded(projection))}, sort_keys=True))
        else:
            print("preparation failed before output directory creation", file=sys.stderr)
    return status

if __name__ == "__main__":
    sys.exit(main())

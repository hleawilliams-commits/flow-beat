#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fresh offline claim/receipt composition only; no SDK capture or parent suite execution."""
import sys
if not __debug__ or sys.flags.optimize:
    raise RuntimeError("optimized Python refused before output creation")
import argparse
import ast
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
FROZEN_PACKET = PARENT / "activation-claim-receipt-plan/private/scope-5e93316acebc44a4852516c4413b9c99/inputs"
FROZEN = {
    "community.c": ("sdk/community.c", "4b2b72e2b623dcb1ecb0ef108a473815adab45a516a6aa13adb6cce512b80866"),
    "receipt.c": ("receipt/receipt.c", "19d46afc34c05bbfaf19f6b2fc091fddb3af2b671b3fdd1373b524e07709d57a"),
    "receipt.h": ("receipt/receipt.h", "abde4ac8f1d35fea71541f26103d5ea1b03dd44e1e475ba5b2fb65fd330dcb4d"),
    "fixture.S": ("receipt/fixture.S", "6cc196de4e7ef941411142ba335576b571c1beec7a2ef881f78fbd29f64a40f9"),
    "inline-receipt.inc": ("generated/inline-receipt.inc", "03f927df4da1d9c3ed56bdf00b20893a68195e65042abcc113258e723f1398cc"),
    "azprot-receipt.h": ("generated/azprot-receipt.h", "28644f70e972dfa28ecd3b940e9f4d132492ecd22f73e48d561c8684ddb9506a"),
}
PLAN = {
    "README.md": "636f85ea1b0ecd48e43ccb6361a06d868db651503de326aa2ec8c33b5297f940",
    "composition-plan.json": "a3c2a65d68232f7f307d988efede0ac3e60de3e8e2cd9b609bd1fa50e189ed59",
}
SERIALIZER_INPUT = PARENT / "activation-installer-receipt-fixture/test_receipt.c"
SERIALIZER_SHA = "768dcd7f52844584f3fea5803f567089a5f46ca446688d52cb95c6eb09da1434"
SOURCES = ("run-tests.py", "compose-source.py", "composition.h", "composition.c",
           "test_composition.c", "test-contract.json")
# Fail closed until the final source and contract have been reviewed.
REVIEWED_NEW = {
    "compose-source.py": "3fdd8eec4ace764fe85cf34e57bd4ca398c3a39a26e8bfe5b3827bb32a4055b4",
    "composition.h": "a445905a2628c8b42ce6911b0bd27ef9e684cb1151c8b538a15123765f552c80",
    "composition.c": "e8a7228d1e9afa492f16e07f681daa82bf56da4ddd3f2119f17e578824be0ea8",
    "test_composition.c": "210e8ea731b5071103ae1088ab803b76d69828bf04291d47dc3f43a7cc74e5ab",
    "test-contract.json": "a7767901a99c7b9c6339ebf881bab8f99f114d8f69544047455a2e5eb2c89677"
}
GENERATED = ("receipt-types.h", "receipt-body.inc", "pinned-community.inc",
             "inline-receipt.inc", "azprot-receipt.h", "receipt-types.diff",
             "receipt-body.diff", "pinned-community.diff", "composition-ledger.json")
COMMON = ["-std=c11", "-O1", "-g", "-fno-omit-frame-pointer",
          "-Wall", "-Wextra", "-Werror", "-fdiagnostics-color=never", "-fno-pie", "-pthread"]
SAN_FLAGS = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
SAN_ENV = {"ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1:abort_on_error=0",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
CONTROL_MACROS = {
    "adopt-preopen-session": "CC_CONTROL_ADOPT_PREOPEN_SESSION",
    "omit-account": "CC_CONTROL_OMIT_ACCOUNT",
    "warning-sdk-refusal": "CC_CONTROL_WARNING_SDK_REFUSAL",
    "failed-init-zero-count": "CC_CONTROL_FAILED_INIT_ZERO_COUNT",
    "ignore-hooks": "CC_CONTROL_IGNORE_HOOKS",
    "truncate-id32": "CC_CONTROL_TRUNCATE_ID32",
}
DIAGNOSTIC = re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|"
                        r"runtime error:|DEADLYSIGNAL|Segmentation fault|"
                        r"FIXTURE_ABORT|warning:|error:", re.I)
SAFE_NAME = re.compile(r"[a-z][a-z0-9-]{0,159}\Z")
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
MAX_REPORT = 8 * 1024 * 1024

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
    require(len(selectors) == 11 and sum(len(x) for x in selectors.values()) == 64,
            "reviewed eleven selectors and sixty-four summed checks")
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
            and len(value["owner"]["id"].encode()) < 33, "bounded owner id")
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

def bounded_text(value, limit, label):
    require(type(value) is str and "\0" not in value and len(value.encode("utf-8")) <= limit,
            label + " bounded text")

def module_value(value):
    bits = ("present range_published handle_retained session_used session_open entry_attempted "
            "entry_returned descriptor_accepted init_attempted init_returned callbacks_retained "
            "hooks_ok firmware_any").split()
    fields(value, " ".join(bits) + " id process_id lo hi index nhooks init_result phase load_status", "module")
    for name in bits:
        bit(value[name], name)
    bounded_text(value["id"], 32, "module ID")
    for name in ("process_id", "lo", "hi"):
        integer(value[name], 0, (1 << 64) - 1, name)
    integer(value["index"], 0, 3, "module index")
    integer(value["nhooks"], 0, 4, "bounded count")
    integer(value["init_result"], -(1 << 31), (1 << 31) - 1, "init result")
    integer(value["phase"], 0, 5, "module phase")
    integer(value["load_status"], 0, 17, "module status")

def claim_value(value):
    fields(value, "status sdk_result captured_session resolved_session module_index nhooks_before "
           "nhooks_after installer_called accounted incomplete receipt", "claim")
    integer(value["status"], 0, 17, "claim status")
    require(type(value["sdk_result"]) is int and value["sdk_result"] in {-1, 0}, "SDK result domain")
    integer(value["captured_session"], 0, (1 << 32) - 1, "captured unsigned handle")
    for name in ("resolved_session", "module_index", "nhooks_before", "nhooks_after"):
        integer(value[name], 0, 4, name)
    for name in ("installer_called", "accounted", "incomplete"):
        bit(value[name], name)
    attempt_value(value["receipt"])

def elf_symbol(elf, name):
    matches = [v for v in elf.symbols if v["name"] == name
               and v["bind"] == 1 and 0 < v["section"] < len(elf.sections)]
    require(len(matches) == 1, "one linked owned symbol: " + name)
    return matches[0]

def elf_bytes(elf, address, size):
    matches = [s for s in elf.sections if s["type"] == 1 and s["flags"] & 2
               and s["address"] <= address and address + size <= s["address"] + s["size"]]
    require(len(matches) == 1, "one complete linked PROGBITS extent")
    s = matches[0]
    return s["data"][address - s["address"]:address - s["address"] + size]

def check_evidence(value, expected, binary):
    """Complete bounded schemas and linked fixed identities, not a second policy proof."""
    fields(value, "schema selector page_size authored_inputs bindings claims core_attempts module_history runs "
           "final_modules receipts checkpoints scheduler entries inits target_hits replacement_hits "
           "callback_probes active_session report", "raw evidence")
    integer(value["schema"], 1, 1, "schema")
    require(value["selector"] == expected["selector"] and value["report"] == expected, "raw report binding")
    require(type(value["report"]["checks"]) is int and type(value["report"]["failures"]) is int,
            "raw report integer counts")
    integer(value["page_size"], 4096, 4096, "selected real page size")
    def array(name, limit, exact=None):
        a = value[name]
        require(type(a) is list and len(a) <= limit and (exact is None or len(a) == exact),
                name + " bounded array")
        return a
    inputs = array("authored_inputs", 4)
    require(inputs, "authored module inputs")
    for x in inputs:
        fields(x, "id process_id handle_present link_query_succeeded link_map_present object_matched "
               "base_valid base load_count hooks firmware_any loads", "authored loader input")
        bounded_text(x["id"], 32, "authored ID")
        for key in ("process_id", "base"):
            integer(x[key], 0, (1 << 64) - 1, key)
        for key in ("handle_present", "link_query_succeeded", "link_map_present", "object_matched",
                    "base_valid", "hooks", "firmware_any"):
            bit(x[key], key)
        integer(x["load_count"], 0, (1 << 32) - 1, "authored load count")
        require(type(x["loads"]) is list and len(x["loads"]) == min(x["load_count"], 4),
                "bounded authored PT_LOAD descriptions")
        for load in x["loads"]:
            fields(load, "load virtual_address memory_size", "load")
            bit(load["load"], "PT_LOAD selection")
            for key in ("virtual_address", "memory_size"):
                integer(load[key], 0, (1 << 64) - 1, key)
    originals = original_symbol(binary)["value"]
    for i, b in enumerate(array("bindings", 4, 4)):
        fields(b, "id target replacement original_slot original_value target_protection before after", "binding")
        integer(b["id"], i, i, "binding id")
        for key in ("target", "replacement", "original_slot", "original_value"):
            integer(b[key], 0, (1 << 64) - 1, key)
        integer(b["target_protection"], -1, 7, "current target protection")
        target = elf_symbol(binary, "rc_target" + str((0, 1, 2, 0)[i]))["value"]
        replacement = elf_symbol(binary, "rc_replacement" + str(i))["value"]
        require((b["target"], b["replacement"], b["original_slot"]) ==
                (target, replacement, originals + i * 8), "linked fixed descriptor identity")
        hex_bytes(b["before"], 16, "original target bytes")
        require(bytes.fromhex(b["before"]) == elf_bytes(binary, target, 16),
                "captured original bytes match linked target")
        readable = b["target_protection"] >= 0 and b["target_protection"] & 1
        hex_bytes(b["after"], 16 if readable else 0, "current target bytes")
    for c in array("claims", 64):
        fields(c, "label input result", "saved claim")
        bounded_text(c["label"], 160, "claim label")
        q = c["input"]
        fields(q, "id binding target replacement original caller_ra captured_session faults recursive", "claim input")
        bounded_text(q["id"], 32, "claim ID")
        integer(q["binding"], 0, 3, "authored binding")
        integer(q["captured_session"], 0, (1 << 32) - 1, "authored captured session")
        for key in ("target", "replacement", "original", "caller_ra", "faults"):
            integer(q[key], 0, (1 << 64) - 1, key)
        bit(q["recursive"], "recursive fixture")
        claim_value(c["result"])
    for a in array("core_attempts", 4):
        attempt_value(a)
    for item in array("module_history", 64):
        fields(item, "label module", "module history")
        bounded_text(item["label"], 160, "module label")
        module_value(item["module"])
    for item in array("runs", 32):
        fields(item, "label result index module", "module run")
        bounded_text(item["label"], 160, "run label")
        integer(item["result"], 0, 17, "run status")
        integer(item["index"], 0, 3, "run index")
        module_value(item["module"])
    for m in array("final_modules", 4, len(inputs)):
        module_value(m)
    retained_attempts = 0
    for i, r in enumerate(array("receipts", 4, 4)):
        fields(r, "binding present attempt island_protection trampoline_protection island_bytes trampoline_bytes",
               "retained receipt")
        integer(r["binding"], i, i, "retained binding")
        bit(r["present"], "receipt presence")
        for name in ("island_protection", "trampoline_protection"):
            integer(r[name], -1, 7, name)
        if r["present"]:
            retained_attempts += 1
            attempt_value(r["attempt"])
        else:
            require(r["attempt"] is None, "no fabricated retained receipt")
        a = r["attempt"]
        ip, tp = r["island_protection"], r["trampoline_protection"]
        have_island = a is not None and a["redirect_published"] and ip >= 0 and ip & 1
        have_tramp = a is not None and a["redirect_published"] and tp >= 0 and tp & 1
        hex_bytes(r["island_bytes"], 16 if have_island else 0, "copied island")
        hex_bytes(r["trampoline_bytes"], 4 * (a["emission"]["out"] + 4) if have_tramp else 0,
                  "copied trampoline")
    for c in array("checkpoints", 128):
        fields(c, "domain point index has_result has_attempt result attempt", "checkpoint")
        integer(c["domain"], 0, 1, "checkpoint domain")
        integer(c["point"], 0, 3 if c["domain"] == 0 else 5, "checkpoint enum")
        integer(c["index"], 0, 4, "checkpoint index")
        for name, parser in (("result", claim_value), ("attempt", attempt_value)):
            bit(c["has_" + name], name + " present")
            if c["has_" + name]:
                parser(c[name])
            else:
                require(c[name] is None, "no fabricated checkpoint " + name)
    scheduler = value["scheduler"]
    fields(scheduler, "held released failed cm_busy writer_busy holds releases", "scheduler")
    for key in ("held", "released", "failed", "cm_busy", "writer_busy"):
        bit(scheduler[key], key)
    for key in ("holds", "releases"):
        integer(scheduler[key], 0, (1 << 32) - 1, key)
    require(scheduler["failed"] == 0, "no fixture scheduling failure")
    for key in ("entries", "inits", "target_hits", "replacement_hits"):
        for number in array(key, 4, 4):
            integer(number, 0, (1 << 64) - 1, key)
    require(value["target_hits"] == value["replacement_hits"] == [0, 0, 0, 0],
            "no patched target or replacement invocation")
    for p in array("callback_probes", 2, 2):
        fields(p, "index started done observed_in_callback got module", "callback probe")
        integer(p["index"], 0, 3, "probe index")
        for key in ("started", "done", "observed_in_callback", "got"):
            bit(p[key], key)
        module_value(p["module"])
    integer(value["active_session"], 0, 4, "final active handle")
    return {"authored_modules": len(inputs), "claims": len(value["claims"]),
            "core_attempts": len(value["core_attempts"]), "retained_attempts": retained_attempts,
            "module_snapshots": len(value["module_history"]), "runs": len(value["runs"]),
            "checkpoints": len(value["checkpoints"]), "closed_schema": True,
            "linked_original_and_target_identities": True, "native_policy_proof": False}

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

def composition_ledger(inputs, generated, stdout):
    """Reconstruct exact copies, complete edits and ledger without importing composer."""
    old = {n: regular(inputs / "frozen" / n) for n in
           ("community.c", "receipt.c", "receipt.h", "inline-receipt.inc", "azprot-receipt.h")}
    authored = {n: regular(inputs / n) for n in ("composition.c", "composition.h")}
    outputs = {n: regular(generated / n) for n in GENERATED if n != "composition-ledger.json"}
    def once(text, before, after):
        require(text.count(before) == 1, "one independent composition anchor")
        return text.replace(before, after, 1)
    def remove_between(text, first, last):
        require(text.count(first) == text.count(last) == 1, "unique independent cut boundaries")
        begin, end = text.index(first), text.index(last)
        require(begin < end, "ordered independent cut")
        return text[:begin] + text[end:]
    header = old["receipt.h"].decode()
    header = once(header, "#ifndef RECEIPT_H\n#define RECEIPT_H",
                  "#ifndef CLAIM_RECEIPT_TYPES_H\n#define CLAIM_RECEIPT_TYPES_H")
    start, end = header.index("/* Offline authored-target fixture only."), header.index("#define RC_BINDINGS")
    header = header[:start] + ("/* Copied receipt values for the separately versioned composition.\n"
        " * Recursive SDK cm/common writer lifecycle is defined in composition.h.\n"
        " * Diagnostic addresses are capabilities, not alias isolation. */\n") + header[end:]
    header = once(header, "#define RC_ID_BYTES 32u", "#define RC_ID_BYTES 33u")
    header = once(header, """typedef struct {
    uint64_t process_id;
    char id[RC_ID_BYTES];
    uintptr_t lo, hi; /* Authored immutable envelope, not discovered ELF proof. */
} rc_module;
typedef struct { uint64_t generation; } rc_epoch;
""", "")
    header = remove_between(header, "rc_status rc_initialize(void);\n", "/* Copies mutable receipts under writer")
    body = once(old["receipt.c"].decode(), '#include "receipt.h"', '#include "composition.h"')
    body = once(body, """static pthread_mutex_t cm = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t writer = PTHREAD_MUTEX_INITIALIZER;
static bool initialized, initialize_attempted, epoch_created, epoch_live;
static rc_module module_value;
static uint64_t module_hooks;
""", """static pthread_mutex_t writer = PTHREAD_MUTEX_INITIALIZER;
static bool initialized, initialize_attempted;
""")
    for before, after in (
        ("if (epoch_created && overlap(blob, (rc_span){module_value.lo, module_value.hi}))",
         "if (cc_storage_overlaps_modules(blob))"),
        ("if (epoch_created && overlap(blobs[k], (rc_span){module_value.lo, module_value.hi}))",
         "if (cc_storage_overlaps_modules(blobs[k]))"),
        ("""    rc_active->prepared = true;
    records[active_binding].immutable = *m; /* Still invisible to lookup. */
""", """    if (!cc_metadata_owner_valid(m)) return false;
#ifdef CC_CONTROL_TRUNCATE_ID32
    m->owner.id[31] = '\\0'; /* Safe fixture-only identity evidence corruption. */
#endif
    rc_active->prepared = true;
    records[active_binding].immutable = *m; /* Still invisible to lookup. */
"""),
        ("return rc_active->prepared && m->binding == active_binding &&",
         "return rc_active->prepared && cc_permit_matches(m) && m->binding == active_binding &&"),
        ("""/* Exact pinned inputs are transformed offline by transform-installer.py.
 * Full diffs/ledger distinguish instrumentation from byte-identical helpers. */""",
         """/* Already retained installer/protection bytes are copied unchanged by
 * compose-source.py; no earlier generator or suite is replayed. */""")):
        body = once(body, before, after)
    body = remove_between(body, "rc_status rc_initialize(void) {\n",
                          "bool rc_snapshot(unsigned binding, rc_attempt *out) {\n")
    preserved, pieces = [], []
    lines = old["community.c"].splitlines(keepends=True)
    for name, first, last in (("cm_id_ok", 461, 470), ("cm_find", 471, 476), ("cm_called_by", 1100, 1102)):
        chunk = b"".join(lines[first - 1:last])
        require(chunk == function_bytes(old["community.c"], name) + b"\n", "complete exact SDK helper")
        preserved.append(dict(name=name, source="community.c", first_line=first,
                              last_line=last, **metadata(chunk)))
        pieces.append(chunk)
    helpers = b"/* Exact retained helper bodies; supplied bounded cm_mod/g_cm context. */\n" + b"\n".join(pieces)
    expected_outputs = {"receipt-types.h": header.encode(), "receipt-body.inc": body.encode(),
                        "pinned-community.inc": helpers,
                        "inline-receipt.inc": old["inline-receipt.inc"],
                        "azprot-receipt.h": old["azprot-receipt.h"]}
    for origin, dest, patch in (("receipt.h", "receipt-types.h", "receipt-types.diff"),
                                ("receipt.c", "receipt-body.inc", "receipt-body.diff"),
                                ("community.c", "pinned-community.inc", "pinned-community.diff")):
        expected_outputs[patch] = "".join(difflib.unified_diff(
            old[origin].decode().splitlines(keepends=True),
            expected_outputs[dest].decode().splitlines(keepends=True),
            fromfile=origin, tofile=dest)).encode()
    require(outputs == expected_outputs, "exact independently reconstructed composed source and full diffs")
    transformations = []
    reviewed_authored = [["A1","community.c",1226,1273,["cc_claim_hook","cc_claim_worker"],"Explicitly transformed claim: outer noinline RA/session capture, predicates under recursive cm->writer, combined preopen/stale veto, exact owned binding and bounded capacity."],["A2","community.c",1092,1102,["cc_lock","cc_both","cc_release_both"],"Selected recursive SDK cm and common writer; no writer->cm edge or lock across authored entry/init."],["A3","community.c",1804,1845,["cc_loader_begin","cc_build_range","cc_run_module"],"Authored explicit loader provenance/capability values replace real loader/parser/descriptor work; immutable per-index range facts precede descriptor callback."],["A4","community.c",1846,1859,["cc_run_module","cc_close_locked","cc_capture_session"],"One-use immutable sessions release-publish index+1/open and0/close, one entry acquire; close/count under shared locks, callback outside; failed init preserves receipts/count."],["A5","community.c",1259,1273,["cc_claim_worker"],"Normally returned actual redirect accounts exactly once under locks; warnings map SDK0; raw installer/receipt values stay distinct; inconsistent publication is terminal/incomplete."],["A6","community.c",1876,1894,["cc_loader_begin","cc_run_module"],"Added explicit one-start controller and nonblocking repeated/in-progress refusal; sequential distinct fixed module sessions allowed."],["A7","receipt.c",386,461,["cc_install_locked","cc_install_core"],"Explicit replacement of public fixture admission: remove duplicate cm/epoch/count and replacement-in-claimant proxy; retain private writer-held actual receipt installer/cut."]]
    for identity, source, first, last, names, description in reviewed_authored:
        chunk = b"".join(old[source].splitlines(keepends=True)[first - 1:last])
        transformations.append(dict(id=identity, kind="explicit-authored-replacement",
            source=source, first_line=first, last_line=last, **metadata(chunk),
            destination="composition.c", functions=names, description=description))
    expected = {
        "schema": 1, "scope": "Offline claim/receipt source composition; not SDK integration or live authorization",
        "runtime_gate": "CLOSED", "child_commands": [], "old_generator_replayed": False,
        "inputs": {n: metadata(v) for n, v in {**old, **authored}.items()},
        "outputs": {n: metadata(v) for n, v in expected_outputs.items()},
        "preserved": preserved, "authored_transformations": transformations,
        "changes": [{"id":"C1","input":"receipt.h","output":"receipt-types.h","description":"Widen owned ID to33; remove independent module/epoch and obsolete admission declarations; retain receipt value layouts and fixed symbols."},{"id":"C2","input":"receipt.c","output":"receipt-body.inc","description":"Remove second cm/epoch/count and public installer/lifecycle; retain common writer, syscall custody, fixed bindings, publication and diagnostics."},{"id":"C3","input":"receipt.c","output":"receipt-body.inc","description":"Use all committed authored module envelopes for prewrite/final storage exclusion, private immutable permit checks, and one safe fixture-only ID-copy control."},{"id":"C4","input":"community.c","output":"pinned-community.inc","description":"Exact three helper bodies only; claim/lifecycle orchestration is authored explicitly in composition.c."},{"id":"C5","input":"inline-receipt.inc","output":"inline-receipt.inc","description":"Exact retained complete generated installer copy, including unchanged no-refusal publication cut."},{"id":"C6","input":"azprot-receipt.h","output":"azprot-receipt.h","description":"Exact retained complete generated protection copy."}]
    }
    actual = regular(generated / "composition-ledger.json")
    require(actual == encoded(expected), "exact independently reconstructed composition ledger")
    message = {"status": "composed-source-only", "outputs": 9, "ledger_sha256": digest(actual)}
    require(stdout == (json.dumps(message) + "\n").encode(), "exact composer completion")
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

def original_symbol(elf):
    rows = [s for s in elf.symbols if s["name"] == "rc_originals"]
    require(len(rows) == 1, "one permanent original array symbol")
    slot = rows[0]
    require(slot["bind"] == 1 and slot["type"] == 1 and slot["size"] == 32
            and slot["value"] % 8 == 0 and 0 < slot["section"] < len(elf.sections),
            "strong defined aligned four-pointer original array")
    section = elf.sections[slot["section"]]
    require(section["type"] == 8 and section["alignment"] >= 8
            and section["flags"] & 3 == 3 and section["address"] <= slot["value"]
            and slot["value"] + 32 <= section["address"] + section["size"],
            "originals retained in allocated writable BSS, not COMMON")
    return slot

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
             "scope": "new authored-input SDK claim and owned receipt composition", "runtime_gate": "CLOSED"})
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
            self.put(name, regular(HERE / name), "new-claim-receipt-fixture")
        for name, (relative, expected) in FROZEN.items():
            raw = regular(FROZEN_PACKET / relative)
            require(digest(raw) == expected, "pinned retained input: " + name)
            self.put("frozen/" + name, raw, "reused-source-only-parent")
        raw = regular(SERIALIZER_INPUT)
        require(digest(raw) == SERIALIZER_SHA, "frozen serializer reference")
        self.put("frozen/test_receipt.c", raw, "reference-only-never-compiled")
        for name, expected in PLAN.items():
            raw = regular(PARENT / "activation-claim-receipt-plan" / name)
            require(digest(raw) == expected, "frozen source-only plan: " + name)
            self.put("plan/" + name, raw, "frozen-source-plan")
        save(self.path / "input-manifest.json", self.manifest)
        require(set(REVIEWED_NEW) == set(SOURCES) - {"run-tests.py"},
                "reviewed source pins finalized")
        for name, expected in REVIEWED_NEW.items():
            require(self.manifest[name]["sha256"] == expected, "reviewed new source pin: " + name)
        for name in ("run-tests.py", "compose-source.py"):
            ast.parse(regular(self.inputs / name), filename=name)
        self.contract = contract_from((self.inputs / "test-contract.json").read_bytes())
        old_serializer = regular(self.inputs / "frozen/test_receipt.c")
        new_serializer = regular(self.inputs / "test_composition.c")
        serializer_pins = {}
        for name in ("js", "hex", "raw_json", "span_json", "metadata_json",
                     "query_json", "protection_json", "attempt_json"):
            before, after = function_bytes(old_serializer, name), function_bytes(new_serializer, name)
            require(before == after, "byte-identical retained serializer body: " + name)
            serializer_pins[name] = metadata(before)
        save(self.path / "source-oracles.json", {"schema": 1,
             "reference_selectors_compiled_or_executed": False,
             "serializer_function_bodies": serializer_pins,
             "python_sources_syntax_parsed": ["run-tests.py", "compose-source.py"],
             "contract_selectors": 11, "contract_summed_checks": 64, "contract_controls": 6})
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
        container = "az-claim-receipt-" + self.path.name[4:] + "-" + str(len(self.commands) + 1)
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

    def compose(self):
        self.phase = "composition"
        raw = self.clean("compose-source", [sys.executable, str(self.inputs / "compose-source.py"),
                         "--sdk-community", str(self.inputs / "frozen/community.c"),
                         "--receipt-c", str(self.inputs / "frozen/receipt.c"),
                         "--receipt-h", str(self.inputs / "frozen/receipt.h"),
                         "--inline", str(self.inputs / "frozen/inline-receipt.inc"),
                         "--protection", str(self.inputs / "frozen/azprot-receipt.h"),
                         "--composition-c", str(self.inputs / "composition.c"),
                         "--composition-h", str(self.inputs / "composition.h"),
                         "--output", str(self.generated)], host=True)
        found = inventory(self.generated)
        require(GENERATED and set(found["files"]) == set(GENERATED) and not found["directories"],
                "exact composed source/diff/ledger inventory")
        ledger = composition_ledger(self.inputs, self.generated, raw)
        self.check_inputs()
        return {"files": found["files"], "ledger": ledger, "stdout": metadata(raw)}

    def inspect_elf(self, folder, object_names, executable=None, standalone=False):
        records = {}
        for name in object_names:
            raw = regular(folder / (name + ".o"))
            parsed = Elf(raw, 1)
            records[name + ".o"] = dict(metadata(raw), machine="AArch64", type=1)
            if name == "composition":
                entry = [x for x in parsed.symbols if x["name"] == "cc_claim_hook"
                         and x["bind"] == 1 and x["type"] == 2
                         and 0 < x["section"] < len(parsed.sections)]
                require(len(entry) == 1, "actual SDK-signature entry compiled")
                authored = [x for x in parsed.symbols if x["name"] == "cc_claim_authored"
                            and x["bind"] == 1 and x["type"] == 2
                            and 0 < x["section"] < len(parsed.sections)]
                require(len(authored) == (0 if standalone else 1),
                        "authored RA entry only in declared fixture configuration")
                hooks = [x["name"] for x in parsed.symbols
                         if x["name"].startswith(("rc_fixture_", "cc_fixture_"))]
                require(bool(hooks) != standalone, "fixture symbols match build configuration")
                records[name + ".o"]["fixture_symbols"] = hooks
                records[name + ".o"]["originals_symbol"] = original_symbol(parsed)
        if executable is not None:
            raw = regular(folder / executable)
            parsed = Elf(raw, 2)
            records[executable] = dict(metadata(raw), machine="AArch64", type=2,
                                      originals_symbol=original_symbol(parsed))
            save(folder / "elf-symbols.json", parsed.summary())
        save(folder / "elf-check.json", records)
        return records

    def build(self, variant, mode, macro=None, standalone=False):
        self.phase = "build-" + variant + "-" + mode
        folder = self.work / "build" / variant / mode
        folder.mkdir(parents=True)
        target = "/out/" + folder.relative_to(self.work).as_posix()
        flags = COMMON + ([] if standalone else ["-DRC_FIXTURE=1", "-DCC_FIXTURE=1"])
        if mode == "asan-ubsan":
            flags += SAN_FLAGS
        if macro is not None:
            flags += ["-D" + macro + "=1"]
        self.clean("compile-" + variant + "-" + mode + "-composition",
                   ["gcc", *flags, "-I/src", "-I/gen", "-c", "/src/composition.c", "-o", target + "/composition.o"],
                   empty_stdout=True)
        if standalone:
            records = self.inspect_elf(folder, ["composition"], standalone=True)
            return {"compile_only": True, "configuration": "no-RC_FIXTURE-or-CC_FIXTURE; actual-builtin-RA", "elf": records}
        self.clean("compile-" + variant + "-" + mode + "-test",
                   ["gcc", *flags, "-I/src", "-I/gen", "-c", "/src/test_composition.c",
                    "-o", target + "/test_composition.o"], empty_stdout=True)
        self.clean("compile-" + variant + "-" + mode + "-fixture",
                   ["gcc", "-g", "-fno-pie", "-c", "/src/frozen/fixture.S", "-o", target + "/fixture.o"],
                   empty_stdout=True)
        self.clean("link-" + variant + "-" + mode,
                   ["gcc", *flags, "-no-pie", target + "/composition.o",
                    target + "/test_composition.o", target + "/fixture.o", "-o", target + "/test-composition"],
                   empty_stdout=True)
        records = self.inspect_elf(folder, ["composition", "test_composition", "fixture"], "test-composition")
        for label, command in (
                ("elf-metadata", ["readelf", "-W", "-h", "-l", "-s", target + "/test-composition"]),
                ("composition-disassembly", ["objdump", "-dr", target + "/composition.o"]),
                ("linked-disassembly", ["objdump", "-d", target + "/test-composition"]),
                ("fixture-disassembly", ["objdump", "-dr", target + "/fixture.o"])):
            data = self.clean(variant + "-" + mode + "-" + label, command)
            (folder / (label + ".txt")).write_bytes(data)
        save(folder / "configuration.json", {"mode": mode, "variant": variant, "flags": flags,
             "fixture_macro": True, "control_macro": macro, "sources": self.manifest,
             "generated_files": inventory(self.generated)["files"]})
        return {"binary": target + "/test-composition", "elf": records, "control_macro": macro,
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
        schema_check = check_evidence(value, expected, Elf(regular(self.work / binary.removeprefix("/out/")), 2))
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
        result = {"schema": 1, "status": "running", "scope": "new offline SDK claim and owned installer receipt composition",
                  "sdk_commit": SDK_COMMIT, "runtime_gate": "CLOSED",
                  "image": self.inspect_image(), "modes": modes, "sections": sections,
                  "selectors": selectors, "control_names": controls,
                  "inputs": self.manifest, "composition": self.compose(),
                  "common_flags": COMMON, "sanitizer_flags": SAN_FLAGS, "sanitizer_environment": SAN_ENV,
                  "candidate": self.results["candidate"], "controls": self.results["controls"],
                  "standalone": None, "old_suites_replayed": False, "sdk_git_capture": False,
                  "native_firmware_or_device_access": False}
        self.phase = "toolchain"
        version = self.clean("compiler-version", ["gcc", "--version"])
        target = self.clean("compiler-target", ["gcc", "-dumpmachine"])
        require(target.strip() == b"aarch64-linux-gnu" and b"7.5.0" in version.splitlines()[0],
                "selected GCC7.5.0 AArch64 compiler target")
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
        "schema": 1, "status": result["status"], "scope": "new offline authored-input SDK claim/receipt composition",
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
        "standalone_configuration_executed": False, "all_executed_configurations_use_RC_FIXTURE_and_CC_FIXTURE": True,
        "actual_builtin_RA_configuration_executed": False,
        "old_suites_replayed": False, "old_sanitizer_probe_replayed": False,
        "sdk_git_capture": False, "native_firmware_or_device_access": False,
        "signal_or_atomic_codegen_proof": False,
    }
    if "image" in result:
        projected["image"] = result["image"]
    if "toolchain" in result:
        projected["toolchain"] = result["toolchain"]
    if "composition" in result:
        projected["composed_files"] = result["composition"]["files"]
    if result.get("standalone") is not None:
        projected["standalone"] = {"compiled": True, "executed": False,
                                    "configuration": "no-RC_FIXTURE-or-CC_FIXTURE; actual-builtin-RA"}
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
        # Preserve all source, composed, build/debug companions, raw outputs and
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

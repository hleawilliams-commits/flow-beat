#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain a new DSO source composition from frozen local bytes; no child commands."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

PINS = {
    "frozen/claim/composition.c": "e8a7228d1e9afa492f16e07f681daa82bf56da4ddd3f2119f17e578824be0ea8",
    "frozen/claim/composition.h": "a445905a2628c8b42ce6911b0bd27ef9e684cb1151c8b538a15123765f552c80",
    "frozen/generated/receipt-body.inc": "60233581d6a882d9e57e6b6fc9602ce440783060446872148f94e9e7205bc02e",
    "frozen/generated/receipt-types.h": "c97454db2aba6e942eb05241bbf80d67ffd8eecb5108927da69600a6a1d15f40",
    "frozen/generated/inline-receipt.inc": "03f927df4da1d9c3ed56bdf00b20893a68195e65042abcc113258e723f1398cc",
    "frozen/generated/azprot-receipt.h": "28644f70e972dfa28ecd3b940e9f4d132492ecd22f73e48d561c8684ddb9506a",
    "frozen/generated/pinned-community.inc": "b51c85bbf81885a3ee58a2bb1e9b7e803509cfde2f9d073d04621549eccaac69",
    "frozen/sdk/azmod-mod.h": "b94e2ba6656c2b852f18691d32225486f29265953c8bf09dcac9a18ce561a630",
}
def sha(b):
    return hashlib.sha256(b).hexdigest()
def meta(b):
    return {"bytes": len(b), "sha256": sha(b)}
def read(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError("nonregular source: " + str(path))
    return path.read_bytes()
def diff(before, after, old, new):
    return "".join(difflib.unified_diff(before.decode().splitlines(keepends=True),
        after.decode().splitlines(keepends=True), fromfile=old, tofile=new)).encode()
def chunk(source, first, following):
    if source.count(first) != 1 or source.count(following) != 1:
        raise ValueError("source extent is not unique: " + first)
    a, b = source.index(first), source.index(following)
    if b <= a:
        raise ValueError("reversed source extent")
    return source[a:b]
def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--inputs", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args()
    if args.output.exists():
        raise ValueError("output must be fresh")
    data = {}
    for name, expected in PINS.items():
        data[name] = read(args.inputs / name)
        if sha(data[name]) != expected:
            raise ValueError("frozen pin mismatch: " + name)
    authored = {}
    new_root = args.inputs / "new"
    if not new_root.is_dir() or new_root.is_symlink():
        raise ValueError("missing regular new source directory")
    for path in sorted(new_root.iterdir()):
        if path.suffix in (".c", ".h", ".S"):
            authored[path.name] = read(path)
    for name in ("core.c", "core.h", "dp_fixture.h"):
        if name not in authored:
            raise ValueError("missing authored source: " + name)
    parent_c = data["frozen/claim/composition.c"].decode()
    core_c = authored["core.c"].decode()
    # These exact retained function extents preserve binding, prepublication
    # permits and already-held writer install/copy semantics. Other changes are
    # fully represented by the complete parent/new diffs below.
    identical = []
    for label, first, following in (
        ("metadata-permit-functions", "static bool cc_storage_overlaps_modules(rc_span value) {", "static int cc_lock("),
        ("close-session", "static cc_status cc_close_locked(", "cc_status cc_run_module("),
        ("writer-held-install-and-core-entry", "static rc_status cc_install_locked(", "static int cc_claim_worker("),
    ):
        old = chunk(parent_c, first, following)
        new_following = "cc_status dp_core_run(" if label == "close-session" else following
        new = chunk(core_c, first, new_following)
        if label == "close-session":
            old = old.replace("cc_capture_session()", "dp_active_session()")
        if old != new:
            raise ValueError("retained function extent changed: " + label)
        identical.append({"name": label, "sha256": sha(new.encode()),
                          "bytes": len(new.encode()),
                          "rename_only": label == "close-session"})
    if "cc_claim_authored(" in core_c or "cc_claim_authored_recursive(" in core_c:
        raise ValueError("authored caller entry must remain absent")
    if core_c.count("uintptr_t raw_ra = CM_RA();") != 1:
        raise ValueError("ordinary outer caller capture")
    if core_c.count("descriptor.init(&dp_core_table)") != 1:
        raise ValueError("actual SDK init call")
    outputs = {Path(name).name: value for name, value in data.items()
               if name.startswith("frozen/generated/") or name == "frozen/sdk/azmod-mod.h"}
    outputs["composition.h"] = b'/* Exact retained receipt-body include compatibility only. */\n#include "core.h"\n'
    outputs["core.c.diff"] = diff(data["frozen/claim/composition.c"], authored["core.c"],
        "frozen/claim/composition.c", "new/core.c")
    outputs["core.h.diff"] = diff(data["frozen/claim/composition.h"], authored["core.h"],
        "frozen/claim/composition.h", "new/core.h")
    for name, value in authored.items():
        if name not in ("core.c", "core.h"):
            outputs[name + ".added.diff"] = diff(b"", value, "/dev/null", "new/" + name)
    ledger = {
        "schema": 1,
        "scope": "Offline fixed authored DSO provenance; no production loader or live attachment",
        "parent_claim_commit": "e9bcf3db860ea5e87607ccddc51faab334bf5da6",
        "plan_commit": "b12fbe65ac66927d9ed554036f80a287e1607d3c",
        "runtime_gate": "CLOSED",
        "frozen_inputs": {name: meta(value) for name, value in data.items()},
        "authored_sources": {name: meta(value) for name, value in authored.items()},
        "retained_function_extents": identical,
        "transformations": [
            {"id": "D1", "change": "Replace bulk authored loader input with one-time fixed-index reservation and checked copied module range publication; loader adapter owns actual handle/query/ELF discovery. Complete loads before any run; callbacks outside cm and writer."},
            {"id": "D2", "change": "Add separately authored exact root-map/dynamic/PT_LOAD and pinned ELF symbol checks; preserve raw versus consumed query outcomes. No unchanged SDK loader body is claimed."},
            {"id": "D3", "change": "Use retained azmod-mod.h entry/init/table types. Replace cc_descriptor and authored callback ABI with validator-before-dereference, actual entry(void), init(const az_core_v1 *). Combined entry-owner control remains loader-only dry decision."},
            {"id": "D4", "change": "Separate complete owned readable descriptor/ID/tag custody from min/max caller envelope; fix tag-loop bound-before-array access. Preserve selected ABI/id/tag acceptance semantics with fixed fixture firmware input."},
            {"id": "D5", "change": "Execute ordinary noinline builtin-RA entry. Remove authored-RA entry functions and old admission controls; retain raw and admission-used RA separately with bounded postworker result copies. RA substitution is the sole new core control."},
            {"id": "D6", "change": "Retain recursive cm then common writer, one-use session and close/count/terminal outcomes; replace old scheduler hooks with loader lifecycle diagnostics outside both locks and outside installer cut."},
            {"id": "D7", "change": "Retain five generated receipt/helper files exactly. Frozen binding definitions remain inside unchanged receipt body. Add static immutable host fixture-input accessor outside SDK table; exact plain original slots and process-image code custody remain."},
            {"id": "D8", "change": "Add bounded actual loader/constructor/entry/init diagnostics and declared five safe controls; full added-file diffs identify new authored code. No loader approval/manifest/firmware path, generated-code invocation or old runtime suite is included."},
        ],
        "generated_outputs": {name: meta(value) for name, value in outputs.items()},
        "source_oracles_only": True,
        "compiler_or_runtime_executed_by_this_tool": False,
    }
    outputs["composition-ledger.json"] = (json.dumps(ledger, indent=2) + "\n").encode()
    args.output.mkdir(parents=True)
    for name, value in outputs.items():
        (args.output / name).write_bytes(value)
    print(json.dumps({"generated": len(outputs), "ledger": meta(outputs["composition-ledger.json"]),
                      "authored_sources": len(authored), "frozen_inputs": len(data)}))
if __name__ == "__main__":
    main()

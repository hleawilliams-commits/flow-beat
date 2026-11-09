#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compose private offline source from exact retained inputs; no child commands."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

PINS = {
    "community.c": "4b2b72e2b623dcb1ecb0ef108a473815adab45a516a6aa13adb6cce512b80866",
    "receipt.c": "19d46afc34c05bbfaf19f6b2fc091fddb3af2b671b3fdd1373b524e07709d57a",
    "receipt.h": "abde4ac8f1d35fea71541f26103d5ea1b03dd44e1e475ba5b2fb65fd330dcb4d",
    "inline-receipt.inc": "03f927df4da1d9c3ed56bdf00b20893a68195e65042abcc113258e723f1398cc",
    "azprot-receipt.h": "28644f70e972dfa28ecd3b940e9f4d132492ecd22f73e48d561c8684ddb9506a",
}
def sha(b):
    return hashlib.sha256(b).hexdigest()
def meta(b):
    return {"bytes": len(b), "sha256": sha(b)}
def change(s, old, new):
    if s.count(old) != 1:
        raise ValueError("nonunique or missing transformation anchor: " + old[:80])
    return s.replace(old, new, 1)
def cut(s, start, end):
    if s.count(start) != 1 or s.count(end) != 1:
        raise ValueError("nonunique cut boundary")
    a, b = s.index(start), s.index(end)
    if a >= b:
        raise ValueError("reversed cut boundary")
    return s[:a] + s[b:]
def diff(before, after, a, b):
    return "".join(difflib.unified_diff(before.splitlines(keepends=True),
        after.splitlines(keepends=True), fromfile=a, tofile=b)).encode()
def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("sdk-community", "receipt-c", "receipt-h", "inline", "protection", "composition-c", "composition-h", "output"):
        p.add_argument("--" + name, required=True, type=Path)
    args = p.parse_args()
    paths = {"community.c": args.sdk_community, "receipt.c": args.receipt_c,
             "receipt.h": args.receipt_h, "inline-receipt.inc": args.inline,
             "azprot-receipt.h": args.protection}
    data = {}
    for name,path in paths.items():
        if path.is_symlink() or not path.is_file():
            raise ValueError("nonregular input")
        value = path.read_bytes()
        if sha(value) != PINS[name]:
            raise ValueError("input pin: " + name)
        data[name] = value
    for name,path in (("composition.c",args.composition_c),("composition.h",args.composition_h)):
        if path.is_symlink() or not path.is_file():
            raise ValueError("nonregular authored input")
        data[name] = path.read_bytes()
    if args.output.exists():
        raise ValueError("output must be fresh")
    header = data["receipt.h"].decode()
    header = change(header, "#ifndef RECEIPT_H\n#define RECEIPT_H",
                    "#ifndef CLAIM_RECEIPT_TYPES_H\n#define CLAIM_RECEIPT_TYPES_H")
    a = header.index("/* Offline authored-target fixture only.")
    b = header.index("#define RC_BINDINGS")
    header = header[:a] + ("/* Copied receipt values for the separately versioned composition.\n"
        " * Recursive SDK cm/common writer lifecycle is defined in composition.h.\n"
        " * Diagnostic addresses are capabilities, not alias isolation. */\n") + header[b:]
    header = change(header, "#define RC_ID_BYTES 32u", "#define RC_ID_BYTES 33u")
    old = """typedef struct {
    uint64_t process_id;
    char id[RC_ID_BYTES];
    uintptr_t lo, hi; /* Authored immutable envelope, not discovered ELF proof. */
} rc_module;
typedef struct { uint64_t generation; } rc_epoch;
"""
    header = change(header, old, "")
    header = cut(header, "rc_status rc_initialize(void);\n", "/* Copies mutable receipts under writer")
    body = data["receipt.c"].decode()
    body = change(body, '#include "receipt.h"', '#include "composition.h"')
    body = change(body, """static pthread_mutex_t cm = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t writer = PTHREAD_MUTEX_INITIALIZER;
static bool initialized, initialize_attempted, epoch_created, epoch_live;
static rc_module module_value;
static uint64_t module_hooks;
""", """static pthread_mutex_t writer = PTHREAD_MUTEX_INITIALIZER;
static bool initialized, initialize_attempted;
""")
    body = change(body,
        "if (epoch_created && overlap(blob, (rc_span){module_value.lo, module_value.hi}))",
        "if (cc_storage_overlaps_modules(blob))")
    body = change(body,
        "if (epoch_created && overlap(blobs[k], (rc_span){module_value.lo, module_value.hi}))",
        "if (cc_storage_overlaps_modules(blobs[k]))")
    body = change(body, """    rc_active->prepared = true;
    records[active_binding].immutable = *m; /* Still invisible to lookup. */
""", """    if (!cc_metadata_owner_valid(m)) return false;
#ifdef CC_CONTROL_TRUNCATE_ID32
    m->owner.id[31] = '\\0'; /* Safe fixture-only identity evidence corruption. */
#endif
    rc_active->prepared = true;
    records[active_binding].immutable = *m; /* Still invisible to lookup. */
""")
    body = change(body,
        "return rc_active->prepared && m->binding == active_binding &&",
        "return rc_active->prepared && cc_permit_matches(m) && m->binding == active_binding &&")
    body = change(body, """/* Exact pinned inputs are transformed offline by transform-installer.py.
 * Full diffs/ledger distinguish instrumentation from byte-identical helpers. */""",
        """/* Already retained installer/protection bytes are copied unchanged by
 * compose-source.py; no earlier generator or suite is replayed. */""")
    body = cut(body, "rc_status rc_initialize(void) {\n", "bool rc_snapshot(unsigned binding, rc_attempt *out) {\n")
    source_lines = data["community.c"].splitlines(keepends=True)
    pieces, preserved = [], []
    for name,first,last in (("cm_id_ok",461,470),("cm_find",471,476),("cm_called_by",1100,1102)):
        chunk = b"".join(source_lines[first-1:last])
        if not chunk.startswith(("static " + ("cm_mod *" if name == "cm_find" else "int ") + name + "(").encode()):
            raise ValueError("function boundary")
        pieces.append(chunk)
        preserved.append({"name":name,"source":"community.c","first_line":first,
                          "last_line":last,**meta(chunk)})
    helpers = b"/* Exact retained helper bodies; supplied bounded cm_mod/g_cm context. */\n" + b"\n".join(pieces)
    outputs = {
        "receipt-types.h": header.encode(),
        "receipt-body.inc": body.encode(),
        "pinned-community.inc": helpers,
        "inline-receipt.inc": data["inline-receipt.inc"],
        "azprot-receipt.h": data["azprot-receipt.h"],
        "receipt-types.diff": diff(data["receipt.h"].decode(), header, "receipt.h", "receipt-types.h"),
        "receipt-body.diff": diff(data["receipt.c"].decode(), body, "receipt.c", "receipt-body.inc"),
        "pinned-community.diff": diff(data["community.c"].decode(), helpers.decode(), "community.c", "pinned-community.inc"),
    }
    authored = []
    for identity,source,first,last,functions,description in [
        ("A1","community.c",1226,1273,["cc_claim_hook","cc_claim_worker"],
         "Explicitly transformed claim: outer noinline RA/session capture, predicates under recursive cm->writer, combined preopen/stale veto, exact owned binding and bounded capacity."),
        ("A2","community.c",1092,1102,["cc_lock","cc_both","cc_release_both"],
         "Selected recursive SDK cm and common writer; no writer->cm edge or lock across authored entry/init."),
        ("A3","community.c",1804,1845,["cc_loader_begin","cc_build_range","cc_run_module"],
         "Authored explicit loader provenance/capability values replace real loader/parser/descriptor work; immutable per-index range facts precede descriptor callback."),
        ("A4","community.c",1846,1859,["cc_run_module","cc_close_locked","cc_capture_session"],
         "One-use immutable sessions release-publish index+1/open and0/close, one entry acquire; close/count under shared locks, callback outside; failed init preserves receipts/count."),
        ("A5","community.c",1259,1273,["cc_claim_worker"],
         "Normally returned actual redirect accounts exactly once under locks; warnings map SDK0; raw installer/receipt values stay distinct; inconsistent publication is terminal/incomplete."),
        ("A6","community.c",1876,1894,["cc_loader_begin","cc_run_module"],
         "Added explicit one-start controller and nonblocking repeated/in-progress refusal; sequential distinct fixed module sessions allowed."),
        ("A7","receipt.c",386,461,["cc_install_locked","cc_install_core"],
         "Explicit replacement of public fixture admission: remove duplicate cm/epoch/count and replacement-in-claimant proxy; retain private writer-held actual receipt installer/cut."),
    ]:
        chunk = b"".join(data[source].splitlines(keepends=True)[first-1:last])
        authored.append({"id":identity,"kind":"explicit-authored-replacement",
            "source":source,"first_line":first,"last_line":last,**meta(chunk),
            "destination":"composition.c","functions":functions,"description":description})
    ledger = {
        "schema":1, "scope":"Offline claim/receipt source composition; not SDK integration or live authorization",
        "runtime_gate":"CLOSED", "child_commands":[], "old_generator_replayed":False,
        "inputs":{name:meta(value) for name,value in data.items()},
        "outputs":{name:meta(value) for name,value in outputs.items()},
        "preserved":preserved,
        "authored_transformations":authored,
        "changes":[
            {"id":"C1","input":"receipt.h","output":"receipt-types.h","description":"Widen owned ID to33; remove independent module/epoch and obsolete admission declarations; retain receipt value layouts and fixed symbols."},
            {"id":"C2","input":"receipt.c","output":"receipt-body.inc","description":"Remove second cm/epoch/count and public installer/lifecycle; retain common writer, syscall custody, fixed bindings, publication and diagnostics."},
            {"id":"C3","input":"receipt.c","output":"receipt-body.inc","description":"Use all committed authored module envelopes for prewrite/final storage exclusion, private immutable permit checks, and one safe fixture-only ID-copy control."},
            {"id":"C4","input":"community.c","output":"pinned-community.inc","description":"Exact three helper bodies only; claim/lifecycle orchestration is authored explicitly in composition.c."},
            {"id":"C5","input":"inline-receipt.inc","output":"inline-receipt.inc","description":"Exact retained complete generated installer copy, including unchanged no-refusal publication cut."},
            {"id":"C6","input":"azprot-receipt.h","output":"azprot-receipt.h","description":"Exact retained complete generated protection copy."},
        ],
    }
    outputs["composition-ledger.json"] = (json.dumps(ledger,indent=2,sort_keys=True)+"\n").encode()
    args.output.mkdir(parents=True)
    for name,value in outputs.items():
        with (args.output/name).open("xb") as f:
            f.write(value)
    print(json.dumps({"status":"composed-source-only","outputs":len(outputs),
                      "ledger_sha256":sha(outputs["composition-ledger.json"])}))
if __name__ == "__main__":
    main()

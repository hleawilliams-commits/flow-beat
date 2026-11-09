#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""New source composition only; no parent transformer, compiler or child commands."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

PINS = {
    "frozen/dso/core.c": "1cf59eae5a5d6de5866d7773da5a5723384670a35d051e5b9eb66b11679086cc",
    "frozen/dso/core.h": "b15d5d8ce8cfeab6646189ca93d4fd9e5f093ac0cad504a9210fb41ee0cc7f37",
    "frozen/dso/loader.c": "88fbcf828f608dee1cbdcbd8d35673c926fce1f225f1561132b9521b6ed0f930",
    "frozen/dso/loader.h": "da64db0ea5293e9dd17cbc0709ed5784556ae1c674d5da1348439d5dd67150e2",
    "frozen/dso/dp_fixture.h": "5cfc290dbe134bbcb3c5a70e75bcc94da7d8a4f284da1fc4443768ff9596282e",
    "frozen/sdk/community.c": "4b2b72e2b623dcb1ecb0ef108a473815adab45a516a6aa13adb6cce512b80866",
    "frozen/sdk/azmod-mod.h": "b94e2ba6656c2b852f18691d32225486f29265953c8bf09dcac9a18ce561a630",
    "frozen/receipt/receipt-body.inc": "60233581d6a882d9e57e6b6fc9602ce440783060446872148f94e9e7205bc02e",
    "frozen/receipt/receipt-types.h": "c97454db2aba6e942eb05241bbf80d67ffd8eecb5108927da69600a6a1d15f40",
    "frozen/receipt/inline-receipt.inc": "03f927df4da1d9c3ed56bdf00b20893a68195e65042abcc113258e723f1398cc",
    "frozen/receipt/azprot-receipt.h": "28644f70e972dfa28ecd3b940e9f4d132492ecd22f73e48d561c8684ddb9506a",
    "frozen/receipt/pinned-community.inc": "b51c85bbf81885a3ee58a2bb1e9b7e803509cfde2f9d073d04621549eccaac69"
}
CONSUMER_SHA = "9e31f235353a169e46f366ec33488076829b9383bc0fd0c462b465a0ddef70b4"
def digest(data): return hashlib.sha256(data).hexdigest()
def meta(data): return {"bytes":len(data),"sha256":digest(data)}
def read(path):
    if path.is_symlink() or not path.is_file(): raise ValueError("regular source required: "+str(path))
    return path.read_bytes()
def diff(a,b,old,new):
    return "".join(difflib.unified_diff(a.decode().splitlines(keepends=True),b.decode().splitlines(keepends=True),fromfile=old,tofile=new)).encode()
def extent(data,first,following):
    first=first.encode();following=following.encode()
    if data.count(first)!=1 or data.count(following)!=1: raise ValueError("unique source extent")
    a=data.index(first);b=data.index(following)
    if b<=a: raise ValueError("ordered source extent")
    return data[a:b]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    args=parser.parse_args()
    if args.output.exists(): raise ValueError("fresh composition directory required")
    frozen={}
    for name,pin in PINS.items():
        raw=read(args.inputs/name)
        if digest(raw)!=pin: raise ValueError("frozen source pin: "+name)
        frozen[name]=raw
    new=args.inputs/"new"
    if new.is_symlink() or not new.is_dir(): raise ValueError("regular authored directory")
    authored={}
    for path in sorted(new.iterdir()):
        if path.suffix in (".c",".h",".inc",".S"): authored[path.name]=read(path)
    for name in ("core.c","core.h","attribution.h","attribution.inc","loader.c","loader.h","test_module.c"):
        if name not in authored: raise ValueError("missing authored source: "+name)
    source_ledger=read(new/"source-ledger.json")
    described=json.loads(source_ledger)
    for name,pin in described["source_files"].items():
        if meta(read(new/name))!=pin: raise ValueError("reviewed ledger source mismatch: "+name)
    consumer=extent(frozen["frozen/sdk/community.c"],"static const char *cm_blame_fault(","static void cm_crash(")
    if len(consumer)!=962 or digest(consumer)!=CONSUMER_SHA: raise ValueError("exact consumer extent pin")
    old=frozen["frozen/dso/core.c"];current=authored["core.c"]
    identical=[]
    for label,first,following in (
        ("metadata-owner-and-permit", "static bool cc_metadata_owner_valid(const rc_metadata *value) {", "static int cc_lock("),
        ("close-session", "static cc_status cc_close_locked(", "cc_status dp_core_run("),
        ("fixed-writer-installer-and-claim", "static rc_status cc_install_locked(", "/* Process-lifetime diagnostic slots"),
    ):
        a=extent(old,first,following);b=extent(current,first,following)
        if a!=b: raise ValueError("frozen core extent changed: "+label)
        identical.append({"name":label,**meta(b)})
    # The full generated files remain exact, including rc_lookup and the cut.
    generated={Path(n).name:b for n,b in frozen.items() if n.startswith("frozen/receipt/")}
    generated["azmod-mod.h"]=frozen["frozen/sdk/azmod-mod.h"]
    generated["dp_fixture.h"]=frozen["frozen/dso/dp_fixture.h"]
    generated["composition.h"]=b'/* Exact retained receipt include compatibility. */\n#include "core.h"\n'
    generated["pinned-consumer.inc"]=consumer
    parent_for={"core.c":"frozen/dso/core.c","core.h":"frozen/dso/core.h","loader.c":"frozen/dso/loader.c","loader.h":"frozen/dso/loader.h"}
    for name,data in authored.items():
        parent=parent_for.get(name)
        generated[name+(".diff" if parent else ".added.diff")]=diff(frozen[parent] if parent else b"",data,parent or "/dev/null","new/"+name)
    selected=[("generated-lookup","frozen/receipt/receipt-body.inc","bool rc_lookup(","int rc_query_protection("),
              ("publication-cut","frozen/receipt/inline-receipt.inc","        /* the original before the store that makes it reachable */","        /* End cut. No caller callback before the actual target branch store. */")]
    for label,name,first,following in selected:
        identical.append({"name":label,**meta(extent(frozen[name],first,following))})
    ledger={"schema":1,"scope":"Offline immutable module/generated attribution; no native signal or live integration", "runtime_gate":"CLOSED",
        "parent_commit":"f2cbeb2fb4394c5a79854c84d9d70f4bd812e721","plan_commit":"6f57ebbe7243a303540e8888fe598414402199b4",
        "frozen_inputs":{n:meta(b) for n,b in frozen.items()},"authored_sources":{n:meta(b) for n,b in authored.items()},
        "reviewed_source_ledger":meta(source_ledger),"retained_function_extents":identical,
        "exact_consumer":{"bytes":962,"sha256":CONSUMER_SHA,"scoped_aliases_in":"new/attribution.inc"},
        "transformations":described["transformations"],"safe_controls":described["safe_controls"],
        "generated_outputs":{n:meta(b) for n,b in generated.items()},
        "compiler_or_runtime_executed_by_this_tool":False,"old_transformer_replayed":False}
    generated["composition-ledger.json"]=(json.dumps(ledger,indent=2)+"\n").encode()
    args.output.mkdir(parents=True)
    for name,data in generated.items():
        with (args.output/name).open("xb") as f:f.write(data)
    print(json.dumps({"generated":len(generated),"ledger":meta(generated["composition-ledger.json"]),"frozen_inputs":len(frozen),"authored_sources":len(authored)}))
if __name__=="__main__":main()

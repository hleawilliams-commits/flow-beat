#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract reviewed immutable readers only; no compiler, loader or child process."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat

def require(ok, message):
    if not ok:
        raise ValueError(message)

def digest(raw):
    return hashlib.sha256(raw).hexdigest()

def meta(raw):
    return {"bytes":len(raw),"sha256":digest(raw)}

def read(path):
    require(path.is_absolute() and ".." not in path.parts, "absolute input path")
    cursor=Path(path.anchor)
    for part in path.parts[1:]:
        cursor=cursor/part
        require(not cursor.is_symlink(), "symlink input path")
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
    with os.fdopen(fd,"rb") as stream:
        before=os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode), "regular input required")
        raw=stream.read()
        after=os.fstat(stream.fileno())
    require(all(getattr(before,k)==getattr(after,k) for k in
                ("st_dev","st_ino","st_size","st_mtime_ns","st_ctime_ns")), "input changed")
    require(len(raw)==before.st_size,"input length changed")
    return raw

def write(path, raw):
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,"wb") as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    contract_raw=read(Path(os.path.abspath(__file__)).with_name("reader-extraction.json"))
    contract=json.loads(contract_raw)
    require(contract["schema"]==1 and contract["runtime_gate"]=="CLOSED","closed extraction")
    sources={}
    for name,pin in contract["sources"].items():
        require(name in ("module/attribution.inc","generated/receipt-body.inc","generated/receipt-types.h"),
                "fixed input names")
        raw=read(args.inputs/name)
        require(meta(raw)==pin,"source pin mismatch: "+name)
        sources[name]=raw
    require(len(sources)==3,"complete extraction inputs")
    pieces=[]
    for part in contract["fragments"]:
        raw=sources[part["source"]]
        lines=raw.splitlines(keepends=True)
        first,last=part["first_line"],part["last_line"]
        require(type(first) is int and type(last) is int and 1<=first<=last<=len(lines),"line extent")
        piece=b"".join(lines[first-1:last])
        require(meta(piece)==part["pin"],"fragment pin: "+part["name"])
        pieces.append(piece)
    readers=contract["preamble"].encode()+b"\n".join(pieces)
    outputs={"readers.inc":readers,"receipt-types.h":sources["generated/receipt-types.h"]}
    require({name:meta(raw) for name,raw in outputs.items()}==contract["outputs"],"exact generated output pins")
    require(args.out.is_absolute() and args.out.parent.is_dir() and ".." not in args.out.parts,"output parent")
    cursor=Path(args.out.anchor)
    for component in args.out.parts[1:-1]:
        cursor=cursor/component
        require(not cursor.is_symlink(),"symlink output ancestor")
    args.out.mkdir(mode=0o700) # exclusive: existing generated directory is refused
    for name,raw in outputs.items():
        write(args.out/name,raw)
    result={"schema":1,"status":"extracted","runtime_gate":"CLOSED",
            "contract":meta(contract_raw),"fragments":contract["fragments"],
            "outputs":contract["outputs"],"compiler_or_runtime_execution":False,
            "consumer_or_installer_body_included":False}
    write(args.out/"extraction.json",(json.dumps(result,indent=2,sort_keys=True)+"\n").encode())
    print(json.dumps({"status":"extracted","fragments":len(pieces),"outputs":contract["outputs"]},sort_keys=True))

if __name__=="__main__":
    main()

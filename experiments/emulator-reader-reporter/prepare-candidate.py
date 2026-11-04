#!/usr/bin/env python3
"""Prepare an inactive retained-reader module beside frozen admission ownership."""
import argparse
import hashlib
import json
from pathlib import Path
import types

if not __debug__:
    raise RuntimeError("optimized Python disables frozen parent pin assertions")
HERE=Path(__file__).resolve().parent
INPUTS=("reporter.rs","tests.rs","parent-inputs.json","candidate-pins.json","prepare-candidate.py")

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(value,message):
    if not value:
        raise RuntimeError(message)

def compose(original,captured):
    candidate=dict(original)
    main="az-preview/src/main.rs"
    anchor="#[cfg(unix)]\nmod supervisor_admission;"
    text=candidate[main].decode()
    require(text.count(anchor)==1,"main module anchor differs")
    candidate[main]=text.replace(anchor,anchor+"\n#[cfg(unix)]\nmod reader_reporter;").encode()
    candidate["az-preview/src/reader_reporter.rs"]=captured["reporter.rs"]
    candidate["az-preview/src/reader_reporter_tests.rs"]=captured["tests.rs"]
    require(len(candidate)==34,"candidate file count differs")
    require({n for n in original if original[n]!=candidate[n]}=={main},
            "unexpected parent mutation")
    return candidate

def prepare(emu_source):
    captured={n:(HERE/n).read_bytes() for n in INPUTS}
    parent_pins=json.loads(captured["parent-inputs.json"])
    require(len(parent_pins)==48,"frozen input inventory differs")
    frozen={n:(HERE.parent/n).read_bytes() for n in parent_pins}
    require({n:sha(b) for n,b in frozen.items()}==parent_pins,"frozen inputs changed")
    parent=HERE.parent/"emulator-supervisor-admission"
    helper=types.ModuleType("frozen_admission")
    helper.__file__=str(parent/"prepare-candidate.py")
    raw=frozen["emulator-supervisor-admission/prepare-candidate.py"].decode()
    anchor='    helper.OUTPUT_ROOT=HERE/"private"\n'
    require(raw.count(anchor)==1,"parent output redirect anchor differs")
    helper.OUTPUT_ROOT=HERE/"private"
    exec(compile(raw.replace(anchor,"    helper.OUTPUT_ROOT=OUTPUT_ROOT\n"),
                 helper.__file__,"exec"),helper.__dict__)
    output=helper.prepare(emu_source.resolve())
    receipt=output/"reporter-preparation.json"
    receipt.write_text(json.dumps({"status":"started"})+"\n")
    try:
        tree=output/"tree"
        baseline=json.loads(frozen["emulator-supervisor-admission/candidate-pins.json"])
        original={n:(tree/n).read_bytes() for n in baseline}
        require(len(original)==32 and {n:sha(b) for n,b in original.items()}==baseline,
                "prepared parent mismatch")
        (output/"inputs").rename(output/"admission-inputs")
        (output/"inputs").mkdir()
        for group,values in (("inputs",captured),("reporter-parent-inputs",frozen),
                             ("reporter-parent-tree",original)):
            for name,data in values.items():
                p=output/group/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        candidate=compose(original,captured)
        pins=json.loads(captured["candidate-pins.json"])
        require({n:sha(b) for n,b in candidate.items()}==pins,"composed candidate differs")
        for name,data in candidate.items():
            p=tree/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        require(all((HERE/n).read_bytes()==b for n,b in captured.items()),"inputs moved during preparation")
        require(all((HERE.parent/n).read_bytes()==b for n,b in frozen.items()),"parents moved during preparation")
        result={"status":"verified","inputs_sha256":{n:sha(b) for n,b in captured.items()},
                "parent_inputs_sha256":parent_pins,"parent_files_sha256":baseline,
                "candidate_files_sha256":pins,"changed_parent_files":["az-preview/src/main.rs"],
                "scope":"Inactive one-shot retained reader/reporter. Module declaration only; no main selection, Child ownership, port adapter, cross-process protocol or legacy supervisor changes."}
        receipt.write_text(json.dumps(result,sort_keys=True,indent=2)+"\n")
    except BaseException as error:
        receipt.write_text(json.dumps({"status":"failed","failure":type(error).__name__+": "+str(error)},indent=2)+"\n")
        raise
    return output

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--emu-source",type=Path,required=True)
    args=p.parse_args()
    print(prepare(args.emu_source))

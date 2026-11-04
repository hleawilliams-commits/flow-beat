#!/usr/bin/env python3
"""Prepare only an inactive admission/actual-child composition after frozen startup."""
import argparse
import hashlib
import json
from pathlib import Path
import types

if not __debug__:
    raise RuntimeError("optimized Python disables frozen parent pin assertions")
HERE=Path(__file__).resolve().parent
INPUTS=("admission.rs","startup-scope.inc.rs","transport-latch.inc.rs",
        "transport-test-edits.json","tests.rs","parent-inputs.json",
        "candidate-pins.json","prepare-candidate.py")

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(value,message):
    if not value:
        raise RuntimeError(message)

def compose(original,captured,frozen):
    candidate=dict(original)
    main="az-preview/src/main.rs"
    anchor="#[cfg(unix)]\nmod owned_startup;"
    declaration='\n#[cfg(unix)]\n#[path = "supervisor_observation/observation.rs"]\nmod supervisor_observation;\n#[cfg(unix)]\nmod supervisor_admission;'
    text=candidate[main].decode()
    require(text.count(anchor)==1,"main module anchor differs")
    candidate[main]=text.replace(anchor,anchor+declaration).encode()
    startup="az-preview/src/owned_startup.rs"
    candidate[startup]+=b"\n"+captured["startup-scope.inc.rs"]
    transport="az-preview/src/mixer_transport.rs"
    text=candidate[transport].decode()
    edits=json.loads(captured["transport-test-edits.json"])
    require(isinstance(edits,list) and len(edits)>0,"missing test seam edits")
    for edit in edits:
        require(text.count(edit["old"])==1 and edit["old"]!=edit["new"],
                "test seam anchor differs")
        text=text.replace(edit["old"],edit["new"])
    candidate[transport]=text.encode()+b"\n"+captured["transport-latch.inc.rs"]
    candidate["az-preview/src/supervisor_admission.rs"]=captured["admission.rs"]
    candidate["az-preview/src/supervisor_admission_tests.rs"]=captured["tests.rs"]
    for n in ("observation.rs","tests.rs"):
        candidate["az-preview/src/supervisor_observation/"+n]=frozen["emulator-supervisor-observation/"+n]
    require(len(candidate)==32,"candidate file count differs")
    require({n for n in original if original[n]!=candidate[n]}=={main,startup,transport},
            "unexpected parent mutation")
    return candidate

def prepare(emu_source):
    captured={n:(HERE/n).read_bytes() for n in INPUTS}
    parent_pins=json.loads(captured["parent-inputs.json"])
    require(len(parent_pins)==40,"frozen input inventory differs")
    frozen={n:(HERE.parent/n).read_bytes() for n in parent_pins}
    require({n:sha(b) for n,b in frozen.items()}==parent_pins,"frozen inputs changed")
    parent=HERE.parent/"emulator-owned-startup"
    helper=types.ModuleType("frozen_startup")
    helper.__file__=str(parent/"prepare-candidate.py")
    raw=frozen["emulator-owned-startup/prepare-candidate.py"].decode()
    anchor="    helper.OUTPUT_ROOT = HERE/'private'\n"
    require(raw.count(anchor)==1,"parent output redirect anchor differs")
    helper.OUTPUT_ROOT=HERE/"private"
    exec(compile(raw.replace(anchor,"    helper.OUTPUT_ROOT = OUTPUT_ROOT\n"),
                 helper.__file__,"exec"),helper.__dict__)
    output=helper.prepare(emu_source.resolve())
    receipt=output/"admission-preparation.json"
    receipt.write_text(json.dumps({"status":"started"})+"\n")
    try:
        tree=output/"tree"
        baseline=json.loads(frozen["emulator-owned-startup/candidate-pins.json"])
        original={n:(tree/n).read_bytes() for n in baseline}
        require(len(original)==28 and {n:sha(b) for n,b in original.items()}==baseline,
                "prepared parent mismatch")
        (output/"inputs").rename(output/"startup-inputs")
        (output/"inputs").mkdir()
        for group,values in (("inputs",captured),("admission-parent-inputs",frozen),("parent-tree",original)):
            for name,data in values.items():
                p=output/group/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        candidate=compose(original,captured,frozen)
        pins=json.loads(captured["candidate-pins.json"])
        require({n:sha(b) for n,b in candidate.items()}==pins,"composed candidate differs")
        for name,data in candidate.items():
            p=tree/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        require(all((HERE/n).read_bytes()==b for n,b in captured.items()),"inputs moved during preparation")
        require(all((HERE.parent/n).read_bytes()==b for n,b in frozen.items()),"parents moved during preparation")
        result={"status":"verified","inputs_sha256":{n:sha(b) for n,b in captured.items()},
                "parent_inputs_sha256":parent_pins,"parent_files_sha256":baseline,
                "candidate_files_sha256":pins,
                "changed_parent_files":["az-preview/src/main.rs","az-preview/src/mixer_transport.rs","az-preview/src/owned_startup.rs"],
                "scope":"Inactive scoped child/admission composition. Terminal port latch plus cfg(test) reservation seam, appended StartupHandle<Child> scope and module declarations; no actual main selection or legacy supervisor changes."}
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

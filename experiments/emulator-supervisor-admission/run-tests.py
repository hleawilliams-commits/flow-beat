#!/usr/bin/env python3
"""Compile the inactive actual-startup composition and execute only its new selectors."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import types

HERE=Path(__file__).resolve().parent
ACTIVE_RECEIPT=None
INPUTS=("admission.rs","startup-scope.inc.rs","transport-latch.inc.rs",
        "transport-test-edits.json","tests.rs","parent-inputs.json",
        "candidate-pins.json","prepare-candidate.py","run-tests.py",
        "controls.json","test-inventory.json")

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(value,message):
    if not value:
        raise RuntimeError(message)

def run(command, log, env, cwd, timeout=45):
    # Every command and descendant belongs to a new private process group.
    # Timeout is an infrastructure failure, never a successful negative control.
    proc = subprocess.Popen(command, env=env, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except BaseException as error:
        # Interrupt/logging failure must not abandon the newly created group.
        # Preserve the initiating exception and any cleanup failures separately.
        cleanup = []
        try:
            if isinstance(error, subprocess.TimeoutExpired):
                log.with_suffix(".timeout-prefix.log").write_bytes(
                    (error.stdout or b"") + (error.stderr or b""))
        except BaseException as capture_error:
            cleanup.append("prefix capture: " + repr(capture_error))
        finally:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            except BaseException as kill_error:
                cleanup.append("group kill: " + repr(kill_error))
            try:
                stdout, stderr = proc.communicate(timeout=10)
                log.write_bytes(stdout + stderr)
            except BaseException as drain_error:
                cleanup.append("drain/log: " + repr(drain_error))
        if cleanup:
            try:
                log.with_suffix(".cleanup-error.log").write_text("\n".join(cleanup)+"\n")
            except BaseException:
                pass
        if isinstance(error, subprocess.TimeoutExpired):
            raise RuntimeError("private process group timed out; never an expected control: " + log.name) from error
        raise
    log.write_bytes(stdout + stderr)
    return subprocess.CompletedProcess(command, proc.returncode, stdout, stderr)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--emu-source",type=Path,required=True)
    p.add_argument("--host-only",action="store_true",
                   help="independent focused repeat, omitting prior production cfg check")
    args=p.parse_args()
    inputs={n:(HERE/n).read_bytes() for n in INPUTS}
    inventory=json.loads(inputs["test-inventory.json"])
    selected=inventory["selected"];all_tests=inventory["all"]
    controls=json.loads(inputs["controls.json"])
    require(len(selected)==7 and selected==sorted(set(selected)),"new selector inventory differs")
    require(len(all_tests)==267 and all_tests==sorted(set(all_tests)),"full compiled inventory differs")
    require([n for n in all_tests if n.startswith("supervisor_admission::tests::")]==selected,
            "selected inherited selector")
    require(len(controls["controls"])==4,"strict control count differs")
    pins=json.loads(inputs["candidate-pins.json"])
    require(all(pins[n]==h for n,h in controls["candidate_sha256"].items()),"control pins differ")
    helper=types.ModuleType("captured_prepare")
    helper.__file__=str(HERE/"prepare-candidate.py")
    exec(compile(inputs["prepare-candidate.py"],helper.__file__,"exec"),helper.__dict__)
    output=helper.prepare(args.emu_source.resolve())
    global ACTIVE_RECEIPT
    receipt=output/"result.json";ACTIVE_RECEIPT=receipt
    receipt.write_text(json.dumps({"status":"started","phase":"captured"})+"\n")
    print("Private snapshot:",output,flush=True)
    for n,data in inputs.items():
        (output/"inputs"/n).write_bytes(data)
    original={n:(output/"tree"/n).read_bytes() for n in pins}
    require({n:sha(b) for n,b in original.items()}==pins,"captured candidate differs")
    env={k:v for k,v in os.environ.items() if k in
         {"PATH","HOME","TMPDIR","USER","LOGNAME","LANG","LC_ALL"}}
    env["CARGO_TARGET_DIR"]=str(output/"target");env["CARGO_NET_OFFLINE"]="true"
    cargo=shutil.which("cargo",path=env.get("PATH"))
    rustc=shutil.which("rustc",path=env.get("PATH"))
    require(cargo and rustc,"host compiler unavailable")
    versions={}
    for n,cmd in (("rustc",[rustc,"--version","--verbose"]),("cargo",[cargo,"--version"])):
        r=run(cmd,output/(n+".log"),env,output)
        require(r.returncode==0,"compiler version failed")
        versions[n]=r.stdout.decode().strip()
    result={"status":"started","host_only":args.host_only,"versions":versions,
            "inputs_sha256":{n:sha(b) for n,b in inputs.items()},
            "candidate_files_sha256":pins,"positive_count":7,"strict_control_count":4,
            "inherited_tests_filtered":260,"runs":[],
            "scope":"Inactive real retained startup/worker/actual-Child composition. Terminal local port admission only; no production main selection, native cancellation, guest stop, emulator, device, recovery or physical timing proof."}
    receipt.write_text(json.dumps(result,sort_keys=True,indent=2)+"\n")
    if not args.host_only:
        checked=run([cargo,"check","--locked","--offline","--manifest-path",
                     str(output/"tree/az-preview/Cargo.toml")],
                    output/"production-check.log",env,output,240)
        require(checked.returncode==0,"production cfg check failed")
        result["production_check_exit"]=0
    else:
        result["production_check_exit"]=None
    for control in [None]+controls["controls"]:
        label=control["name"] if control else "candidate"
        folder=output/label;folder.mkdir();tree=folder/"tree"
        variant=dict(original)
        if control:
            require(control["test"] in selected,"control selected an inherited test")
            for edit in control.get("edits",[control]):
                require(edit["file"] in variant,"foreign control file")
                text=variant[edit["file"]].decode()
                require(text.count(edit["old"])==1 and edit["old"]!=edit["new"],"control anchor differs")
                variant[edit["file"]]=text.replace(edit["old"],edit["new"]).encode()
        for n,data in variant.items():
            dest=tree/n;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
        built=run([cargo,"test","--locked","--offline","--manifest-path",
                   str(tree/"az-preview/Cargo.toml"),"--no-run","--message-format=json"],
                  folder/"compile.log",env,output,240)
        require(built.returncode==0,"variant compile failed: "+label)
        messages=[json.loads(line) for line in built.stdout.splitlines() if line.startswith(b"{")]
        executables=[m["executable"] for m in messages if m.get("reason")=="compiler-artifact"
                     and m.get("executable") and m.get("profile",{}).get("test")]
        require(len(executables)==1,"ambiguous test executable")
        built_exe=Path(executables[0]).resolve()
        require(built_exe.is_relative_to((output/"target").resolve()),"executable outside private target")
        exe=folder/"tests-bin";exe.write_bytes(built_exe.read_bytes());exe.chmod(0o700)
        binary_hash=sha(exe.read_bytes())
        listing=run([str(exe),"--list"],folder/"inventory.log",env,output)
        listed=sorted(re.findall(r"^(\S+): test$",listing.stdout.decode(),re.M))
        require(listing.returncode==0 and listed==all_tests,"compiled test inventory differs")
        fixtures=folder/"fixtures";fixtures.mkdir()
        test_env=dict(env);test_env["AZ_SUPERVISOR_ADMISSION_TEST_ROOT"]=str(fixtures)
        row={"name":label,"compile_exit":0,"source_sha256":{n:sha(b) for n,b in variant.items()},
             "executable_sha256":binary_hash,"tests":[]}
        for index,test in enumerate([control["test"]] if control else selected):
            log=folder/("case-"+str(index)+".log")
            ran=run([str(exe),test,"--exact","--test-threads=1","--nocapture"],log,test_env,output,40)
            text=(ran.stdout+ran.stderr).decode(errors="replace")
            require("running 1 test" in text and "FIXTURE_ABORT" not in text,"fixture/selection failure")
            require(not re.search(r"fatal runtime error|stack overflow|AddressSanitizer|ThreadSanitizer|runtime error:",text),
                    "unexpected runtime diagnostic")
            entry={"test":test,"exit":ran.returncode,"log_sha256":sha(log.read_bytes())}
            if control:
                raw=variant["az-preview/src/supervisor_admission_tests.rs"].decode()
                name=test.rsplit("::",1)[-1]
                start=raw.index("fn "+name+"(");end=raw.find("\n#[test]",start)
                end=len(raw) if end<0 else end
                section=raw[start:end];marker=control["assertion_message"]
                anchor=control["assertion_anchor"]
                require(anchor.startswith("assert_eq!(") and section.count(marker)==1
                        and section.count(anchor)==1, "ambiguous control oracle")
                pos=start+section.index(anchor)
                loc="supervisor_admission_tests.rs:"+str(raw.count("\n",0,pos)+1)+":"+str(pos-raw.rfind("\n",0,pos))+":"
                require(ran.returncode==101 and marker in text and loc in text
                        and "test result: FAILED. 0 passed; 1 failed;" in text,
                        "control did not reach exact final oracle: "+label)
                entry["expected_assertion_location"]=loc
            else:
                require(ran.returncode==0 and "test result: ok. 1 passed; 0 failed;" in text,
                        "positive failed: "+test)
            row["tests"].append(entry)
            require(sha(exe.read_bytes())==binary_hash,"test executable changed during execution")
        result["runs"].append(row)
        receipt.write_text(json.dumps(result,sort_keys=True,indent=2)+"\n")
        require(all((tree/n).read_bytes()==b for n,b in variant.items()),"variant source changed")
        print(label,"selected="+str(len(row["tests"])),"passed expected outcomes",flush=True)
    require(all((HERE/n).read_bytes()==b for n,b in inputs.items()),"published input changed")
    require(all((output/"tree"/n).read_bytes()==b for n,b in original.items()),"prepared source changed")
    parent=json.loads(inputs["parent-inputs.json"])
    require(all(sha((HERE.parent/n).read_bytes())==h for n,h in parent.items()),"frozen parent changed")
    logs={p.relative_to(output).as_posix():sha(p.read_bytes()) for p in output.rglob("*.log")
          if "target" not in p.relative_to(output).parts}
    result.update(status="passed",logs_sha256=logs)
    receipt.write_text(json.dumps(result,sort_keys=True,indent=2)+"\n")
    print("PASS: 7 new selectors, 4 strict controls; 260 inherited selectors filtered.",flush=True)
    print("Private receipt:",receipt,flush=True)

if __name__=="__main__":
    try:
        main()
    except BaseException as error:
        if ACTIVE_RECEIPT is not None and ACTIVE_RECEIPT.exists():
            failed=json.loads(ACTIVE_RECEIPT.read_text())
            failed.update(status="failed",failure=type(error).__name__+": "+str(error))
            ACTIVE_RECEIPT.write_text(json.dumps(failed,sort_keys=True,indent=2)+"\n")
        raise

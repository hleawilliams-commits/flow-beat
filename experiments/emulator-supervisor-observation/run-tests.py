#!/usr/bin/env python3
"""Run only this new observation slice with private sentinel children.

Default: also compile the inactive module in the pinned actual az-preview crate.
--host-only skips that integration check for an independent focused repeat.
No emulator, application window, device endpoint, guest launch, or prior suite.
"""
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

HERE = Path(__file__).resolve().parent
ACTIVE_RECEIPT = None
INPUTS = ("observation.rs", "tests.rs", "base-source-pins.json", "candidate-pins.json",
          "prepare-candidate.py", "run-tests.py", "controls.json", "test-inventory.json")

def require(value, message):
    if not value:
        raise RuntimeError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def run(command, log, env, cwd, timeout=45):
    # Every command and descendant belongs to a new private process group.
    # Timeout is an infrastructure failure, never a successful negative control.
    proc = subprocess.Popen(command, env=env, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        log.with_suffix(".timeout-prefix.log").write_bytes(
            (error.stdout or b"") + (error.stderr or b""))
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        stdout, stderr = proc.communicate(timeout=10)
        log.write_bytes(stdout + stderr)
        raise RuntimeError("private process group timed out; never an expected control: " + log.name)
    log.write_bytes(stdout + stderr)
    return subprocess.CompletedProcess(command, proc.returncode, stdout, stderr)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--emu-source", type=Path, required=True)
    p.add_argument("--host-only", action="store_true")
    args = p.parse_args()
    inputs = {n:(HERE/n).read_bytes() for n in INPUTS}
    inventory = json.loads(inputs["test-inventory.json"])
    controls = json.loads(inputs["controls.json"])
    selected = inventory["selected"]
    require(len(selected) == 8 and selected == sorted(set(selected)), "unexpected test inventory")
    require(all(n.startswith("tests::") for n in selected), "foreign selected test")
    require(len(controls["controls"]) == 4, "unexpected strict control count")
    require(controls["observation_sha256"] == sha(inputs["observation.rs"])
            and controls["tests_sha256"] == sha(inputs["tests.rs"]), "control source pins differ")
    prepare = types.ModuleType("captured_prepare")
    prepare.__file__ = str(HERE/"prepare-candidate.py")
    exec(compile(inputs["prepare-candidate.py"], prepare.__file__, "exec"), prepare.__dict__)
    output = prepare.prepare(args.emu_source.resolve())
    global ACTIVE_RECEIPT
    receipt = output/"result.json"
    ACTIVE_RECEIPT = receipt
    receipt.write_text(json.dumps({"status":"started", "phase":"captured"} )+"\n")
    print("Private snapshot:", output, flush=True)
    for name,data in inputs.items():
        (output/"inputs"/name).write_bytes(data)
    # A minimal inherited environment avoids ambient target/wrapper/preload flags.
    env = {k:v for k,v in os.environ.items()
           if k in {"PATH", "HOME", "TMPDIR", "USER", "LOGNAME", "LANG", "LC_ALL"}}
    env["CARGO_TARGET_DIR"] = str(output/"target")
    env["CARGO_NET_OFFLINE"] = "true"
    rustc = shutil.which("rustc", path=env.get("PATH"))
    cargo = shutil.which("cargo", path=env.get("PATH"))
    require(rustc is not None and cargo is not None, "host compiler unavailable")
    versions = {}
    for name,command in (("rustc", [rustc, "--version", "--verbose"]), ("cargo", [cargo, "--version"])):
        proc = run(command, output/(name+".log"), env, output)
        require(proc.returncode == 0, name + " unavailable")
        versions[name] = proc.stdout.decode().strip()
    summary = {"status":"started", "host_only":args.host_only, "versions":versions,
        "emulator_commit":prepare.COMMIT, "inputs_sha256":{n:sha(b) for n,b in inputs.items()},
        "candidate_files_sha256":json.loads(inputs["candidate-pins.json"]),
        "scope":"Inactive std-only borrowed Child observation and one-shot reader result retention. Actual private sentinel children, anonymous socket EOF, and host channel disconnection; scripted reader/poll errors and panics. No lifecycle or route wiring, emulator execution, native authority, sanitizers or hardware evidence.",
        "positive_count":8, "strict_control_count":4, "runs":[]}
    receipt.write_text(json.dumps(summary,indent=2,sort_keys=True)+"\n")
    if not args.host_only:
        result = run([cargo, "check", "--locked", "--offline", "--manifest-path",
                      str(output/"tree/az-preview/Cargo.toml")],
                     output/"production-check.log", env, output, 240)
        require(result.returncode == 0, "inactive module actual crate production check failed")
        summary["production_check_exit"] = result.returncode
    else:
        summary["production_check_exit"] = None
    library = output/"lib"
    library.mkdir()
    for n in ("observation.rs", "tests.rs"):
        (library/n).write_bytes(inputs[n])
    result = run([rustc, "--edition=2021", "--crate-name", "supervisor_observation",
                  "--crate-type=lib", str(library/"observation.rs"), "-o", str(library/"observation.rlib")],
                 library/"compile.log", env, output)
    require(result.returncode == 0, "standalone production cfg compile failed")
    summary["standalone_production_compile_exit"] = result.returncode
    for control in [None] + controls["controls"]:
        label = control["name"] if control else "candidate"
        directory = output/label
        directory.mkdir()
        source = inputs["observation.rs"].decode()
        if control:
            require(control["test"] in selected, "control test not selected")
            require(source.count(control["old"]) == 1 and control["old"] != control["new"],
                    "control mutation anchor differs: " + label)
            source = source.replace(control["old"], control["new"])
        (directory/"observation.rs").write_text(source)
        (directory/"tests.rs").write_bytes(inputs["tests.rs"])
        exe = directory/"tests-bin"
        result = run([rustc, "--edition=2021", "--crate-name", "supervisor_observation",
                      "--test", str(directory/"observation.rs"), "-o", str(exe)],
                     directory/"compile.log", env, output)
        require(result.returncode == 0, "variant did not compile: " + label)
        listed = run([str(exe), "--list"], directory/"inventory.log", env, output)
        require(listed.returncode == 0, "inventory execution failed")
        names = sorted(line.removesuffix(": test") for line in listed.stdout.decode().splitlines()
                       if line.endswith(": test"))
        require(names == selected and "8 tests, 0 benchmarks" in listed.stdout.decode(),
                "compiled test inventory differs: " + label)
        chosen = [control["test"]] if control else selected
        row = {"name":label, "source_sha256":sha(source.encode()),
               "tests_sha256":sha(inputs["tests.rs"]), "compile_exit":0, "tests":[]}
        for i,test in enumerate(chosen):
            log = directory/("case-"+str(i)+".log")
            result = run([str(exe), test, "--exact", "--test-threads=1", "--nocapture"],
                         log, env, output, 30)
            text = (result.stdout + result.stderr).decode(errors="replace")
            require("FIXTURE_ABORT" not in text and "running 1 test" in text,
                    "fixture cleanup or selection failure: " + label)
            if control:
                marker = control["assertion_message"]
                tests = inputs["tests.rs"].decode()
                require(tests.count(marker) == 1, "control assertion not unique")
                pos = tests.rfind("assert_eq!(observed,", 0, tests.index(marker))
                require(pos >= 0 and tests.rfind("fn "+test.split("::")[-1]+"()", 0, pos) >= 0,
                        "control oracle location missing")
                line = tests.count("\n",0,pos)+1
                col = pos-tests.rfind("\n",0,pos)
                location = "tests.rs:"+str(line)+":"+str(col)+":"
                require(result.returncode == 101 and marker in text and location in text
                        and "test result: FAILED. 0 passed; 1 failed;" in text,
                        "control failed for unexpected reason: " + label)
                row["assertion_location"] = location
            else:
                require(result.returncode == 0 and "test result: ok. 1 passed; 0 failed;" in text,
                        "positive test failed: " + test)
            row["tests"].append({"test":test,"exit":result.returncode,"log_sha256":sha(log.read_bytes())})
        summary["runs"].append(row)
        receipt.write_text(json.dumps(summary,indent=2,sort_keys=True)+"\n")
    # Verify the prepared and public bytes did not move under any compilation.
    require(all((HERE/n).read_bytes() == b for n,b in inputs.items()), "public input changed during run")
    require(all(sha((output/"tree"/n).read_bytes()) == h for n,h in summary["candidate_files_sha256"].items()),
            "prepared source changed during run")
    logs = {p.relative_to(output).as_posix():sha(p.read_bytes())
            for p in output.rglob("*.log") if "target" not in p.relative_to(output).parts}
    summary.update(status="passed", logs_sha256=logs)
    receipt.write_text(json.dumps(summary,indent=2,sort_keys=True)+"\n")
    print("PASS: 8 positive selectors; 4 compiled strict controls; actual crate check:",
          "skipped (independent host repeat)" if args.host_only else "passed", flush=True)
    print("Private receipt:",receipt,flush=True)

if __name__ == "__main__":
    try:
        main()
    except BaseException as error:
        if ACTIVE_RECEIPT is not None and ACTIVE_RECEIPT.exists():
            failed = json.loads(ACTIVE_RECEIPT.read_text())
            failed.update(status="failed", failure=type(error).__name__+": "+str(error))
            ACTIVE_RECEIPT.write_text(json.dumps(failed,indent=2,sort_keys=True)+"\n")
        raise

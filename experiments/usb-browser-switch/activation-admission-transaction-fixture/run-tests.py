#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""New offline C admission/transaction matrix only. Never invokes SDK or device tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import uuid

HERE = Path(__file__).resolve().parent
BASELINE = "3f9b8078e090f7b688d1902b261653a0fb130548"
SOURCES = ("run-tests.py", "transaction.h", "transaction.c", "test_transaction.c", "test-contract.json")
PINS = {"README.md": "ac0b902286ddb119674e311f53c80d035a6d3b361029081ec67e84843b5d6a8b",
        "verification.json": "491ed48e8075b34455abf889379d14070cbceac1fb0e9c46ab4dbd30aba56026"}
COMMON = ["-std=c17", "-mcpu=generic", "-march=armv8-a", "-O1", "-g",
          "-fno-omit-frame-pointer", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-pthread"]

def digest(data):
    return hashlib.sha256(data).hexdigest()

def encode(obj):
    return (json.dumps(obj, indent=2, sort_keys=True) + "\n").encode()

def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)

def main():
    if not __debug__:
        raise SystemExit("optimized Python unsupported")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="/usr/bin/clang")
    parser.add_argument("--modes", nargs="+", choices=("plain", "asan-ubsan"),
                        default=["plain", "asan-ubsan"])
    parser.add_argument("--selectors", nargs="+")
    parser.add_argument("--controls", nargs="+")
    parser.add_argument("--sections", nargs="+", choices=("standalone", "candidate", "controls"),
                        default=["standalone", "candidate", "controls"])
    args = parser.parse_args()
    contract = json.loads((HERE / "test-contract.json").read_bytes())
    selectors, controls = contract["selectors"], contract["controls"]
    selected = args.selectors if args.selectors is not None else list(selectors)
    selected_controls = args.controls if args.controls is not None else list(controls)
    for values, allowed in ((args.modes, ("plain", "asan-ubsan")),
                            (args.sections, ("standalone", "candidate", "controls")),
                            (selected, selectors), (selected_controls, controls)):
        if not values or len(values) != len(set(values)) or not set(values) <= set(allowed):
            parser.error("invalid or repeated selection")
    for checks in selectors.values():
        if not checks or len(checks) != len(set(checks)) or not all(type(x) is str for x in checks):
            parser.error("invalid named checks")
    for control in controls.values():
        if (set(control) != {"macro", "selector", "expected_failures"} or
            control["selector"] not in selectors or not control["macro"].startswith("TX_CONTROL_") or
            not control["macro"].replace("_", "").isalnum() or
            not control["expected_failures"] or
            len(control["expected_failures"]) != len(set(control["expected_failures"])) or
            not set(control["expected_failures"]) <= set(selectors[control["selector"]])):
            parser.error("invalid control contract")
    if len({c["macro"] for c in controls.values()}) != len(controls):
        parser.error("duplicate control macros")
    run = HERE / "private" / ("run-" + uuid.uuid4().hex)
    run.mkdir(parents=True)
    (run / "tmp").mkdir()
    environment = {"PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "LANG": "C", "LC_ALL": "C",
                   "TMPDIR": str(run / "tmp")}
    inputs, commands, artifacts = {}, [], {}
    result = {"schema": 1, "status": "running", "baseline_commit": BASELINE,
              "scope": "Authored C admission, pthread schedules and synthetic resource/installer outcomes only",
              "modes": args.modes, "sections": args.sections, "selectors": selected,
              "control_names": selected_controls, "candidate": {}, "controls": {},
              "common_flags": COMMON, "runtime_gate": "CLOSED",
              "sdk_or_device_execution": False, "old_suites_replayed": False,
              "production_integration": False, "codegen_or_signal_proof": False}
    def retain(name, raw, origin):
        save(run / "inputs" / name, raw)
        inputs[name] = {"bytes": len(raw), "sha256": digest(raw), "origin": origin}
    def record_artifact(path):
        raw = path.read_bytes()
        artifacts[str(path.relative_to(run))] = {"bytes": len(raw), "sha256": digest(raw)}
    def command(name, argv, env_add=None, timeout=45):
        folder = run / "commands" / ("%03d-" % (len(commands)+1) + name)
        spec = {"argv": [str(x) for x in argv], "cwd": str(run),
                "environment": dict(environment, **(env_add or {})), "timeout_seconds": timeout}
        save(folder / "command.json", encode(spec))
        proc, rc, failure, out, err = None, None, None, b"", b""
        try:
            proc = subprocess.Popen(spec["argv"], cwd=spec["cwd"], env=spec["environment"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    start_new_session=True)
            out, err = proc.communicate(timeout=timeout)
            rc = proc.returncode
        except BaseException as exc:
            failure = type(exc).__name__ + ": " + str(exc)
            if isinstance(exc, subprocess.TimeoutExpired):
                out, err = exc.stdout or b"", exc.stderr or b""
            if proc is not None:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                except BaseException as cleanup:
                    failure += "; cleanup: " + repr(cleanup)
                try:
                    out, err = proc.communicate(timeout=5)
                    rc = proc.returncode
                except BaseException as drain:
                    if isinstance(drain, subprocess.TimeoutExpired):
                        out, err = drain.stdout or out, drain.stderr or err
                    failure += "; drain: " + repr(drain)
        save(folder / "stdout.bin", out)
        save(folder / "stderr.bin", err)
        item = {"name": name, "directory": folder.name, **spec, "returncode": rc, "failure": failure,
                "stdout_bytes": len(out), "stderr_bytes": len(err),
                "stdout_sha256": digest(out), "stderr_sha256": digest(err)}
        save(folder / "receipt.json", encode(item))
        item["receipt_sha256"] = digest((folder / "receipt.json").read_bytes())
        commands.append(item)
        if failure:
            raise RuntimeError("command incomplete: " + name)
        return rc, out, err
    def report(name, exe, selector, expected, env):
        rc, out, err = command(name, [exe, selector], env)
        if err:
            raise RuntimeError("unexpected stderr: " + name)
        parsed = json.loads(out)
        if (set(parsed) != {"selector", "checks", "check_names", "failures", "failed_checks"} or
            parsed["selector"] != selector or type(parsed["checks"]) is not int or
            type(parsed["failures"]) is not int or
            parsed["check_names"] != selectors[selector] or
            parsed["checks"] != len(selectors[selector]) or
            parsed["failed_checks"] != expected or parsed["failures"] != len(expected) or
            rc != (1 if expected else 0)):
            raise RuntimeError("exact report oracle differs: " + name)
        return parsed
    try:
        for name in SOURCES:
            retain(name, (HERE / name).read_bytes(), "new-offline-C-fixture")
        if json.loads((run / "inputs" / "test-contract.json").read_bytes()) != contract:
            raise RuntimeError("test contract changed before capture")
        for name, expected in PINS.items():
            raw = (HERE.parent / "activation-generated-range-integration-review" / name).read_bytes()
            if digest(raw) != expected:
                raise RuntimeError("frozen integration input changed: " + name)
            retain("integration/" + name, raw, BASELINE)
        base = run / "candidate"
        for name in ("transaction.h", "transaction.c", "test_transaction.c"):
            save(base / name, (run / "inputs" / name).read_bytes())
        rc, version, err = command("compiler-version", [args.compiler, "--version"])
        if rc or err or b"Target: arm64-apple-" not in version:
            raise RuntimeError("Apple ARM64 compiler provenance failed")
        result["compiler_version_sha256"] = digest(version)
        if "standalone" in args.sections:
            obj = base / "transaction-standalone.o"
            rc, out, err = command("compile-standalone-C",
                [args.compiler, *COMMON, "-c", base / "transaction.c", "-o", obj])
            if rc or out or err:
                raise RuntimeError("strict standalone C build failed")
            result["standalone_compile_only"] = True
        for mode in args.modes:
            flags, env = [*COMMON, "-DTX_FIXTURE=1"], {}
            if mode == "asan-ubsan":
                flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
                env = {"ASAN_OPTIONS": "abort_on_error=1:detect_leaks=0",
                       "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
            if "candidate" in args.sections:
                exe = base / ("transaction-" + mode)
                rc, out, err = command("compile-candidate-" + mode,
                    [args.compiler, *flags, base / "transaction.c", base / "test_transaction.c",
                     "-o", exe])
                if rc or out or err:
                    raise RuntimeError("strict candidate build failed: " + mode)
                result["candidate"][mode] = {}
                for selector in selected:
                    result["candidate"][mode][selector] = report(
                        "candidate-" + mode + "-" + selector, exe, selector, [], env)
            if "controls" in args.sections:
                result["controls"][mode] = {}
                for name in selected_controls:
                    spec = controls[name]
                    folder = run / "controls" / mode / name
                    # One selected authored control configuration; site count is source-reviewed.
                    for src in ("transaction.h", "transaction.c", "test_transaction.c"):
                        save(folder / src, (run / "inputs" / src).read_bytes())
                    exe = folder / "transaction"
                    rc, out, err = command("compile-control-" + mode + "-" + name,
                        [args.compiler, *flags, "-D" + spec["macro"] + "=1",
                         folder / "transaction.c", folder / "test_transaction.c", "-o", exe])
                    if rc or out or err:
                        raise RuntimeError("strict control build failed: " + name)
                    observed = report("control-" + mode + "-" + name, exe,
                                      spec["selector"], spec["expected_failures"], env)
                    result["controls"][mode][name] = {"configuration": spec, "report": observed}
        result["status"] = "passed"
    except BaseException as exc:
        result.update(status="failed", error=type(exc).__name__ + ": " + str(exc))
    for path in sorted(run.rglob("*")):
        if path.is_file() and path.relative_to(run).parts[0] not in ("inputs", "commands"):
            record_artifact(path)
    result.update(inputs=inputs, commands=commands, artifacts=artifacts)
    save(run / "result.json", encode(result))
    print(json.dumps({"status": result["status"], "run": run.name,
                      "result_sha256": digest((run / "result.json").read_bytes())}))
    return 0 if result["status"] == "passed" else 1

if __name__ == "__main__":
    raise SystemExit(main())

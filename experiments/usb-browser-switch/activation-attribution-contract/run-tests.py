#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run only the new offline attribution contract against exact private extracts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import signal
import sys
import uuid

HERE = Path(__file__).resolve().parent
SDK_COMMIT = "3e0c5cb010454228f4ee63226b60d060494eeaae"
SOURCE_SHA = "4b2b72e2b623dcb1ecb0ef108a473815adab45a516a6aa13adb6cce512b80866"
CAPTURE_SHA = "e22d851e0c72f080e05b8f7e0a5db5994f8f68b3fc6ae8eeb864aee248616bfd"
DEFAULT_CAPTURE = HERE.parent / "activation-unwind-policy/private/capture-4d7eb81ff4e04f6099c87eaa42bc66d3"
EXTENTS = {
    "cm_range_cb": (69129, 69728, "a13b13bb80cda80f3f7fdae33161788ff826b07d15937f2654b72536d780d814"),
    "cm_owner_of": (70446, 70609, "22f8016ac6a6e04f4f56bc7532f41368f7306db3a71eb9598de69a76cd68e6da"),
    "cm_blame_fault": (71830, 72792, "9e31f235353a169e46f366ec33488076829b9383bc0fd0c462b465a0ddef70b4"),
}
# Reviewed exact selector counts and safe negative-control failure oracles.
SELECTORS = {'anonymous-miss': 5,
 'pc-precedence': 7,
 'lr-precedence': 7,
 'frame-hit': 7,
 'range-envelope': 12,
 'frame-first-read-failure': 5,
 'frame-second-read-failure': 5,
 'frame-limit-hit': 7,
 'frame-limit-miss': 5,
 'invalid-low-frame': 5,
 'invalid-unaligned-frame': 5,
 'nonincreasing-frame': 7,
 'first-call-guard': 4,
 'open-failure': 6,
 'write-failure': 8,
 'write-short': 8,
 'fsync-close-failure': 8}
CONTROLS = {'inclusive-upper-bound': {'before': 'a < g_cm[i].hi',
                           'after': 'a <= g_cm[i].hi',
                           'selector': 'range-envelope',
                           'expected_failures': ['excluded-boundaries']},
 'lr-before-pc': {'before': 'int who = cm_owner_of(pc);\n    if (who < 0) who = cm_owner_of(lr);',
                  'after': 'int who = cm_owner_of(lr);\n    if (who < 0) who = cm_owner_of(pc);',
                  'selector': 'pc-precedence',
                  'expected_failures': ['owner', 'write-payload']},
 'ignore-second-read-failure': {'before': 'if (!crash_peek8(fp, &nfp) || !crash_peek8(fp + 8, '
                                          '&ra)) break;',
                                'after': 'if (!crash_peek8(fp, &nfp)) break;\n'
                                         '        (void)crash_peek8(fp + 8, &ra);',
                                'selector': 'frame-second-read-failure',
                                'expected_failures': ['owner',
                                                      'peek-trace',
                                                      'unblamed-count',
                                                      'no-file-io']},
 'seventeenth-frame': {'before': 'i < 16 && fp >= 0x10000',
                       'after': 'i < 17 && fp >= 0x10000',
                       'selector': 'frame-limit-miss',
                       'expected_failures': ['owner',
                                             'peek-trace',
                                             'unblamed-count',
                                             'no-file-io']},
 'ignore-once-guard': {'before': 'if (written) return NULL;',
                       'after': 'if (written < 0) return NULL;',
                       'selector': 'first-call-guard',
                       'expected_failures': ['second-owner', 'second-no-side-effects']}}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def encoded(obj):
    return (json.dumps(obj, indent=2, sort_keys=True) + "\n").encode()

def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as output:
        output.write(data)

def main():
    if not __debug__:
        raise SystemExit("optimized Python is not supported")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, default=DEFAULT_CAPTURE)
    parser.add_argument("--compiler", default="/usr/bin/clang")
    parser.add_argument("--modes", nargs="+", choices=("plain", "asan-ubsan"),
                        default=["plain", "asan-ubsan"])
    parser.add_argument("--sections", nargs="+", choices=("candidate", "controls"),
                        default=["candidate", "controls"])
    parser.add_argument("--selectors", nargs="+")
    parser.add_argument("--control-names", nargs="+")
    args = parser.parse_args()
    if not SELECTORS or not CONTROLS:
        parser.error("selector/control contract has not been finalized")
    if len(set(args.modes)) != len(args.modes) or len(set(args.sections)) != len(args.sections):
        parser.error("duplicate mode/section")
    selected = args.selectors if args.selectors is not None else list(SELECTORS)
    selected_controls = args.control_names if args.control_names is not None else list(CONTROLS)
    if not selected or not set(selected) <= set(SELECTORS) or len(set(selected)) != len(selected):
        parser.error("invalid or duplicate selector")
    if (not selected_controls or not set(selected_controls) <= set(CONTROLS)
            or len(set(selected_controls)) != len(selected_controls)):
        parser.error("invalid or duplicate control")
    run = HERE / "private" / ("run-" + uuid.uuid4().hex)
    run.mkdir(parents=True)
    commands, inputs, artifacts = [], {}, {}
    result = {"schema": 1, "status": "running", "sdk_commit": SDK_COMMIT,
              "scope": "Three exact SDK attribution functions with synthetic owned records and modeled reads/I/O; fresh process selectors.",
              "baseline_commit": "0aecf22d00258d66bdfcccf7fe450452c1f3a725",
              "modes": args.modes, "sections": args.sections,
              "selectors": selected, "control_names": selected_controls,
              "old_suites_replayed": False, "native_hook_or_device_access": False,
              "production_registry_or_repair": False, "runtime_gate": "CLOSED",
              "candidate": {}, "controls": {}}
    def retain(name, data, origin):
        save(run / "inputs" / name, data)
        inputs[name] = {"bytes": len(data), "sha256": sha(data), "origin": origin}
    def artifact(path):
        data = path.read_bytes()
        artifacts[str(path.relative_to(run))] = {"bytes": len(data), "sha256": sha(data)}
    def command(name, argv, env_add=None, timeout=45):
        folder = run / "commands" / ("%03d-" % (len(commands)+1) + name)
        spec = {"argv": [str(x) for x in argv], "env_add": env_add or {},
                "timeout_seconds": timeout}
        save(folder / "command.json", encoded(spec))
        failure = None
        proc = None
        stdout, stderr, rc = b"", b"", None
        try:
            proc = subprocess.Popen(spec["argv"], stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, start_new_session=True,
                                    env=dict(os.environ, **spec["env_add"]))
            stdout, stderr = proc.communicate(timeout=timeout)
            rc = proc.returncode
        except BaseException as exc:
            failure = type(exc).__name__ + ": " + str(exc)
            if isinstance(exc, subprocess.TimeoutExpired):
                stdout, stderr = exc.stdout or b"", exc.stderr or b""
            if proc is not None:
                # Only the new group created for this owned command is stopped.
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                except BaseException as cleanup:
                    failure += "; group stop: " + type(cleanup).__name__ + ": " + str(cleanup)
                try:
                    stdout, stderr = proc.communicate(timeout=5)
                    rc = proc.returncode
                except subprocess.TimeoutExpired as drain:
                    stdout, stderr = drain.stdout or stdout, drain.stderr or stderr
                    failure += "; output drain timed out"
                except BaseException as cleanup:
                    failure += "; output drain: " + type(cleanup).__name__ + ": " + str(cleanup)
        save(folder / "stdout.bin", stdout)
        save(folder / "stderr.bin", stderr)
        item = {"name": name, "directory": folder.name, **spec, "returncode": rc,
                "failure": failure, "stdout_bytes": len(stdout), "stderr_bytes": len(stderr),
                "stdout_sha256": sha(stdout), "stderr_sha256": sha(stderr)}
        save(folder / "receipt.json", encoded(item))
        item["receipt_sha256"] = sha((folder / "receipt.json").read_bytes())
        commands.append(item)
        if failure:
            raise RuntimeError("command did not complete: " + name)
        return rc, stdout, stderr
    def report(name, exe, selector, expected_failures, env_add):
        rc, out, err = command(name, [exe, selector], env_add)
        if err:
            raise RuntimeError("unexpected stderr: " + name)
        parsed = json.loads(out)
        if set(parsed) != {"selector", "checks", "failures", "failed_checks"}:
            raise RuntimeError("unexpected report schema: " + name)
        if (parsed["selector"] != selector or type(parsed["checks"]) is not int
                or parsed["checks"] != SELECTORS[selector]):
            raise RuntimeError("invalid selector/check count: " + name)
        if (type(parsed["failures"]) is not int or type(parsed["failed_checks"]) is not list
                or not all(type(item) is str for item in parsed["failed_checks"])):
            raise RuntimeError("invalid failure field type: " + name)
        if parsed["failed_checks"] != expected_failures or parsed["failures"] != len(expected_failures):
            raise RuntimeError("exact failure oracle differs: " + name)
        if rc != (1 if expected_failures else 0):
            raise RuntimeError("exit status differs: " + name)
        return parsed
    try:
        capture = args.capture.absolute()
        capture_raw = (capture / "result.json").read_bytes()
        if sha(capture_raw) != CAPTURE_SHA:
            raise RuntimeError("prior source capture differs")
        source = (capture / "inputs/sdk/src/azmod/community.c").read_bytes()
        if sha(source) != SOURCE_SHA:
            raise RuntimeError("pinned SDK source differs")
        retain("run-tests.py", Path(__file__).read_bytes(), "new-fixture")
        retain("test_attribution.c", (HERE / "test_attribution.c").read_bytes(), "new-fixture")
        retain("community.c", source, SDK_COMMIT)
        retain("source-capture-result.json", capture_raw, "prior-capture-no-replay")
        chunks = []
        result["extractions"] = {}
        for name, (start, end, digest) in EXTENTS.items():
            data = source[start:end]
            if sha(data) != digest or not data.endswith(b"\n}\n"):
                raise RuntimeError("exact source extent differs: " + name)
            chunks.append(data)
            result["extractions"][name] = {"start": start, "end": end,
                "bytes": len(data), "sha256": digest, "line": source[:start].count(b"\n")+1}
        generated = b"\n".join(chunks)
        base = run / "candidate"
        save(base / "pinned-attribution.inc", generated)
        artifact(base / "pinned-attribution.inc")
        save(base / "test_attribution.c", (run / "inputs/test_attribution.c").read_bytes())
        artifact(base / "test_attribution.c")
        rc, version, err = command("compiler-version", [args.compiler, "--version"])
        if rc or err:
            raise RuntimeError("compiler provenance failed")
        result["compiler_version_sha256"] = sha(version)
        for mode in args.modes:
            flags = ["-std=c11", "-O1", "-g", "-fno-omit-frame-pointer",
                     "-Wall", "-Wextra", "-Werror", "-Wpedantic"]
            env = {}
            if mode == "asan-ubsan":
                flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
                env = {"ASAN_OPTIONS": "abort_on_error=1:detect_leaks=0",
                       "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
            if "candidate" in args.sections:
                exe = base / ("attribution-" + mode)
                rc, out, err = command("compile-candidate-" + mode,
                    [args.compiler, *flags, base / "test_attribution.c", "-o", exe])
                if rc or out or err:
                    raise RuntimeError("strict candidate build failed: " + mode)
                artifact(exe)
                result["candidate"][mode] = {}
                for selector in selected:
                    result["candidate"][mode][selector] = report(
                        "candidate-" + mode + "-" + selector, exe, selector, [], env)
            if "controls" in args.sections:
                result["controls"][mode] = {}
                for name in selected_controls:
                    spec = CONTROLS[name]
                    folder = run / "controls" / mode / name
                    before, after = spec["before"].encode(), spec["after"].encode()
                    if generated.count(before) != 1:
                        raise RuntimeError("control mutation is not unique: " + name)
                    mutated = generated.replace(before, after, 1)
                    save(folder / "pinned-attribution.inc", mutated)
                    save(folder / "test_attribution.c", (run / "inputs/test_attribution.c").read_bytes())
                    artifact(folder / "pinned-attribution.inc")
                    artifact(folder / "test_attribution.c")
                    exe = folder / "attribution"
                    rc, out, err = command("compile-control-" + mode + "-" + name,
                        [args.compiler, *flags, folder / "test_attribution.c", "-o", exe])
                    if rc or out or err:
                        raise RuntimeError("strict control build failed: " + name)
                    artifact(exe)
                    observed = report("control-" + mode + "-" + name, exe,
                                      spec["selector"], spec["expected_failures"], env)
                    result["controls"][mode][name] = {"mutation": spec,
                        "mutated_include_sha256": sha(mutated), "report": observed}
        result["status"] = "passed"
    except BaseException as exc:
        result.update(status="failed", error=type(exc).__name__ + ": " + str(exc))
    # Include linker/debug companion outputs and partial build artifacts too.
    for path in sorted(run.rglob("*")):
        if path.is_file() and path.relative_to(run).parts[0] not in ("commands", "inputs"):
            artifact(path)
    result.update(inputs=inputs, commands=commands, artifacts=artifacts)
    save(run / "result.json", encoded(result))
    print(json.dumps({"status": result["status"], "run": run.name,
                      "result_sha256": sha((run / "result.json").read_bytes())}))
    return 0 if result["status"] == "passed" else 1

if __name__ == "__main__":
    raise SystemExit(main())

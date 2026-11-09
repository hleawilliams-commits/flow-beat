#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run only the new offline generated-range fixture; no SDK, device or old-suite execution."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import signal
import sys
import uuid

HERE = Path(__file__).resolve().parent
BASELINE = "53e0b13abab00828439939a5ef131d871faf988b"
SOURCE_FILES = ("run-tests.py", "registry.h", "registry.cpp", "test_registry.cpp")
CONTRACT_PINS = {'README.md': 'dac814afceb53820b0b7a86dc5339fd2a9ca3234b6e15d8ea64f59ba48f789f4', 'verification.json': '4cba4d56379d29feeddf4799eca123bd02489fdc98c65238bfffa10e5c3fba63'}
SELECTORS = {'emitted-variants': 7, 'interval-boundaries': 5, 'invalid-inputs': 6, 'reservation-conflicts': 5, 'capacity': 5, 'owner-custody': 4, 'cancel-stale-ticket': 5, 'modeled-prepublication-refusals': 11, 'publication-order': 4, 'postpublication-warnings': 4, 'nested-read': 2, 'held-writer-reader': 2, 'lookup-precedence': 3, 'core-fallback': 3, 'later-module-masking': 3, 'frame-composition': 4}
CONTROLS = {'inclusive-range-end': {'file': 'registry.cpp', 'before': 'address >= range.begin && address < range.end', 'after': 'address >= range.begin && address <= range.end', 'selector': 'interval-boundaries', 'expected_failures': ['island-instruction-literal-boundaries', 'neighbor-and-pool-slack', 'trampoline-literal-and-page-slack']}, 'stale-generation-cancel': {'file': 'registry.cpp', 'before': '    if (slot.generation != ticket.generation_)\n        return Error::InvalidTicket;\n', 'after': '', 'selector': 'capacity', 'expected_failures': ['cancel-reuses-capacity-with-new-generation', 'replacement-capacity-stays-bounded']}, 'owner-byte-not-preserved': {'file': 'registry.cpp', 'before': '            copied.id[i] = static_cast<char>(c);', 'after': "            copied.id[i] = i == 0 ? 'X' : static_cast<char>(c);", 'selector': 'owner-custody', 'expected_failures': ['owner-copied-before-source-mutation', 'modeled-init-failure-retains-published-owner', 'returned-identity-is-independent-copy', 'module-snapshot-copies-owner']}, 'module-first-bypassed': {'file': 'registry.cpp', 'before': '        if (contains(modules.modules_[i].range, address)) {', 'after': '        if (false && contains(modules.modules_[i].range, address)) {', 'selector': 'lookup-precedence', 'expected_failures': ['first-module-envelope-beats-generated', 'module-envelope-hole-policy-unchanged']}, 'publication-before-target-unlock': {'file': 'registry.cpp', 'before': '    slot->phase = Phase::Authorized;', 'after': '    slot->phase = Phase::Authorized;\n    slot->committed.store(1U, std::memory_order_release);', 'selector': 'publication-order', 'expected_failures': ['all-invisible-stages-before-unlock']}, 'island-literal-storage-omitted': {'file': 'registry.cpp', 'before': '    return {layout.island_instructions.begin, layout.island_literal.end};', 'after': '    return {layout.island_instructions.begin, layout.island_instructions.end};', 'selector': 'reservation-conflicts', 'expected_failures': ['prepared-overlap-is-invisible', 'cross-kind-range-conflict', 'one-emission-internal-overlap']}, 'core-as-community': {'file': 'registry.cpp', 'before': '        result.kind = payload.owner.kind == OwnerKind::Core\n                          ? MatchKind::Core : MatchKind::Community;', 'after': '        result.kind = MatchKind::Community;', 'selector': 'core-fallback', 'expected_failures': ['core-pc-continues-to-community-lr', 'core-pc-and-lr-continue-to-frame', 'core-never-becomes-community-blame']}, 'original-before-metadata': {'file': 'test_registry.cpp', 'before': '    const auto committed = r.commit(p);', 'after': '    original = true; // Deliberately incorrect modeled ordering, control only.\n    const auto committed = r.commit(p);', 'selector': 'publication-order', 'expected_failures': ['metadata-visible-before-original']}}

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
    parser.add_argument("--compiler", default="/usr/bin/clang++")
    parser.add_argument("--modes", nargs="+", choices=("plain", "asan-ubsan"),
                        default=["plain", "asan-ubsan"])
    parser.add_argument("--sections", nargs="+", choices=("codegen", "candidate", "controls"),
                        default=["codegen", "candidate", "controls"])
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
    (run / "tmp").mkdir()
    command_environment = {"PATH": "/usr/bin:/bin:/usr/sbin:/sbin",
                           "LANG": "C", "LC_ALL": "C", "TMPDIR": str(run / "tmp")}
    commands, inputs, artifacts = [], {}, {}
    result = {"schema": 1, "status": "running",
              "scope": "Synthetic owned metadata and modeled installer; ordinary-thread fixture only.",
              "baseline_commit": BASELINE,
              "modes": args.modes, "sections": args.sections,
              "selectors": selected, "control_names": selected_controls,
              "old_suites_replayed": False, "native_hook_or_device_access": False,
              "core_repair_or_native_adapter": False, "runtime_gate": "CLOSED",
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
                "environment": dict(command_environment, **(env_add or {})),
                "cwd": str(run), "timeout_seconds": timeout}
        save(folder / "command.json", encoded(spec))
        failure = None
        proc = None
        stdout, stderr, rc = b"", b"", None
        try:
            proc = subprocess.Popen(spec["argv"], stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, start_new_session=True,
                                    env=spec["environment"], cwd=spec["cwd"])
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
        for name in SOURCE_FILES:
            retain(name, (HERE / name).read_bytes(), "new-offline-fixture")
        result["contract_inputs"] = {}
        for name, expected in CONTRACT_PINS.items():
            raw = (HERE.parent / "activation-generated-range-contract" / name).read_bytes()
            if sha(raw) != expected:
                raise RuntimeError("contract input changed: " + name)
            retain("contract/" + name, raw, BASELINE)
            result["contract_inputs"][name] = expected
        base = run / "candidate"
        for name in SOURCE_FILES:
            if name != "run-tests.py":
                save(base / name, (run / "inputs" / name).read_bytes())
                artifact(base / name)
        rc, version, err = command("compiler-version", [args.compiler, "--version"])
        if rc or err or b"Target: arm64-apple-" not in version:
            raise RuntimeError("ARM64 Apple compiler provenance failed")
        result["compiler_version_sha256"] = sha(version)
        common = ["-std=c++17", "-mcpu=generic", "-march=armv8-a", "-O1", "-g", "-fno-omit-frame-pointer",
                  "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-pthread"]
        if "codegen" in args.sections:
            asm = base / "registry-arm64.s"
            rc, out, err = command("compile-atomic-codegen",
                [args.compiler, *common, "-S", base / "registry.cpp", "-o", asm])
            if rc or out or err:
                raise RuntimeError("strict codegen compile failed")
            artifact(asm)
            assembly = asm.read_text()
            loads = len(re.findall(r"(?m)^\s*ldar\s", assembly))
            stores = len(re.findall(r"(?m)^\s*stlr\s", assembly))
            def function_body(method):
                label = re.compile(r"(?m)^(_+ZNK?12az_generated8Registry6" + method + r"[^:\n]*):")
                matches = list(label.finditer(assembly))
                if len(matches) != 1:
                    raise RuntimeError("ambiguous/missing codegen function: " + method)
                start = matches[0].end()
                end = assembly.find(".cfi_endproc", start)
                if end < 0:
                    raise RuntimeError("missing function boundary: " + method)
                return matches[0].group(1), assembly[start:end]
            lookup_symbol, lookup_body = function_body("lookup")
            commit_symbol, commit_body = function_body("commit")
            calls = sorted(set(re.findall(r"(?m)^\s*b(?:l)?\s+([^\s;]+)", assembly)))
            helpers = [c for c in calls if re.search(
                r"__atomic|__sync|__aarch64_|__cxx_atomic|atomic_wait|pthread_mutex|os_unfair_lock", c)]
            lookup_loads = len(re.findall(r"(?m)^\s*ldar\s", lookup_body))
            commit_stores = len(re.findall(r"(?m)^\s*stlr\s", commit_body))
            if not lookup_loads or not commit_stores or helpers:
                raise RuntimeError("method-bound atomic codegen evidence missing or helper call")
            result["codegen"] = {"assembly_sha256": sha(asm.read_bytes()),
                "ldar_instruction_count": loads, "stlr_instruction_count": stores,
                "lookup_symbol": lookup_symbol, "lookup_ldar_count": lookup_loads,
                "commit_symbol": commit_symbol, "commit_stlr_count": commit_stores,
                "direct_branch_targets": calls, "rejected_helper_calls": helpers,
                "scope": "Plain standalone registry assembly only; not Linux ABI or signal-safety proof."}
            result["codegen_controls"] = {}
            codegen_mutations = {
                "relaxed-lookup": ("slot.committed.load(std::memory_order_acquire)",
                                   "slot.committed.load(std::memory_order_relaxed)", False, True),
                "relaxed-commit": ("slot->committed.store(1U, std::memory_order_release);",
                                   "slot->committed.store(1U, std::memory_order_relaxed);", True, False),
            }
            for name, (before, after, expected_load, expected_store) in codegen_mutations.items():
                folder = run / "codegen-controls" / name
                raw = (base / "registry.cpp").read_text()
                if raw.count(before) != 1:
                    raise RuntimeError("codegen control not unique: " + name)
                save(folder / "registry.cpp", raw.replace(before, after, 1).encode())
                save(folder / "registry.h", (base / "registry.h").read_bytes())
                asm_control = folder / "registry-arm64.s"
                rc, out, err = command("compile-codegen-control-" + name,
                    [args.compiler, *common, "-S", folder / "registry.cpp", "-o", asm_control])
                if rc or out or err:
                    raise RuntimeError("strict codegen control build failed: " + name)
                assembly = asm_control.read_text()
                _, lookup_body = function_body("lookup")
                _, commit_body = function_body("commit")
                observed_load = bool(re.search(r"(?m)^\s*ldar\s", lookup_body))
                observed_store = bool(re.search(r"(?m)^\s*stlr\s", commit_body))
                control_calls = sorted(set(re.findall(r"(?m)^\s*b(?:l)?\s+([^\s;]+)", assembly)))
                control_helpers = [c for c in control_calls if re.search(
                    r"__atomic|__sync|__aarch64_|__cxx_atomic|atomic_wait|pthread_mutex|os_unfair_lock", c)]
                if (observed_load, observed_store) != (expected_load, expected_store) or control_helpers:
                    raise RuntimeError("codegen control exact oracle differs: " + name)
                result["codegen_controls"][name] = {
                    "before": before, "after": after,
                    "lookup_has_ldar": observed_load, "commit_has_stlr": observed_store,
                    "rejected_helper_calls": control_helpers,
                    "assembly_sha256": sha(asm_control.read_bytes()),
                    "source_sha256": sha((folder / "registry.cpp").read_bytes()),
                    "executed": False,
                    "scope": "Compiled-only rejected memory-order variant; not a weak-memory runtime test."}
        for mode in args.modes:
            flags, env = list(common), {}
            if mode == "asan-ubsan":
                flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
                env = {"ASAN_OPTIONS": "abort_on_error=1:detect_leaks=0",
                       "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
            if "candidate" in args.sections:
                exe = base / ("registry-" + mode)
                rc, out, err = command("compile-candidate-" + mode,
                    [args.compiler, *flags, base / "registry.cpp", base / "test_registry.cpp", "-o", exe])
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
                    mutated_sha = None
                    for source_name in SOURCE_FILES:
                        if source_name == "run-tests.py":
                            continue
                        raw = (run / "inputs" / source_name).read_bytes()
                        if source_name == spec["file"]:
                            before, after = spec["before"].encode(), spec["after"].encode()
                            if raw.count(before) != 1:
                                raise RuntimeError("control mutation not unique: " + name)
                            raw = raw.replace(before, after, 1)
                            mutated_sha = sha(raw)
                        save(folder / source_name, raw)
                        artifact(folder / source_name)
                    if mutated_sha is None:
                        raise RuntimeError("control source absent")
                    exe = folder / "registry"
                    rc, out, err = command("compile-control-" + mode + "-" + name,
                        [args.compiler, *flags, folder / "registry.cpp", folder / "test_registry.cpp", "-o", exe])
                    if rc or out or err:
                        raise RuntimeError("strict control build failed: " + name)
                    artifact(exe)
                    observed = report("control-" + mode + "-" + name, exe,
                                      spec["selector"], spec["expected_failures"], env)
                    result["controls"][mode][name] = {"mutation": spec,
                        "mutated_source_sha256": mutated_sha, "report": observed}
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

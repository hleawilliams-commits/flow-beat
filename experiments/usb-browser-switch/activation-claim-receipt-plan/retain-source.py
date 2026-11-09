#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain a source-only claim/receipt composition plan from already captured inputs; no subprocesses."""
import argparse
import hashlib
import json
from pathlib import Path
import uuid

HERE = Path(__file__).resolve().parent

def digest(data):
    return hashlib.sha256(data).hexdigest()

def encode(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()

def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, required=True,
                        help="Existing retained integration-review inputs/sdk directory")
    parser.add_argument("--generated-root", type=Path, required=True, help="Existing retained receipt generated files")
    args = parser.parse_args()
    run = HERE / "private" / ("scope-" + uuid.uuid4().hex)
    run.mkdir(parents=True)
    result = {
        "schema": 1, "status": "retaining", "runtime_gate": "CLOSED",
        "scope": "Source-only community claim/receipt composition plan",
        "new_sdk_inputs": 0, "source_git_reads": 0, "child_commands": [],
        "compiler_make_installer_or_matrix_execution": False,
        "firmware_or_device_access": False, "old_capture_or_matrix_replay": False,
        "inputs": {}, "artifacts": {}
    }
    try:
        plan_bytes = (HERE / "composition-plan.json").read_bytes()
        plan = json.loads(plan_bytes)
        if plan["status"] != "specified-not-implemented" or plan["runtime_gate"] != "CLOSED":
            raise ValueError("plan gate")
        result["sdk_commit"] = plan["sdk_commit"]
        result["baseline_commit"] = plan["baseline_commit"]
        sources = {}
        def retain(name, data, origin):
            if name in result["inputs"] or Path(name).is_absolute() or ".." in Path(name).parts:
                raise ValueError("invalid or duplicate retained input")
            save(run / "inputs" / name, data)
            result["inputs"][name] = {
                "bytes": len(data), "sha256": digest(data), "origin": origin
            }
            sources[name] = data
        retain("composition-plan.json", plan_bytes, "new-source-only-plan")
        retain("retain-source.py", Path(__file__).read_bytes(), "new-retention-script")
        for spec in plan["sources"]:
            relative = Path(spec["path"])
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError("source path")
            if spec["root"] == "sdk":
                path = args.sdk_root / relative
            elif spec["root"] == "generated":
                path = args.generated_root / relative
            elif spec["root"] == "repository":
                path = HERE.parent / relative
            else:
                raise ValueError("source root")
            if path.is_symlink() or not path.is_file():
                raise ValueError("nonregular source")
            data = path.read_bytes()
            if digest(data) != spec["sha256"] or len(data) != spec["bytes"]:
                raise ValueError("pinned input changed: " + spec["name"])
            retain(spec["name"], data, str(path.resolve()))
        anchors, ids = [], set()
        for anchor in plan["anchors"]:
            if anchor["id"] in ids:
                raise ValueError("duplicate anchor")
            ids.add(anchor["id"])
            lines = sources[anchor["source"]].splitlines(keepends=True)
            first, last = anchor["first_line"], anchor["last_line"]
            if not (isinstance(first, int) and isinstance(last, int) and 1 <= first <= last <= len(lines)):
                raise ValueError("anchor bounds")
            excerpt = b"".join(lines[first - 1:last])
            anchors.append({**anchor, "bytes": len(excerpt), "sha256": digest(excerpt)})
        index = {
            "scope": "Line-delimited retained-byte anchors; no semantic or runtime proof",
            "anchors": anchors
        }
        data = encode(index)
        save(run / "anchor-index.json", data)
        result["artifacts"]["anchor-index.json"] = {"bytes": len(data), "sha256": digest(data)}
        result.update(status="retained", reused_inputs=len(plan["sources"]),
                      new_plan_inputs=2, anchors=len(anchors))
    except BaseException as exc:
        result.update(status="failed", error=type(exc).__name__ + ": " + str(exc))
    raw = encode(result)
    save(run / "result.json", raw)
    if result["status"] == "retained":
        public = {key: value for key, value in result.items() if key not in ("inputs", "artifacts")}
        public["inputs"] = {
            key: {"bytes": value["bytes"], "sha256": value["sha256"]}
            for key, value in result["inputs"].items()
        }
        public["artifacts"] = result["artifacts"]
        public["private_result_sha256"] = digest(raw)
        public["retained_files"] = len(result["inputs"]) + len(result["artifacts"]) + 1
        save(HERE / "result.json", encode(public))
    print(json.dumps({"status": result["status"], "run": run.name,
                      "private_result_sha256": digest(raw)}))
    return 0 if result["status"] == "retained" else 1

if __name__ == "__main__":
    raise SystemExit(main())

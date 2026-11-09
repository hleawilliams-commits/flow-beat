#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain a source-only fault-reader contract and pinned local bytes; never run child commands."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import uuid

# Do not resolve(): every ancestor is opened with O_NOFOLLOW below.
SCRIPT = Path(os.path.abspath(__file__))
HERE = SCRIPT.parent
DEFAULT_REPO = SCRIPT.parents[3]
DIRECTORY_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
FILE_FLAGS = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK
NAME = re.compile(r"[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encode(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def parts(name):
    if not isinstance(name, str) or not NAME.fullmatch(name):
        raise ValueError("invalid relative name")
    path = PurePosixPath(name)
    if path.is_absolute() or any(p in (".", "..") for p in name.split("/")):
        raise ValueError("invalid relative path component")
    return name.split("/")


def absolute_directory(path):
    path = Path(os.path.abspath(path))
    fd = os.open(path.anchor, DIRECTORY_FLAGS)
    try:
        for component in path.parts[1:]:
            next_fd = os.open(component, DIRECTORY_FLAGS, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        return fd
    except BaseException:
        os.close(fd)
        raise


def parent_fd(root_fd, name, create=False):
    components = parts(name)
    fd = os.dup(root_fd)
    try:
        for component in components[:-1]:
            if create:
                try:
                    os.mkdir(component, mode=0o700, dir_fd=fd)
                except FileExistsError:
                    pass
            next_fd = os.open(component, DIRECTORY_FLAGS, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        return fd, components[-1]
    except BaseException:
        os.close(fd)
        raise


def read_bytes(root_fd, name):
    parent, leaf = parent_fd(root_fd, name)
    fd = None
    try:
        fd = os.open(leaf, FILE_FLAGS, dir_fd=parent)
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode):
            raise ValueError("input is not a regular file")
        with os.fdopen(fd, "rb") as stream:
            fd = None
            data = stream.read()
            after = os.fstat(stream.fileno())
        fields = ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")
        if any(getattr(before, key) != getattr(after, key) for key in fields):
            raise ValueError("input changed during read")
        if len(data) != before.st_size:
            raise ValueError("input length changed during read")
        return data
    finally:
        if fd is not None:
            os.close(fd)
        os.close(parent)


def save(root_fd, name, data):
    parent, leaf = parent_fd(root_fd, name, create=True)
    fd = None
    try:
        fd = os.open(leaf, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=parent)
        with os.fdopen(fd, "wb") as stream:
            fd = None
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    finally:
        if fd is not None:
            os.close(fd)
        os.close(parent)


def inventory(root_fd):
    """Close over regular files only; never traverse symlinks or special files."""
    found = {}
    def visit(fd, prefix):
        for name in sorted(os.listdir(fd)):
            parts(name)
            full = prefix + name
            info = os.stat(name, dir_fd=fd, follow_symlinks=False)
            if stat.S_ISDIR(info.st_mode):
                child = os.open(name, DIRECTORY_FLAGS, dir_fd=fd)
                try:
                    visit(child, full + "/")
                finally:
                    os.close(child)
            elif stat.S_ISREG(info.st_mode):
                data = read_bytes(fd, name)
                found[full] = {"bytes": len(data), "sha256": digest(data)}
            else:
                raise ValueError("nonregular retained path: " + full)
    visit(root_fd, "")
    return found


def exists(root_fd, name):
    try:
        os.stat(name, dir_fd=root_fd, follow_symlinks=False)
        return True
    except FileNotFoundError:
        return False


def new_packet(here_fd):
    try:
        os.mkdir("private", mode=0o700, dir_fd=here_fd)
    except FileExistsError:
        pass
    private_fd = os.open("private", DIRECTORY_FLAGS, dir_fd=here_fd)
    try:
        run_name = "scope-" + uuid.uuid4().hex
        os.mkdir(run_name, mode=0o700, dir_fd=private_fd)
        return run_name, os.open(run_name, DIRECTORY_FLAGS, dir_fd=private_fd)
    finally:
        os.close(private_fd)


def retain(run_fd, here_fd, repo_fd, repo_root, result):
    plan_bytes = read_bytes(here_fd, "operating-contract.json")
    plan = json.loads(plan_bytes)
    if plan["status"] != "source-reviewed-not-integrated" or plan["runtime_gate"] != "CLOSED":
        raise ValueError("plan gate")
    for key in ("sdk_commit", "baseline_commit"):
        value = plan[key]
        if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{40}", value):
            raise ValueError("invalid commit pin: " + key)
        result[key] = value
    if not isinstance(plan["sources"], list) or not isinstance(plan["anchors"], list):
        raise ValueError("source and anchor lists required")
    sources = {}
    declared = {"operating-contract.json", "retain-source.py", "README.md"}
    for spec in plan["sources"]:
        name = spec["name"]
        parts(name)
        parts(spec["path"])
        if name in declared:
            raise ValueError("duplicate input name: " + name)
        if not isinstance(spec["sha256"], str) or not SHA256.fullmatch(spec["sha256"]):
            raise ValueError("invalid source digest: " + name)
        if type(spec["bytes"]) is not int or spec["bytes"] < 0:
            raise ValueError("invalid source length: " + name)
        declared.add(name)
    for name in declared:
        if any(name.startswith(other + "/") for other in declared if other != name):
            raise ValueError("input file/directory name conflict")

    def snapshot(name, data, origin):
        save(run_fd, "inputs/" + name, data)
        result["inputs"][name] = {"bytes": len(data), "sha256": digest(data), "origin": origin}
        sources[name] = data

    snapshot("operating-contract.json", plan_bytes, str(HERE / "operating-contract.json"))
    snapshot("retain-source.py", read_bytes(here_fd, "retain-source.py"), str(SCRIPT))
    snapshot("README.md", read_bytes(here_fd, "README.md"), str(HERE / "README.md"))
    for spec in plan["sources"]:
        data = read_bytes(repo_fd, spec["path"])
        if len(data) != spec["bytes"] or digest(data) != spec["sha256"]:
            raise ValueError("pinned input changed: " + spec["name"])
        snapshot(spec["name"], data, str(repo_root / spec["path"]))

    anchors, ids = [], set()
    for anchor in plan["anchors"]:
        if set(anchor) != {"id", "source", "first_line", "last_line", "reason"}:
            raise ValueError("unexpected anchor fields")
        if not isinstance(anchor["reason"], str) or not anchor["reason"].strip():
            raise ValueError("anchor reason must be a nonempty string")
        identifier = anchor["id"]
        parts(identifier)
        if identifier in ids:
            raise ValueError("duplicate anchor id")
        ids.add(identifier)
        lines = sources[anchor["source"]].splitlines(keepends=True)
        first, last = anchor["first_line"], anchor["last_line"]
        if type(first) is not int or type(last) is not int or not (1 <= first <= last <= len(lines)):
            raise ValueError("anchor bounds: " + identifier)
        excerpt = b"".join(lines[first - 1:last])
        anchors.append({**anchor, "bytes": len(excerpt), "sha256": digest(excerpt)})
    anchor_bytes = encode({"scope": "Line-delimited retained bytes; no semantic or runtime proof", "anchors": anchors})
    save(run_fd, "anchor-index.json", anchor_bytes)
    result["artifacts"]["anchor-index.json"] = {"bytes": len(anchor_bytes), "sha256": digest(anchor_bytes)}
    expected = {"inputs/" + name: {key: value[key] for key in ("bytes", "sha256")}
                for name, value in result["inputs"].items()}
    expected.update(result["artifacts"])
    if inventory(run_fd) != expected:
        raise ValueError("retained file inventory is not closed")
    result.update(status="retained", reused_inputs=len(plan["sources"]),
                  new_plan_inputs=3, anchors=len(anchors), closed_inventory=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=DEFAULT_REPO)
    args = parser.parse_args()
    repo_root = Path(os.path.abspath(args.repo_root))
    here_fd = repo_fd = run_fd = None
    run_name = None
    try:
        here_fd = absolute_directory(HERE)
        repo_fd = absolute_directory(repo_root)
        if repo_root / "experiments/usb-browser-switch/activation-fault-reader-contract" != HERE:
            raise ValueError("repository root does not contain this experiment")
        if exists(here_fd, "result.json"):
            raise FileExistsError("public result already exists; no overwrite or replay")
        run_name, run_fd = new_packet(here_fd)
        result = {
            "schema": 1, "status": "retaining", "plan_status": "source-reviewed-not-integrated",
            "runtime_gate": "CLOSED", "scope": "Source-only fault-reader operating-contract review",
            "new_sdk_inputs": 0, "source_git_reads": 0, "child_commands": [],
            "compiler_loader_installer_or_matrix_execution": False,
            "firmware_or_device_access": False, "old_capture_or_matrix_replay": False,
            "inputs": {}, "artifacts": {}, "closed_inventory": False
        }
        try:
            retain(run_fd, here_fd, repo_fd, repo_root, result)
        except BaseException as exc:
            result.update(status="failed", error=type(exc).__name__ + ": " + str(exc))
        # The inventory explicitly excludes its enclosing result. No self hash.
        result["inventory"] = inventory(run_fd)
        result["inventory_excludes"] = ["result.json"]
        result["retained_files"] = len(result["inventory"]) + 1
        raw = encode(result)
        save(run_fd, "result.json", raw)
        expected = dict(result["inventory"])
        expected["result.json"] = {"bytes": len(raw), "sha256": digest(raw)}
        if inventory(run_fd) != expected:
            raise ValueError("final private packet inventory is not closed")
        if result["status"] == "retained":
            public = {key: value for key, value in result.items() if key not in ("inputs", "inventory")}
            public["inputs"] = {name: {key: value[key] for key in ("bytes", "sha256")}
                                for name, value in result["inputs"].items()}
            public["inventory"] = expected
            public["inventory_excludes"] = []
            public["private_result_sha256"] = digest(raw)
            save(here_fd, "result.json", encode(public))
        print(json.dumps({"status": result["status"], "run": run_name,
                          "private_result_sha256": digest(raw)}))
        return 0 if result["status"] == "retained" else 1
    except BaseException as exc:
        failure = {"status": "failed", "runtime_gate": "CLOSED",
                   "error": type(exc).__name__ + ": " + str(exc), "run": run_name,
                   "scope": "Retention or publication failure; no runtime execution"}
        if run_fd is not None:
            try:
                save(run_fd, "retention-failure-" + uuid.uuid4().hex + ".json", encode(failure))
            except BaseException as retain_error:
                failure["failure_record_error"] = type(retain_error).__name__ + ": " + str(retain_error)
        print(json.dumps(failure), file=sys.stderr)
        return 1
    finally:
        for fd in (run_fd, repo_fd, here_fd):
            if fd is not None:
                os.close(fd)


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Apply the offline candidate to fresh copies of exact local module sources."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASE_COMMIT = "06fc981707e69982c0554bb508e0c646c005a979"
MODULE = Path("guest/modules/subucom_virt")
PINS = {
    "subucom_virt.c": "9631ca05810408eb9cd40cb38fa3f90d8dd2ac1d4d09a2e73d084ea0dbe03654",
    "subucom_frames.h": "4d9d833e583b7c6a5aa3f12de0fea2b7245e9dac915477ab96b9cfdfa75c2c0b",
    "Makefile": "a85021fa4fb059ccbb89c3c2db6659720bcf8dfd80e826831b2363474f2e1be6",
}
PATCH_SHA256 = "34d870df06f902c04eb0c2029a1c0411e647519ff4a8fabdc17b16ffe8befb3f"
CANDIDATE_PINS = {
    **PINS,
    "subucom_virt.c": "09b73c132a245a46e9855888c8cb1ea0fac5e9adba195ed848983b71262a2180",
    "subucom_ownership.h": "77d0cb71414ec49750040cc22dd93f6553c418cee54f091aa6e4d23686ee43cc",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def prepare(emu, output):
    emu, output = Path(emu).resolve(), Path(output).resolve()
    if output.exists():
        raise ValueError("output must be a new directory")
    # This tool has no runtime path, network operation, installer or cleanup.
    revision = subprocess.check_output(
        ["git", "-C", str(emu), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != BASE_COMMIT:
        raise ValueError("emulator revision differs from reviewed base")
    sources = {name: (emu / MODULE / name).read_bytes() for name in PINS}
    for name, data in sources.items():
        if sha(data) != PINS[name]:
            raise ValueError("source drift: " + name)
    patch = (HERE / "emulator-overlay-ownership.patch").read_bytes()
    if sha(patch) != PATCH_SHA256:
        raise ValueError("patch differs from reviewed candidate")
    # Complete all provenance checks before creating the new output.
    target = output / MODULE
    target.mkdir(parents=True, exist_ok=False)
    for name, data in sources.items():
        (target / name).write_bytes(data)
    result = subprocess.run(
        ["patch", "--batch", "--forward", "--fuzz=0", "-p1"],
        cwd=output, input=patch, capture_output=True, check=False,
    )
    (output / "applied.patch").write_bytes(patch)
    (output / "apply.log").write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError("patch failed; retained fresh output for diagnosis")
    actual = {p.name: sha(p.read_bytes()) for p in sorted(target.iterdir())
              if p.is_file()}
    if actual != CANDIDATE_PINS:
        raise ValueError("applied output differs from exact candidate pins")
    receipt = {
        "base_commit": revision,
        "base_sha256": PINS,
        "patch_sha256": PATCH_SHA256,
        "candidate_sha256": actual,
        "scope": "fresh source copy only; no module build, install or runtime action",
    }
    (output / "prepare-result.json").write_text(
        json.dumps(receipt, sort_keys=True, indent=2) + "\n"
    )
    return receipt


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emu", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(prepare(args.emu, args.output), sort_keys=True, indent=2))

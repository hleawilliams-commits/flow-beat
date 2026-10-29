#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Export pinned Git blobs and apply three captured patches in a fresh tree."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE = Path('guest/modules/subucom_virt')
PARENT_SHA = 'd3590a456624e0c2f6530d52af4d176cf56a3e57e2c4741bf025d933779e0d34'
PINS_SHA = '7410374d7690548b217763ee2ec2f260dd07b1319897e6560e624ff0f4791b2d'
PATCH_SHA = 'fdf40df07b76e22f1684186e4d725ccc5d1cc56e24d5cb9a7f7813534a045352'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def captured(path, expected):
    data = path.read_bytes()
    if sha(data) != expected:
        raise ValueError('input drift: ' + str(path))
    return data


def prepare(emu, output):
    emu, output = Path(emu).resolve(), Path(output).resolve()
    if output.exists():
        raise ValueError('output must be a new directory')
    parent_bytes = captured(HERE / 'parent-inputs.json', PARENT_SHA)
    pins_bytes = captured(HERE / 'candidate-pins.json', PINS_SHA)
    parent, pins = json.loads(parent_bytes), json.loads(pins_bytes)
    # Export the exact committed blobs, never the emulator working tree.
    sources = {}
    for name, expected in parent['base_sha256'].items():
        data = subprocess.check_output([
            'git', '-C', str(emu), 'show',
            parent['base_commit'] + ':' + str(MODULE / name)])
        if sha(data) != expected:
            raise ValueError('base blob drift: ' + name)
        sources[name] = data
    stages = [(captured(HERE.parent / s['path'], s['sha256']),
               s['output_sha256']) for s in parent['stages']]
    stages.append((captured(HERE / 'overlay-restore.patch', PATCH_SHA), pins))
    # Validate all inputs before writing; apply only the captured byte arrays.
    target = output / MODULE
    target.mkdir(parents=True, exist_ok=False)
    (output / 'parent-inputs.json').write_bytes(parent_bytes)
    (output / 'candidate-pins.json').write_bytes(pins_bytes)
    for name, data in sources.items():
        (target / name).write_bytes(data)
    applied = []
    for index, (patch, expected) in enumerate(stages, 1):
        (output / f'applied-{index}.patch').write_bytes(patch)
        result = subprocess.run(
            ['patch', '--batch', '--forward', '--fuzz=0', '-p1'],
            cwd=output, input=patch, capture_output=True)
        (output / f'apply-{index}.log').write_bytes(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('patch failed; retained fresh output')
        actual = {p.name: sha(p.read_bytes()) for p in sorted(target.iterdir())
                  if p.is_file()}
        if actual != expected:
            raise ValueError('applied output differs from exact pins')
        applied.append({'patch_sha256': sha(patch), 'output_sha256': actual})
    receipt = {'base_commit': parent['base_commit'],
               'base_sha256': parent['base_sha256'],
               'parent_inputs_sha256': sha(parent_bytes),
               'candidate_pins_sha256': sha(pins_bytes),
               'stages': applied, 'candidate_sha256': actual,
               'scope': 'fresh source export only; no build, install or endpoint access'}
    (output / 'prepare-result.json').write_text(
        json.dumps(receipt, sort_keys=True, indent=2) + '\n')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emu', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.emu, args.output), sort_keys=True, indent=2))

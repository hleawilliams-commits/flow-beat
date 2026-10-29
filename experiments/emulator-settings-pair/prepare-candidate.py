#!/usr/bin/env python3
"""Prepare an inactive settings-pair successor in a fresh private tree only."""
import argparse
import hashlib
import json
from pathlib import Path
import types

HERE = Path(__file__).resolve().parent
INPUTS = ('settings_pair.rs', 'settings_pair_tests.rs', 'prepare-candidate.py',
          'parent-inputs.json', 'candidate-pins.json')

def sha(data):
    return hashlib.sha256(data).hexdigest()

def prepare(emu_source):
    captured = {n: (HERE/n).read_bytes() for n in INPUTS}
    parent_pins = json.loads(captured['parent-inputs.json'])
    frozen = {n: (HERE.parent/n).read_bytes() for n in parent_pins}
    assert all(sha(frozen[n]) == h for n,h in parent_pins.items())
    parent = HERE.parent/'emulator-owned-exit'
    helper = types.ModuleType('frozen_owned_exit_prepare')
    helper.__file__ = str(parent/'prepare-candidate.py')
    raw = frozen['emulator-owned-exit/prepare-candidate.py'].decode()
    # Change only the fresh-output location. All pinned exports, four patch
    # applications and intermediate inventories execute unchanged.
    anchor = "private = HERE/'private'"
    assert raw.count(anchor) == 1
    helper.OUTPUT_ROOT = HERE/'private'
    exec(compile(raw.replace(anchor, 'private = OUTPUT_ROOT'), helper.__file__, 'exec'), helper.__dict__)
    output = helper.prepare(emu_source.resolve())
    tree = output/'tree'
    for n,d in captured.items():
        dest = output/'inputs'/n
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(d)
    for n,d in frozen.items():
        dest = output/'frozen'/n
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(d)
    baseline = json.loads(frozen['emulator-owned-exit/candidate-pins.json'])['files']
    original = {n:(tree/n).read_bytes() for n in baseline}
    assert all(sha(d)==baseline[n] for n,d in original.items())
    main = 'az-preview/src/main.rs'
    text = original[main].decode()
    anchor = 'mod owned_lifecycle;'
    assert text.count(anchor)==1
    (tree/main).write_text(text.replace(anchor,anchor+'\n#[cfg(unix)]\nmod settings_pair;'))
    cargo = 'az-preview/Cargo.toml'
    text = original[cargo].decode()
    assert "[target.'cfg(unix)'.dependencies]" not in text
    (tree/cargo).write_text(text+'\n[target.\'cfg(unix)\'.dependencies]\nlibc = "=0.2.190"\n')
    lock = 'az-preview/Cargo.lock'
    text = original[lock].decode()
    anchor = 'name = "az-preview"\nversion = "0.1.0"\ndependencies = [\n "eframe",\n "egui",\n "image",\n'
    assert text.count(anchor)==1
    (tree/lock).write_text(text.replace(anchor,anchor+' "libc",\n'))
    for n in ('settings_pair.rs','settings_pair_tests.rs'):
        (tree/'az-preview/src'/n).write_bytes(captured[n])
    expected = set(baseline)|{'az-preview/src/settings_pair.rs','az-preview/src/settings_pair_tests.rs'}
    final = {n:sha((tree/n).read_bytes()) for n in sorted(expected)}
    assert json.loads(captured['candidate-pins.json']) == final
    changes = {n for n in baseline if baseline[n]!=final[n]}
    assert changes == {main,cargo,lock}
    assert all((HERE/n).read_bytes()==d for n,d in captured.items())
    assert all((HERE.parent/n).read_bytes()==d for n,d in frozen.items())
    receipt = {'parent_inputs_sha256':parent_pins,'inputs_sha256':{n:sha(d) for n,d in captured.items()},
               'candidate_files_sha256':final,'changed_parent_files':sorted(changes),
               'source_only_changes':'Inactive Unix module declaration and direct pinned libc dependency; no live construction or legacy settings changes.',
               'status':'verified'}
    (output/'settings-preparation.json').write_text(json.dumps(receipt,sort_keys=True,indent=2)+'\n')
    return output

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emu-source',type=Path,required=True)
    args=p.parse_args()
    print(prepare(args.emu_source))

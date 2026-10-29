#!/usr/bin/env python3
"""Prepare a pure inactive ACK mapper after the frozen settings-pair candidate."""
import argparse
import hashlib
import json
from pathlib import Path
import types

if not __debug__:
    raise RuntimeError('optimized Python disables frozen parent pin assertions')

HERE = Path(__file__).resolve().parent
INPUTS = ('settings_mapper.rs', 'settings_mapper_tests.rs', 'prepare-candidate.py',
          'parent-inputs.json', 'candidate-pins.json')

def sha(data):
    return hashlib.sha256(data).hexdigest()

def prepare(emu_source):
    captured = {n: (HERE/n).read_bytes() for n in INPUTS}
    pins = json.loads(captured['parent-inputs.json'])
    frozen = {n: (HERE.parent/n).read_bytes() for n in pins}
    assert all(sha(frozen[n]) == h for n,h in pins.items())
    parent = HERE.parent/'emulator-settings-pair'
    helper = types.ModuleType('frozen_settings_prepare')
    helper.__file__ = str(parent/'prepare-candidate.py')
    raw = frozen['emulator-settings-pair/prepare-candidate.py'].decode()
    anchor = "helper.OUTPUT_ROOT = HERE/'private'"
    assert raw.count(anchor) == 1
    helper.OUTPUT_ROOT = HERE/'private'
    exec(compile(raw.replace(anchor, 'helper.OUTPUT_ROOT = OUTPUT_ROOT'),
                 helper.__file__, 'exec'), helper.__dict__)
    output = helper.prepare(emu_source.resolve())
    tree = output/'tree'
    # Preserve the parent's captured inputs under their own namespace before
    # writing this successor's identically named preparation inputs.
    (output/'inputs').rename(output/'settings-pair-inputs')
    (output/'inputs').mkdir()
    for n,d in captured.items():
        (output/'inputs'/n).write_bytes(d)
    for n,d in frozen.items():
        dest = output/'mapper-parent-inputs'/n
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(d)
    baseline = json.loads(frozen['emulator-settings-pair/candidate-pins.json'])
    original = {n:(tree/n).read_bytes() for n in baseline}
    assert all(sha(d) == baseline[n] for n,d in original.items())
    main = 'az-preview/src/main.rs'
    text = original[main].decode()
    anchor = '#[cfg(unix)]\nmod settings_pair;'
    assert text.count(anchor) == 1
    (tree/main).write_text(text.replace(anchor, anchor+'\n#[cfg(unix)]\nmod settings_mapper;'))
    for n in ('settings_mapper.rs','settings_mapper_tests.rs'):
        (tree/'az-preview/src'/n).write_bytes(captured[n])
    expected = set(baseline)|{'az-preview/src/settings_mapper.rs','az-preview/src/settings_mapper_tests.rs'}
    final = {n:sha((tree/n).read_bytes()) for n in sorted(expected)}
    assert json.loads(captured['candidate-pins.json']) == final
    changes = {n for n in baseline if baseline[n]!=final[n]}
    assert changes == {main}
    assert all((HERE/n).read_bytes()==d for n,d in captured.items())
    assert all((HERE.parent/n).read_bytes()==d for n,d in frozen.items())
    receipt = {'parent_inputs_sha256':pins,
               'inputs_sha256':{n:sha(d) for n,d in captured.items()},
               'candidate_files_sha256':final,
               'changed_parent_files':sorted(changes),
               'source_only_changes':'Inactive Unix module declaration; no live construction, settings sink, dependencies or legacy helper changes.',
               'status':'verified'}
    (output/'mapper-preparation.json').write_text(json.dumps(receipt,sort_keys=True,indent=2)+'\n')
    return output

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emu-source',type=Path,required=True)
    args=p.parse_args()
    print(prepare(args.emu_source))

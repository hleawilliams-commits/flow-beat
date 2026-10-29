#!/usr/bin/env python3
"""Prepare an inactive bounded broker wire seam after frozen owned startup."""
import argparse
import hashlib
import json
from pathlib import Path
import types

if not __debug__:
    raise RuntimeError('optimized Python disables frozen parent pin assertions')

HERE = Path(__file__).resolve().parent
C_INPUTS = ('wire.h', 'wire.c', 'guest_dispatch.h', 'guest_dispatch.c',
            'wire_tests.c', 'fixture_peer.c')
INPUTS = ('broker_wire.rs', 'broker_wire_tests.rs', 'prepare-candidate.py',
          'parent-inputs.json', 'candidate-pins.json') + C_INPUTS

def sha(data):
    return hashlib.sha256(data).hexdigest()

def prepare(emu_source):
    captured = {n: (HERE/n).read_bytes() for n in INPUTS}
    pins = json.loads(captured['parent-inputs.json'])
    frozen = {n: (HERE.parent/n).read_bytes() for n in pins}
    assert len(pins) == 40
    assert all(sha(frozen[n]) == h for n,h in pins.items())
    parent = HERE.parent/'emulator-owned-startup'
    helper = types.ModuleType('frozen_startup_prepare')
    helper.__file__ = str(parent/'prepare-candidate.py')
    raw = frozen['emulator-owned-startup/prepare-candidate.py'].decode()
    anchor = "    helper.OUTPUT_ROOT = HERE/'private'\n"
    assert raw.count(anchor) == 1
    helper.OUTPUT_ROOT = HERE/'private'
    exec(compile(raw.replace(anchor, '    helper.OUTPUT_ROOT = OUTPUT_ROOT\n'),
                 helper.__file__, 'exec'), helper.__dict__)
    output = helper.prepare(emu_source.resolve())
    tree = output/'tree'
    (output/'inputs').rename(output/'owned-startup-inputs')
    (output/'inputs').mkdir()
    for n,d in captured.items():
        (output/'inputs'/n).write_bytes(d)
    for n,d in frozen.items():
        target = output/'wire-parent-inputs'/n
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(d)
    baseline = json.loads(frozen['emulator-owned-startup/candidate-pins.json'])
    assert len(baseline) == 28
    original = {n:(tree/n).read_bytes() for n in baseline}
    assert all(sha(d) == baseline[n] for n,d in original.items())
    main = 'az-preview/src/main.rs'
    text = original[main].decode()
    anchor = '#[cfg(unix)]\nmod owned_startup;'
    assert text.count(anchor) == 1
    (tree/main).write_text(text.replace(anchor, anchor+'\n#[cfg(unix)]\nmod broker_wire;'))
    for n in ('broker_wire.rs','broker_wire_tests.rs'):
        (tree/'az-preview/src'/n).write_bytes(captured[n])
    expected = set(baseline)|{'az-preview/src/broker_wire.rs','az-preview/src/broker_wire_tests.rs'}
    assert len(expected) == 30
    final = {n:sha((tree/n).read_bytes()) for n in sorted(expected)}
    assert json.loads(captured['candidate-pins.json']) == final
    changes = {n for n in baseline if baseline[n]!=final[n]}
    assert changes == {main}
    wire = output/'wire-src'
    wire.mkdir()
    for n in C_INPUTS:
        (wire/n).write_bytes(captured[n])
    for n in ('broker.h','broker.c'):
        (wire/n).write_bytes(frozen['emulator-broker-restore/'+n])
    assert all((HERE/n).read_bytes()==d for n,d in captured.items())
    assert all((HERE.parent/n).read_bytes()==d for n,d in frozen.items())
    receipt = {'parent_inputs_sha256':pins,
               'inputs_sha256':{n:sha(d) for n,d in captured.items()},
               'candidate_files_sha256':final,
               'wire_sources_sha256':{p.name:sha(p.read_bytes()) for p in sorted(wire.iterdir())},
               'changed_parent_files':sorted(changes),
               'source_only_changes':'Inactive Unix module declaration and bounded caller-supplied stream backend; separate C codec/borrowed broker dispatcher. No main wiring, endpoint, acquisition or dependency changes.',
               'status':'verified'}
    (output/'wire-preparation.json').write_text(json.dumps(receipt,sort_keys=True,indent=2)+'\n')
    return output

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emu-source',type=Path,required=True)
    args=p.parse_args()
    print(prepare(args.emu_source))

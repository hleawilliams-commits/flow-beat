#!/usr/bin/env python3
"""Prepare inactive retained worker startup after the frozen fixed session."""
import argparse
import hashlib
import json
from pathlib import Path
import types

if not __debug__:
    raise RuntimeError('optimized Python disables frozen parent pin assertions')

HERE = Path(__file__).resolve().parent
INPUTS = ('owned_startup.rs', 'owned_startup_tests.rs', 'prepare-candidate.py',
          'parent-inputs.json', 'candidate-pins.json')

def sha(data):
    return hashlib.sha256(data).hexdigest()

def prepare(emu_source):
    captured = {n: (HERE/n).read_bytes() for n in INPUTS}
    pins = json.loads(captured['parent-inputs.json'])
    frozen = {n: (HERE.parent/n).read_bytes() for n in pins}
    assert len(pins) == 33
    assert all(sha(frozen[n]) == h for n,h in pins.items())
    parent = HERE.parent/'emulator-fixed-session'
    helper = types.ModuleType('frozen_fixed_prepare')
    helper.__file__ = str(parent/'prepare-candidate.py')
    raw = frozen['emulator-fixed-session/prepare-candidate.py'].decode()
    anchor = "    helper.OUTPUT_ROOT = HERE/'private'\n"
    assert raw.count(anchor) == 1
    helper.OUTPUT_ROOT = HERE/'private'
    exec(compile(raw.replace(anchor, '    helper.OUTPUT_ROOT = OUTPUT_ROOT\n'),
                 helper.__file__, 'exec'), helper.__dict__)
    output = helper.prepare(emu_source.resolve())
    tree = output/'tree'
    # Preserve the parent's captured inputs under their own namespace before
    # writing this successor's identically named preparation inputs.
    (output/'inputs').rename(output/'fixed-session-inputs')
    (output/'inputs').mkdir()
    for n,d in captured.items():
        (output/'inputs'/n).write_bytes(d)
    for n,d in frozen.items():
        dest = output/'startup-parent-inputs'/n
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(d)
    baseline = json.loads(frozen['emulator-fixed-session/candidate-pins.json'])
    assert len(baseline) == 26
    original = {n:(tree/n).read_bytes() for n in baseline}
    assert all(sha(d) == baseline[n] for n,d in original.items())
    main = 'az-preview/src/main.rs'
    text = original[main].decode()
    anchor = '#[cfg(unix)]\nmod fixed_session;'
    assert text.count(anchor) == 1
    (tree/main).write_text(text.replace(anchor, anchor+'\n#[cfg(unix)]\nmod owned_startup;'))
    transport = 'az-preview/src/mixer_transport.rs'
    helper_source = '\n#[cfg(test)]\npub(crate) fn fail_spawn_for_owned_startup(route: Arc<MixerRoute>, capacity: usize)\n    -> Result<(MixerPort, MixerWorker), TransportError> {\n    channel_with_spawn(route, capacity, |_job| {\n        Err(std::io::Error::other("injected owned-startup spawn refusal"))\n    })\n}\n'
    assert 'fail_spawn_for_owned_startup' not in original[transport].decode()
    (tree/transport).write_bytes(original[transport] + helper_source.encode())
    for n in ('owned_startup.rs','owned_startup_tests.rs'):
        (tree/'az-preview/src'/n).write_bytes(captured[n])
    expected = set(baseline)|{'az-preview/src/owned_startup.rs','az-preview/src/owned_startup_tests.rs'}
    assert len(expected) == 28
    final = {n:sha((tree/n).read_bytes()) for n in sorted(expected)}
    assert json.loads(captured['candidate-pins.json']) == final
    changes = {n for n in baseline if baseline[n]!=final[n]}
    assert changes == {main, transport}
    assert all((HERE/n).read_bytes()==d for n,d in captured.items())
    assert all((HERE.parent/n).read_bytes()==d for n,d in frozen.items())
    receipt = {'parent_inputs_sha256':pins,
               'inputs_sha256':{n:sha(d) for n,d in captured.items()},
               'candidate_files_sha256':final,
               'changed_parent_files':sorted(changes),
               'source_only_changes':'Inactive Unix module declaration, retained local worker startup, and a test-only actual spawn-error helper. No main wiring, guest startup, native acquisition or dependency changes.',
               'status':'verified'}
    (output/'startup-preparation.json').write_text(json.dumps(receipt,sort_keys=True,indent=2)+'\n')
    return output

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emu-source',type=Path,required=True)
    args=p.parse_args()
    print(prepare(args.emu_source))

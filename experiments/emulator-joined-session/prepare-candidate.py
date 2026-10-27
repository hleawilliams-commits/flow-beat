#!/usr/bin/env python3
"""Export pinned blobs and apply three inactive preview patches, offline.

Writes only a fresh ignored private tree. Does not build, run an application,
contact any endpoint or mutate the emulator checkout or frozen parent evidence.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import stat
import types
import uuid

HERE = Path(__file__).resolve().parent
COMMIT = '06fc981707e69982c0554bb508e0c646c005a979'
GATE = 'emulator-preview-gate/'
TRANSPORT = 'emulator-preview-transport/'

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(value, message):
    if not value:
        raise ValueError(message)

def regular(path):
    require(stat.S_ISREG(path.lstat().st_mode), 'input must be a regular file')
    return path.read_bytes()

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key')
        result[key] = value
    return result

def document(data):
    return json.loads(data, object_pairs_hook=unique_object)

def prepare(emu_source):
    dependency_pins = document(regular(HERE/'parent-inputs.json'))
    needed = {GATE+n for n in ('prepare-candidate.py', 'base-source-pins.json',
                               'candidate-pins.json', 'preview-gate.patch', 'Cargo.lock')}
    needed |= {TRANSPORT+n for n in ('candidate-pins.json', 'transport.patch')}
    require(set(dependency_pins) == needed, 'parent dependency inventory differs')
    parent = {n: regular(HERE.parent/n) for n in needed}
    require(all(sha(parent[n]) == h for n, h in dependency_pins.items()), 'frozen parent input drift')
    # This exact pinned helper defines safe Git exports/inventories and has no
    # import-time actions. Its prepare function is not called.
    helper = types.ModuleType('frozen_prepare')
    helper.__file__ = str(HERE.parent/GATE/'prepare-candidate.py')
    exec(compile(parent[GATE+'prepare-candidate.py'], helper.__file__, 'exec'), helper.__dict__)
    base_doc = document(parent[GATE+'base-source-pins.json'])
    require(base_doc['emulator_commit'] == COMMIT, 'base commit differs')
    base = helper.file_pins(base_doc['files'], helper.BASE_FILES)
    gate_doc = document(parent[GATE+'candidate-pins.json'])
    gate = helper.file_pins(gate_doc['files'], helper.BASE_FILES | helper.ADDED_FILES)
    require(sha(parent[GATE+'preview-gate.patch']) == gate_doc['patch_sha256'], 'gate patch mismatch')
    require(sha(parent[GATE+'Cargo.lock']) == gate_doc['cargo_lock_sha256'], 'gate lock mismatch')
    prior_doc = document(parent[TRANSPORT+'candidate-pins.json'])
    expected = set(gate) | {'az-preview/src/mixer_transport.rs'}
    prior = helper.file_pins(prior_doc['files'], frozenset(expected))
    require(sha(parent[TRANSPORT+'transport.patch']) == prior_doc['patch_sha256'], 'transport patch mismatch')
    require(sha(parent[GATE+'candidate-pins.json']) == prior_doc['parent_candidate_pins_sha256'],
            'transport parent pin mismatch')
    patch = regular(HERE/'joined-session.patch')
    final_doc = document(regular(HERE/'candidate-pins.json'))
    require(set(final_doc) == {'files', 'patch_sha256', 'parent_candidate_pins_sha256'}, 'candidate schema')
    require(final_doc['patch_sha256'] == sha(patch), 'successor patch mismatch')
    require(final_doc['parent_candidate_pins_sha256'] == sha(parent[TRANSPORT+'candidate-pins.json']),
            'successor parent pin mismatch')
    final = helper.file_pins(final_doc['files'], frozenset(expected))
    require(helper.git(['rev-parse', '--verify', COMMIT+'^{commit}'], emu_source).strip().decode() == COMMIT,
            'pinned commit unavailable')
    private = HERE/'private'
    private.mkdir(exist_ok=True)
    require(stat.S_ISDIR(private.lstat().st_mode), 'private output must be a real directory')
    output = private/('prepare-'+uuid.uuid4().hex)
    output.mkdir(mode=0o700, exist_ok=False)
    tree = output/'tree'
    tree.mkdir()
    receipt = {'status': 'started', 'emulator_commit': COMMIT,
               'parent_inputs_sha256': dependency_pins, 'patch_sha256': sha(patch)}
    try:
        helper.git(['-c', 'init.templateDir=', 'init', '--quiet'], tree)
        for name, digest in sorted(base.items()):
            entries = helper.git(['ls-tree', '-z', COMMIT, '--', name], emu_source).split(b'\0')
            require(len(entries) == 2 and entries[1] == b'', 'unexpected Git entry')
            metadata, path = entries[0].split(b'\t', 1)
            mode, kind, _ = metadata.split(b' ')
            require(path.decode() == name and mode in (b'100644', b'100755') and kind == b'blob',
                    'source is not a regular Git blob')
            data = helper.git(['show', COMMIT+':'+name], emu_source)
            require(sha(data) == digest, 'base blob mismatch: '+name)
            destination = tree/name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            destination.chmod(0o755 if mode == b'100755' else 0o644)
        require(helper.inventory(tree) == base, 'base inventory mismatch')
        stages = [('preview-gate.patch', parent[GATE+'preview-gate.patch'], gate),
                  ('transport.patch', parent[TRANSPORT+'transport.patch'], prior),
                  ('joined-session.patch', patch, final)]
        for name, data, pins in stages:
            saved = output/name
            saved.write_bytes(data)
            helper.git(['apply', '--check', str(saved)], tree)
            helper.git(['apply', str(saved)], tree)
            if name == 'preview-gate.patch':
                (tree/'az-preview/Cargo.lock').write_bytes(parent[GATE+'Cargo.lock'])
            require(helper.inventory(tree) == pins, 'candidate inventory mismatch after '+name)
        receipt.update(status='verified', files=final)
    except BaseException as error:
        receipt.update(status='failed', error=type(error).__name__+': '+str(error))
        (output/'preparation.json').write_text(json.dumps(receipt, sort_keys=True, indent=2)+'\n')
        raise
    (output/'preparation.json').write_text(json.dumps(receipt, sort_keys=True, indent=2)+'\n')
    return output

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emu-source', type=Path, required=True)
    args = parser.parse_args()
    print(prepare(args.emu_source.resolve()))

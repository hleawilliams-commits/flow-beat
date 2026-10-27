#!/usr/bin/env python3
"""Export pinned emulator blobs and apply the two inactive preview patches.

Only a fresh ignored private tree is written. No build, application, service,
backend, socket, remote Git or device operation is performed here.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import stat
import uuid

HERE = Path(__file__).resolve().parent
PARENT = HERE.parent/'emulator-preview-gate'
COMMIT = '06fc981707e69982c0554bb508e0c646c005a979'

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
    needed = {'prepare-candidate.py', 'base-source-pins.json', 'candidate-pins.json',
              'preview-gate.patch', 'Cargo.lock'}
    require(set(dependency_pins) == needed, 'parent dependency inventory differs')
    parent = {n: regular(PARENT/n) for n in needed}
    require(all(sha(parent[n]) == h for n, h in dependency_pins.items()), 'frozen parent input drift')
    # Load only the exact pinned local helper; it has no import-time actions.
    spec = importlib.util.spec_from_file_location('frozen_prepare', PARENT/'prepare-candidate.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    base_doc = document(parent['base-source-pins.json'])
    require(base_doc['emulator_commit'] == COMMIT, 'base commit differs')
    base = helper.file_pins(base_doc['files'], helper.BASE_FILES)
    prior_doc = document(parent['candidate-pins.json'])
    prior = helper.file_pins(prior_doc['files'], helper.BASE_FILES | helper.ADDED_FILES)
    require(sha(parent['preview-gate.patch']) == prior_doc['patch_sha256'], 'parent patch mismatch')
    require(sha(parent['Cargo.lock']) == prior_doc['cargo_lock_sha256'], 'parent lock mismatch')
    patch = regular(HERE/'transport.patch')
    final_doc = document(regular(HERE/'candidate-pins.json'))
    require(set(final_doc) == {'files', 'patch_sha256', 'parent_candidate_pins_sha256'}, 'candidate schema')
    require(final_doc['patch_sha256'] == sha(patch), 'successor patch mismatch')
    require(final_doc['parent_candidate_pins_sha256'] == sha(parent['candidate-pins.json']), 'parent pin mismatch')
    expected = set(prior) | {'az-preview/src/mixer_transport.rs'}
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
        old_patch = output/'parent.patch'
        old_patch.write_bytes(parent['preview-gate.patch'])
        helper.git(['apply', '--check', str(old_patch)], tree)
        helper.git(['apply', str(old_patch)], tree)
        (tree/'az-preview/Cargo.lock').write_bytes(parent['Cargo.lock'])
        require(helper.inventory(tree) == prior, 'parent candidate mismatch')
        new_patch = output/'transport.patch'
        new_patch.write_bytes(patch)
        helper.git(['apply', '--check', str(new_patch)], tree)
        helper.git(['apply', str(new_patch)], tree)
        actual = helper.inventory(tree)
        require(actual == final, 'successor candidate inventory/hash mismatch')
        receipt.update(status='verified', files=actual)
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

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a private source snapshot in an existing pinned, networkless ARM64 image."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import uuid

HERE = Path(__file__).resolve().parent
SDK_PIN = '6563e8e657a4ef113696de9d0f9327a6c8d0fab7'
IMAGE = 'az-mods-sdk:1'
IMAGE_ID = 'sha256:c96cac5682604faf32417c92f391ec233faec98d5e05d0de29f1bac0cfd2053e'
FILES = ['Makefile', 'azmod.json', 'mod.c', 'observer.cpp', 'icon.png', 'LICENSE',
         'frozen-inputs.json', 'abi/submission-entry.S', 'abi/executor-entry.S',
         'deps/task-capture-contract/capture.h', 'deps/task-value-transport/transport.h',
         'deps/task-call-return-abi/context.h', 'build.py']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--result', type=Path, required=True)
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=sdk, text=True).strip() == SDK_PIN
    sdk_files = ['include/azmod-mod.h', 'tools/azmod-pack', 'tools/azmod_pack.py']
    for name in sdk_files:
        assert (sdk / name).read_bytes() == subprocess.check_output(['git', 'show', SDK_PIN + ':' + name], cwd=sdk)
    image = subprocess.check_output(['docker', 'image', 'inspect', IMAGE, '--format', '{{.Id}} {{.Architecture}}'], text=True).strip()
    assert image == IMAGE_ID + ' arm64', image
    frozen = json.loads((HERE / 'frozen-inputs.json').read_text())
    for name, entry in frozen['files'].items():
        assert digest(HERE / name) == entry['sha256'], name
    hashes = {name: digest(HERE / name) for name in FILES}
    run = HERE / 'out-history' / ('build-' + str(uuid.uuid4()))
    source, output, headers, validation = (run / x for x in ('source', 'output', 'sdk-include', 'validation'))
    for folder in (source, output, headers, validation):
        folder.mkdir(parents=True)
    for name in FILES:
        dst = source / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(HERE / name, dst)
    shutil.copyfile(sdk / 'include/azmod-mod.h', headers / 'azmod-mod.h')
    (run / 'source-manifest.json').write_text(json.dumps(hashes, indent=2) + '\n')
    command = '''set -eu
uname -m
g++ --version | head -n1
make -C /src AZ_SDK_INCLUDE=/sdk-include OUT=/out
readelf --wide --dyn-syms /out/encoder-task-observer.so > /out/dynamic-symbols.txt
readelf --wide --dynamic /out/encoder-task-observer.so > /out/dynamic.txt
readelf --debug-dump=frames /out/encoder-task-observer.so > /out/cfi.txt
objdump -dC /out/encoder-task-observer.so > /out/disassembly.txt
nm -SC /out/encoder-task-observer.so > /out/symbols.txt
'''
    built = subprocess.run(['docker', 'run', '--rm', '--pull=never', '--network', 'none', '--platform', 'linux/arm64',
                            '-v', str(source) + ':/src:ro', '-v', str(output) + ':/out',
                            '-v', str(headers) + ':/sdk-include:ro', IMAGE, 'sh', '-c', command],
                           text=True, capture_output=True, timeout=180)
    log = built.stdout + built.stderr
    (run / 'build-output.txt').write_text(log)
    (run / 'build-exit.json').write_text(json.dumps({'returncode': built.returncode}) + '\n')
    print(log, end='', flush=True)
    assert built.returncode == 0, str(run)
    for name in ('azmod.json', 'icon.png', 'LICENSE'):
        shutil.copyfile(source / name, validation / name)
    shutil.copyfile(output / 'encoder-task-observer.so', validation / 'encoder-task-observer.so')
    checked = subprocess.run([sys.executable, str(sdk / 'tools/azmod-pack'), 'validate', str(validation)],
                             text=True, capture_output=True, timeout=60)
    check_log = checked.stdout + checked.stderr
    (run / 'validation-output.txt').write_text(check_log)
    (run / 'validation-exit.json').write_text(json.dumps({'returncode': checked.returncode}) + '\n')
    print(check_log.replace(str(validation), '<VALIDATION>'), end='', flush=True)
    assert checked.returncode == 0, str(run)
    dynamic = (output / 'dynamic-symbols.txt').read_text()
    imports = [line for line in dynamic.splitlines() if ' UND ' in line and re.search(r'__(?:atomic|sync)_', line)]
    assert not imports, imports
    assert hashes == {name: digest(HERE / name) for name in FILES}, 'source changed during build'
    public_log = log + check_log.replace(str(validation), '<VALIDATION>')
    report = {'scope': 'Unpublished passive SDK adapter ARM64 build and SDK validation; no native hook execution.',
              'source_sha256': hashes, 'sdk_commit': SDK_PIN, 'sdk_source_sha256': {name: digest(sdk / name) for name in sdk_files},
              'image': IMAGE, 'image_id': IMAGE_ID, 'platform': 'linux/arm64', 'network': 'none',
              'private_run_id': run.name, 'binary_sha256': digest(output / 'encoder-task-observer.so'),
              'dynamic_atomic_imports': imports, 'sdk_validation_passed': True,
              'build_log_sha256': hashlib.sha256(public_log.encode()).hexdigest(),
              'emulator_installation': False, 'physical_access': False, 'package_published': False, 'binary_included': False}
    (run / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    (HERE / 'build-log.txt').write_text(public_log)
    args.result.write_text(json.dumps(report, indent=2) + '\n')
    print('Retained build ' + run.name)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Offline retained broker lifecycle with exact frozen operation bodies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import uuid

if not __debug__:
    raise RuntimeError('optimized Python disables required verification assertions')

HERE = Path(__file__).resolve().parent
PRODUCTION = ('wire.c', 'guest_dispatch.c', 'broker_owner.c')
MODES = {'plain': [], 'asan-ubsan': ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'],
         'tsan': ['-fsanitize=thread', '-fno-omit-frame-pointer']}
ENV_STRIP = ('ASAN_OPTIONS', 'TSAN_OPTIONS', 'UBSAN_OPTIONS', 'LSAN_OPTIONS',
             'DYLD_INSERT_LIBRARIES', 'DYLD_LIBRARY_PATH', 'LD_PRELOAD', 'LD_LIBRARY_PATH')

def sha(data):
    return hashlib.sha256(data.encode() if isinstance(data, str) else data).hexdigest()

def save_json(path, value):
    path.write_text(json.dumps(value, sort_keys=True, indent=2) + '\n')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--result', type=Path)
    args = parser.parse_args()
    if args.result and args.result.exists():
        raise ValueError('result must not overwrite existing evidence')
    run = HERE / 'private' / ('run-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    save_json(run / 'attempt.json', {'status': 'preparing'})
    try:
        names = ('run-tests.py', 'test-broker-owner.c', 'production-smoke.c',
                 'broker_owner.h', 'broker_owner.c', 'sync.h', 'sync.c',
                 'parent-inputs.json', 'controls.json', 'test-inventory.json')
        captured = {n: (HERE / n).read_bytes() for n in names}
        for name, data in captured.items():
            (run / name).write_bytes(data)
        save_json(run / 'attempt.json', {'status': 'preparing', 'input_sha256': {n: sha(v) for n,v in captured.items()}})
        # Scrub sanitizer/preload overrides before any child process.
        for name in ENV_STRIP:
            os.environ.pop(name, None)
        pins = json.loads(captured['parent-inputs.json'])
        frozen = {n: (HERE.parent / n).read_bytes() for n in pins}
        for name, data in frozen.items():
            if sha(data) != pins[name]:
                raise ValueError('frozen input drift: ' + name)
            dest = run / 'frozen' / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
        generated = {}
        for folder, files in (
            ('emulator-broker-restore', ('broker.h',)),
            ('emulator-broker-wire', ('wire.c','wire.h','guest_dispatch.c','guest_dispatch.h'))):
            for name in files:
                generated[name] = frozen[folder + '/' + name]
        # Exact frozen operation helpers/bodies, excluding old public lifecycle.
        broker = frozen['emulator-broker-restore/broker.c']
        first, last = b'int mb_create(', b'int mb_adopt('
        if broker.count(first) != 1 or broker.count(last) != 1:
            raise ValueError('frozen broker lifecycle boundaries must be unique')
        before, after = broker.index(first), broker.index(last)
        if before >= after:
            raise ValueError('broker lifecycle boundary order')
        prefix, suffix = broker[:before], broker[after:]
        generated['broker-core.inc'] = prefix + suffix
        for name in ('broker_owner.c','broker_owner.h','sync.c','sync.h'):
            generated[name] = captured[name]
        for name, data in generated.items():
            dest = run / 'source' / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
        controls = json.loads(captured['controls.json'])
        inventory = json.loads(captured['test-inventory.json'])
        cases = inventory['cases']
        if inventory.get('production_cases') != {'real-sync': 1}:
            raise ValueError('production sync inventory mismatch')
        if (not isinstance(cases, dict) or not cases or not controls or
            any(not isinstance(n, str) or re.fullmatch(r'[a-z0-9-]+', n) is None or
                type(count) is not int or count <= 0 for n, count in cases.items()) or
            len({c['name'] for c in controls}) != len(controls)):
            raise ValueError('invalid inventory or empty/duplicate controls')
        variants = []
        for c in controls:
            if c['file'] != 'broker_owner.c' or c['case'] not in cases:
                raise ValueError('unreviewed control target')
            original = generated[c['file']].decode()
            if original.count(c['old']) != 1 or not c['expected_failures']:
                raise ValueError('nonunique or empty control: ' + c['name'])
            variants.append((c, original.replace(c['old'], c['new']).encode()))
        env = os.environ.copy()
        for name in ENV_STRIP:
            env.pop(name, None)
        cc = shutil.which(os.environ.get('CC', 'cc'))
        if not cc:
            raise ValueError('compiler not found')
        compiler = subprocess.check_output([cc, '--version'], env=env, text=True)
        (run / 'compiler.txt').write_text(compiler)
        result = {'source_sha256': {n: sha(v) for n,v in captured.items()},
                  'frozen_inputs_sha256': pins,
                  'generated_sha256': {n: sha(v) for n,v in generated.items()},
                  'broker_core': {'source': 'emulator-broker-restore/broker.c',
                                  'excluded_start': 'int mb_create(',
                                  'suffix_start': 'int mb_adopt(',
                                  'prefix_bytes': len(prefix), 'suffix_bytes': len(suffix),
                                  'prefix_sha256': sha(prefix), 'suffix_sha256': sha(suffix),
                                  'combined_sha256': sha(prefix + suffix)},
                  'compiler': compiler, 'sanitizer_preload_environment_removed': list(ENV_STRIP),
                  'compiles': [], 'links': [], 'runs': [],
                  'scope': 'Offline retained broker lifecycle with exact frozen operation bodies and C dispatcher; real pthread success paths, explicit scripted synchronization refusals and synthetic backend state only. Production pthread wrapper smoke is linked separately. No native adapter/CLAIM graph, descriptor custody, endpoint, install, runtime permission, checked alias exclusion, concurrency proof or recovery authority.'}
        def execute(command, log, compile_step=False):
            try:
                proc = subprocess.run(command, env=env, capture_output=True, text=True, timeout=45)
            except subprocess.TimeoutExpired as error:
                def raw(value):
                    return value if isinstance(value, bytes) else (value or '').encode()
                log.write_bytes(raw(error.stdout) + raw(error.stderr))
                raise
            output = proc.stdout + proc.stderr
            log.write_text(output)
            if compile_step and (proc.returncode != 0 or output):
                raise RuntimeError('compile/link diagnostic: ' + str(log.relative_to(run)))
            return proc, output
        includes = ['-I' + str(run / 'source')]
        for mode, flags in MODES.items():
            dest = run / mode
            dest.mkdir()
            base = [cc, '-std=gnu11', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror', *flags, *includes]
            objects = {}
            for name in PRODUCTION:
                obj = dest / (name + '.o')
                proc, output = execute(base + ['-c', str(run / 'source' / name), '-o', str(obj)], dest / (name + '-compile.txt'), True)
                objects[name] = obj
                result['compiles'].append({'mode': mode, 'variant': 'candidate', 'file': name,
                                           'source_sha256': sha(generated[name]), 'exit_code': proc.returncode,
                                           'output_sha256': sha(output)})
            harness = dest / 'harness.o'
            proc, output = execute(base + ['-c', str(run / 'test-broker-owner.c'), '-o', str(harness)],
                                   dest / 'harness-compile.txt', True)
            result['compiles'].append({'mode': mode, 'variant': 'candidate', 'file': 'test-broker-owner.c',
                                       'source_sha256': sha(captured['test-broker-owner.c']),
                                       'exit_code': proc.returncode, 'output_sha256': sha(output)})
            smoke_objects = {}
            for name in ('sync.c','production-smoke.c'):
                source = run / ('source/' + name if name == 'sync.c' else name)
                obj = dest / (name + '.o')
                proc, output = execute(base + ['-c', str(source), '-o', str(obj)],
                                       dest / (name + '-compile.txt'), True)
                smoke_objects[name] = obj
                result['compiles'].append({'mode': mode, 'variant': 'production-sync', 'file': name,
                                           'source_sha256': sha(captured[name]), 'exit_code': proc.returncode,
                                           'output_sha256': sha(output)})
            smoke = dest / 'production-sync'
            smoke.mkdir()
            exe = smoke / 'composition'
            proc, output = execute([cc, '-pthread', *flags,
                                    *[str(objects[n]) for n in PRODUCTION],
                                    *[str(smoke_objects[n]) for n in ('sync.c','production-smoke.c')],
                                    '-o', str(exe)], smoke / 'link.txt', True)
            result['links'].append({'mode': mode, 'variant': 'production-sync', 'exit_code': proc.returncode, 'output_sha256': sha(output)})
            proc, output = execute([str(exe)], smoke / 'real-sync.txt')
            if proc.returncode != 0 or proc.stdout != 'checks=1 failures=0\n' or proc.stderr:
                raise RuntimeError('production sync smoke failed: ' + mode)
            result['runs'].append({'mode': mode, 'variant': 'production-sync', 'case': 'real-sync',
                                   'checks': 1, 'failures': [], 'exit_code': 0, 'output_sha256': sha(output)})
            print(mode, 'production-sync', 'real-sync checks=1 failures=0', flush=True)
            rows = [('candidate', None, None, [(name, []) for name in cases])]
            rows += [(c['name'], c['file'], source, [(c['case'], c['expected_failures'])]) for c,source in variants]
            for variant, changed, source, selectors in rows:
                vd = dest / variant
                vd.mkdir()
                use_objects = objects.copy()
                if changed:
                    path = vd / changed
                    path.write_bytes(source)
                    obj = vd / (changed + '.o')
                    proc, output = execute(base + ['-c', str(path), '-o', str(obj)], vd / 'compile.txt', True)
                    use_objects[changed] = obj
                    result['compiles'].append({'mode': mode, 'variant': variant, 'file': changed,
                                               'source_sha256': sha(source), 'exit_code': proc.returncode,
                                               'output_sha256': sha(output)})
                exe = vd / 'composition'
                proc, output = execute([cc, '-pthread', *flags, str(harness),
                                        *[str(use_objects[n]) for n in PRODUCTION], '-o', str(exe)], vd / 'link.txt', True)
                result['links'].append({'mode': mode, 'variant': variant, 'exit_code': proc.returncode, 'output_sha256': sha(output)})
                for case, expected in selectors:
                    proc, output = execute([str(exe), case], vd / (case + '.txt'))
                    if re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|LeakSanitizer|runtime error:|DEADLYSIGNAL', output):
                        raise RuntimeError('sanitizer diagnostic: ' + mode + '/' + variant + '/' + case)
                    failed = re.findall(r'^FAIL (.+)$', proc.stderr, re.M)
                    expected_stdout = 'checks=' + str(cases[case]) + ' failures=' + str(len(expected)) + '\n'
                    expected_stderr = ''.join('FAIL ' + label + '\n' for label in expected)
                    if proc.returncode != int(bool(expected)) or proc.stdout != expected_stdout or proc.stderr != expected_stderr or failed != expected:
                        raise RuntimeError('unexpected functional result: ' + mode + '/' + variant + '/' + case)
                    result['runs'].append({'mode': mode, 'variant': variant, 'case': case,
                                           'checks': cases[case], 'failures': failed,
                                           'exit_code': proc.returncode, 'output_sha256': sha(output)})
                    print(mode, variant, case, expected_stdout.strip(), flush=True)
        # A stable source tree is part of this receipt.
        for name, data in captured.items():
            if (HERE / name).read_bytes() != data:
                raise ValueError('new source drift during run: ' + name)
        for name, data in frozen.items():
            if (HERE.parent / name).read_bytes() != data:
                raise ValueError('frozen input drift during run: ' + name)
        save_json(run / 'result.json', result)
        save_json(run / 'attempt.json', {'status': 'passed', 'result_sha256': sha((run / 'result.json').read_bytes())})
        if args.result:
            args.result.write_bytes((run / 'result.json').read_bytes())
        print('RESULT', sha((run / 'result.json').read_bytes()), 'private snapshot', run)
    except BaseException as error:
        save_json(run / 'attempt.json', {'status': 'failed', 'error_type': type(error).__name__, 'error': str(error)})
        print('FAILED retained private snapshot', run, flush=True)
        raise

if __name__ == '__main__':
    main()

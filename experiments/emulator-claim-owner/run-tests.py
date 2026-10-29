#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Offline retained original-descriptor and one-shot CLAIM ownership checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import uuid

if not __debug__:
    raise RuntimeError('optimized Python disables required verification assertions')

HERE = Path(__file__).resolve().parent
FUNCTIONS = ('mixer_instance_init', 'xput_bytes', 'xget_bytes', 'ctrl_release',
             'mixer_owned_next', 'ctrl_ioctl', 'ctrl_write')
PRODUCTION = ('claim.c',)
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
    parser.add_argument('--emu', type=Path, required=True)
    parser.add_argument('--result', type=Path)
    args = parser.parse_args()
    if args.result and args.result.exists():
        raise ValueError('result must not overwrite existing evidence')
    run = HERE / 'private' / ('run-' + uuid.uuid4().hex)
    run.mkdir(parents=True)
    save_json(run / 'attempt.json', {'status': 'preparing'})
    try:
        names = ('run-tests.py', 'test-claim.c', 'claim.h', 'claim.c',
                 'parent-inputs.json', 'controls.json', 'test-inventory.json')
        captured = {n: (HERE / n).read_bytes() for n in names}
        for name, data in captured.items():
            (run / name).write_bytes(data)
        save_json(run / 'attempt.json', {'status': 'preparing', 'input_sha256': {n: sha(v) for n,v in captured.items()}})
        # Helpers launch Git/patch themselves; scrub before any child process.
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
        parent = run / 'frozen' / 'emulator-overlay-restore'
        # Captured helpers only; no parent suite is executed.
        prepare = runpy.run_path(str(parent / 'prepare.py'))['prepare']
        extract = runpy.run_path(str(parent / 'run-tests.py'))['extract']
        receipt = prepare(args.emu, run / 'prepared')
        mod = run / 'prepared/guest/modules/subucom_virt'
        src = (mod / 'subucom_virt.c').read_text()
        methods = {n: extract(src, n) for n in FUNCTIONS}
        declarations = ''
        for name in ('spi_crc_len', 'n_crc', 'interval_us', 'testmode_held'):
            matches = re.findall(r'^static [^;\n]*\b' + name + r'[^;\n]*;', src, re.M)
            if len(matches) != 1:
                raise ValueError('declaration: ' + name)
            declarations += matches[0] + '\n'
        start = src.index('static DEFINE_SPINLOCK(miso_lock);')
        end = src.index('/* MOSI:', start)
        declarations += src[start:end].replace(methods['mixer_instance_init'], '')
        kernel = declarations + '\n'.join(methods.values())
        shim = frozen['emulator-overlay-restore/test-restore.c'].decode().split('#include "candidate.inc"')[0]
        generated = {'candidate.inc': kernel.encode(), 'kernel-shim.inc': shim.encode(),
                     'linux/types.h': b'#include <stdint.h>\ntypedef uint32_t __u32;typedef int32_t __s32;typedef uint8_t __u8;\n#define __aligned_u64 uint64_t __attribute__((aligned(8)))\n',
                     'linux/ioctl.h': b'#define _IOW(t,n,s) ((1u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n#define _IOR(t,n,s) ((2u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n'}
        for name in ('subucom_ownership.h', 'subucom_frames.h'):
            generated[name] = (mod / name).read_bytes()
        for folder, files in (
            ('emulator-native64-adapter', ('syscalls.h',)),
            ('emulator-broker-restore', ('broker.h',))):
            for name in files:
                generated[name] = frozen[folder + '/' + name]
        generated['claim.c'] = captured['claim.c']
        generated['claim.h'] = captured['claim.h']
        for name, data in generated.items():
            dest = run / 'source' / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
        controls = json.loads(captured['controls.json'])
        inventory = json.loads(captured['test-inventory.json'])
        cases = inventory['cases']
        if (not isinstance(cases, dict) or not cases or not controls or
            any(not isinstance(n, str) or re.fullmatch(r'[a-z0-9-]+', n) is None or
                type(count) is not int or count <= 0 for n, count in cases.items()) or
            len({c['name'] for c in controls}) != len(controls)):
            raise ValueError('invalid inventory or empty/duplicate controls')
        variants = []
        for c in controls:
            if c['file'] != 'claim.c' or c['case'] not in cases:
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
                  'frozen_inputs_sha256': pins, 'kernel_prepare': receipt,
                  'generated_sha256': {n: sha(v) for n,v in generated.items()},
                  'kernel_function_sha256': {n: sha(v) for n,v in methods.items()},
                  'compiler': compiler, 'sanitizer_preload_environment_removed': list(ENV_STRIP),
                  'compiles': [], 'links': [], 'runs': [],
                  'scope': 'Offline retained owner for caller-transferred original fd, exact prestate, one-shot CLAIM and matched poststate, against seven exact extracted driver methods. Synchronous serial modeled fd/syscall/usercopy/RNG/lock fixtures only. No adapter/broker graph, live libc/VFS, native baseline/stop authority, alias exclusion, concurrent/durable custody, checked teardown, endpoint or install.'}
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
            proc, output = execute(base + ['-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                                          '-c', str(run / 'test-claim.c'), '-o', str(harness)],
                                   dest / 'harness-compile.txt', True)
            result['compiles'].append({'mode': mode, 'variant': 'candidate', 'file': 'test-claim.c',
                                       'source_sha256': sha(captured['test-claim.c']),
                                       'exit_code': proc.returncode, 'output_sha256': sha(output)})
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

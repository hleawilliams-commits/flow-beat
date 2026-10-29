#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Offline guest stream custody over the retained production graph; no endpoints."""
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
PRODUCTION = ('claim.c', 'retained.c', 'broker_owner.c', 'wire.c', 'guest_dispatch.c', 'graph.c', 'stream.c')
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
        names = ('run-tests.py', 'test-stream.c', 'stream.h', 'stream.c',
                 'stream_native.h', 'stream_native.c',
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
            ('emulator-native64-adapter', ('adapter.h','syscalls.h')),
            ('emulator-broker-restore', ('broker.h',)),
            ('emulator-claim-owner', ('claim.c','claim.h')),
            ('emulator-retained-adapter', ('retained.c','retained.h')),
            ('emulator-retained-broker', ('broker_owner.c','broker_owner.h','sync.h')),
            ('emulator-broker-wire', ('wire.c','wire.h','guest_dispatch.c','guest_dispatch.h')),
            ('emulator-owned-graph', ('graph.c','graph.h'))):
            for name in files:
                generated[name] = frozen[folder + '/' + name]
        # Byte-identical private core only: legacy public lifecycle is not linked.
        adapter = frozen['emulator-native64-adapter/adapter.c']
        marker = b'int mba_attach('
        if adapter.count(marker) != 1:
            raise ValueError('frozen native core boundary must be unique')
        core = adapter[:adapter.index(marker)]
        generated['native64-core.inc'] = core
        broker = frozen['emulator-broker-restore/broker.c']
        first, last = b'int mb_create(', b'int mb_adopt('
        if broker.count(first) != 1 or broker.count(last) != 1:
            raise ValueError('frozen broker lifecycle boundaries must be unique')
        before, after = broker.index(first), broker.index(last)
        if before >= after:
            raise ValueError('broker lifecycle boundary order')
        prefix, suffix = broker[:before], broker[after:]
        generated['broker-core.inc'] = prefix + suffix
        support_source = frozen['emulator-owned-graph/test-graph.c']
        support_boundary = b'static void retain_case(void)'
        if support_source.count(support_boundary) != 1:
            raise ValueError('graph fixture support boundary must be unique')
        support = support_source[:support_source.index(support_boundary)]
        if re.search(rb'\b(?:int main|static void [a-z_]+_case)\s*\(', support):
            raise ValueError('parent selector or main entered support prefix')
        generated['graph-support.inc'] = support
        for name in ('stream.c', 'stream.h', 'stream_native.h'):
            generated[name] = captured[name]
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
            if c['file'] != 'stream.c' or c['case'] not in cases:
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
                  'native64_core': {'source': 'emulator-native64-adapter/adapter.c',
                                    'boundary': 'int mba_attach(', 'prefix_bytes': len(core),
                                    'prefix_sha256': sha(core)},
                  'broker_core': {'source': 'emulator-broker-restore/broker.c',
                                  'excluded_start': 'int mb_create(',
                                  'suffix_start': 'int mb_adopt(',
                                  'prefix_bytes': len(prefix), 'suffix_bytes': len(suffix),
                                  'prefix_sha256': sha(prefix), 'suffix_sha256': sha(suffix),
                                  'combined_sha256': sha(prefix + suffix)},
                  'graph_fixture_support': {'source': 'emulator-owned-graph/test-graph.c',
                                           'boundary': support_boundary.decode(),
                                           'prefix_bytes': len(support), 'prefix_sha256': sha(support),
                                           'parent_selectors_compiled': False},
                  'compiler': compiler, 'sanitizer_preload_environment_removed': list(ENV_STRIP),
                  'compiles': [], 'links': [], 'runs': [],
                  'scope': 'Offline guest stream owner over actual frozen graph/child/wire code and seven exact extracted driver methods. Reused exact graph fixture support only, no parent selectors. Stream read/write/close and controller descriptors/syscalls/usercopy/RNG/kernel locks are modeled; actual pthread success paths remain. Thin production POSIX stream wrappers are separately strict-compiled only, never linked into the scripted fixture or executed. A bounded step limits call count, not blocking time. Scripted EPIPE returns require caller-arranged signal handling and do not prove SIGPIPE survival. External quiescence and uncertainty accounting remain preconditions. No native endpoint, Linux ARM64 build, live VFS, authentication, transport/supervisor binding, alias exclusion, native baseline/stop authority, concurrency/durable custody, installation or runtime permission.'}
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
        # Real POSIX wrapper body compiles against host headers only. It is not
        # linked with the fixture's modeled syscall symbols and is never run.
        native_obj = run / 'stream_native.o'
        proc, output = execute([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                                '-c', str(run / 'stream_native.c'), '-o', str(native_obj)],
                               run / 'stream-native-compile.txt', True)
        result['compiles'].append({'mode': 'plain-host-wrapper', 'variant': 'production-wrapper',
                                   'file': 'stream_native.c', 'source_sha256': sha(captured['stream_native.c']),
                                   'exit_code': proc.returncode, 'output_sha256': sha(output),
                                   'linked': False, 'executed': False})
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
                                          '-c', str(run / 'test-stream.c'), '-o', str(harness)],
                                   dest / 'harness-compile.txt', True)
            result['compiles'].append({'mode': mode, 'variant': 'candidate', 'file': 'test-stream.c',
                                       'source_sha256': sha(captured['test-stream.c']),
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
                exe = vd / 'stream-composition'
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

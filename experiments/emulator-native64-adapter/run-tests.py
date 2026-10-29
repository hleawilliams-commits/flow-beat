#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Focused offline native64 adapter checks against the frozen broker/kernel."""
import argparse
import hashlib
import json
import os
import re
import runpy
import subprocess
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
FUNCTIONS = ('mixer_instance_init', 'xput_bytes', 'xget_bytes', 'ctrl_release',
             'mixer_owned_next', 'ctrl_ioctl', 'ctrl_write')


def sha(data):
    return hashlib.sha256(data.encode() if isinstance(data, str) else data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emu', type=Path, required=True)
    parser.add_argument('--modes', nargs='+', choices=['plain', 'asan-ubsan', 'tsan'],
                        default=['plain', 'asan-ubsan', 'tsan'])
    parser.add_argument('--result', type=Path)
    args = parser.parse_args()
    run = HERE / 'private/runs' / uuid.uuid4().hex
    run.mkdir(parents=True)
    names = ('adapter.c', 'adapter.h', 'syscalls.h', 'native.c', 'test-adapter.c', 'run-tests.py',
             'parent-inputs.json', 'controls.json', 'test-inventory.json')
    captured = {n: (HERE / n).read_bytes() for n in names}
    for name, data in captured.items():
        (run / name).write_bytes(data)
    pins = json.loads(captured['parent-inputs.json'])
    frozen = {n: (HERE.parent / n).read_bytes() for n in pins}
    for name, data in frozen.items():
        assert sha(data) == pins[name], name
        dest = run / 'frozen' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    # Run captured helper definitions only. Prepare itself pins all three patches
    # and checks the complete intermediate inventories; no parent tests run.
    parent = 'emulator-overlay-restore'
    extract = runpy.run_path(str(run / 'frozen' / parent / 'run-tests.py'))['extract']
    prepare = runpy.run_path(str(run / 'frozen' / parent / 'prepare.py'))['prepare']
    prepare.__globals__['HERE'] = HERE.parent / parent
    receipt = prepare(args.emu, run / 'prepared')
    for name in ('parent-inputs.json', 'candidate-pins.json'):
        assert (run / 'prepared' / name).read_bytes() == frozen[parent + '/' + name]
    assert (run / 'prepared/applied-3.patch').read_bytes() == frozen[parent + '/overlay-restore.patch']
    mod = run / 'prepared/guest/modules/subucom_virt'
    assert {n: sha((mod / n).read_bytes()) for n in receipt['candidate_sha256']} == receipt['candidate_sha256']
    src = (mod / 'subucom_virt.c').read_text()
    methods = {n: extract(src, n) for n in FUNCTIONS}
    declarations = ''
    for name in ('spi_crc_len', 'n_crc', 'interval_us', 'testmode_held'):
        lines = re.findall(r'^static [^;\n]*\b' + name + r'[^;\n]*;', src, re.M)
        assert len(lines) == 1, name
        declarations += lines[0] + '\n'
    start = src.index('static DEFINE_SPINLOCK(miso_lock);')
    end = src.index('/* MOSI:', start)
    declarations += src[start:end].replace(methods['mixer_instance_init'], '')
    kernel = declarations + '\n'.join(methods.values())
    (run / 'candidate.inc').write_text(kernel)
    shim = frozen[parent + '/test-restore.c'].decode().split('#include "candidate.inc"')[0]
    (run / 'kernel-shim.inc').write_text(shim)
    (run / 'linux').mkdir()
    (run / 'linux/types.h').write_text('#include <stdint.h>\ntypedef uint32_t __u32;typedef int32_t __s32;typedef uint8_t __u8;\n#define __aligned_u64 uint64_t __attribute__((aligned(8)))\n')
    (run / 'linux/ioctl.h').write_text('#define _IOW(t,n,s) ((1u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n#define _IOR(t,n,s) ((2u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n')
    for name in ('subucom_ownership.h', 'subucom_frames.h'):
        (run / name).write_bytes((mod / name).read_bytes())
    for name in ('broker.c', 'broker.h'):
        (run / name).write_bytes(frozen['emulator-broker-restore/' + name])
    (run / 'broker-under-test.inc').write_bytes(frozen['emulator-broker-restore/broker.c'])
    controls = json.loads(captured['controls.json'])
    inventory = json.loads(captured['test-inventory.json'])
    broker = captured['adapter.c'].decode()
    variants = [('candidate', broker, 'all', [])]
    for control in controls:
        old, new = control['old'], control['new']
        assert broker.count(old) == 1, control['name']
        variants.append((control['name'], broker.replace(old, new),
                         control['case'], [control['label']]))
    cc = os.environ.get('CC', 'cc')
    compiler = subprocess.check_output([cc, '--version'], text=True)
    (run / 'compiler.txt').write_text(compiler)
    result = {
        'source_sha256': {n: sha(d) for n, d in captured.items()},
        'frozen_inputs_sha256': pins, 'kernel_prepare': receipt,
        'kernel_function_sha256': {n: sha(d) for n, d in methods.items()},
        'kernel_extraction_sha256': sha(kernel), 'kernel_declarations_sha256': sha(declarations),
        'kernel_shim_sha256': sha(shim), 'compiler': compiler,
        'uapi_shims_sha256': {n: sha((run / 'linux' / n).read_bytes()) for n in ('types.h', 'ioctl.h')},
        'runs': [], 'standalone_compiles': [],
        'scope': 'Offline native64 adapter with frozen broker and seven exact extracted RESTORE kernel methods. Test fd-table/syscall boundary plus owned-memory usercopy, RNG and pthread shims. Native libc wrappers are not executed in this host fixture; separately compiled/linked for ARM64 Linux. No actual kernel execution, live fd custody, native consumption, stop/recovery coordinator, durable journal, installation or endpoint contact.'}
    modes = {'plain': [], 'asan-ubsan': ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'],
             'tsan': ['-fsanitize=thread', '-fno-omit-frame-pointer']}
    # Compile the new adapter as a separate TU with no fixture unused suppression.
    # Production native.c is Linux-only and separately built, never run here.
    for mode in args.modes:
        command = [cc, '-std=c11', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror',
                   *modes[mode], '-I' + str(run), '-c', str(run / 'adapter.c'), '-o', str(run / (mode + '.o'))]
        proc = subprocess.run(command, capture_output=True, text=True)
        output = proc.stdout + proc.stderr
        (run / (mode + '-standalone.txt')).write_text(output)
        if proc.returncode or output:
            raise RuntimeError('standalone compile diagnostic: ' + str(run))
        result['standalone_compiles'].append({'mode': mode, 'exit_code': proc.returncode,
                                            'output_sha256': sha(output)})
    for name, source, case, expected in variants:
        dest = run / name
        dest.mkdir()
        (dest / 'adapter-under-test.inc').write_text(source)
        for mode in args.modes:
            exe = dest / mode
            command = [cc, '-std=gnu11', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                       *modes[mode], '-I' + str(dest), '-I' + str(run),
                       str(run / 'test-adapter.c'), '-o', str(exe)]
            proc = subprocess.run(command, capture_output=True, text=True)
            output = proc.stdout + proc.stderr
            (dest / (mode + '-compile.txt')).write_text(output)
            if proc.returncode or output:
                raise RuntimeError('fixture compile diagnostic: ' + str(dest))
            proc = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=45)
            output = proc.stdout + proc.stderr
            (dest / (mode + '.txt')).write_text(output)
            if re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|LeakSanitizer|runtime error:|DEADLYSIGNAL', output):
                raise RuntimeError('sanitizer diagnostic: ' + str(dest))
            failed = sorted(re.findall(r'^FAIL (.+)$', proc.stderr, re.M))
            match = re.search(r'^checks=(\d+) failures=(\d+)$', proc.stdout, re.M)
            if (not match or int(match[1]) != inventory['checks'][case]
                    or int(match[2]) != len(expected) or failed != sorted(expected)
                    or proc.returncode != bool(expected)):
                raise RuntimeError('unexpected functional result: ' + str(dest))
            result['runs'].append({'variant': name, 'case': case, 'mode': mode,
                                   'checks': int(match[1]), 'failures': failed,
                                   'exit_code': proc.returncode, 'adapter_sha256': sha(source),
                                   'output_sha256': sha(output)})
            print(name, mode, match[0], flush=True)
    for name, data in captured.items():
        assert (HERE / name).read_bytes() == data, 'source changed during run: ' + name
    for name, data in frozen.items():
        assert (HERE.parent / name).read_bytes() == data, 'frozen input changed: ' + name
    raw = json.dumps(result, sort_keys=True, indent=2) + '\n'
    (run / 'result.json').write_text(raw)
    if args.result:
        args.result.write_text(raw)
    print('RESULT', sha(raw), 'private snapshot', run)


if __name__ == '__main__':
    main()

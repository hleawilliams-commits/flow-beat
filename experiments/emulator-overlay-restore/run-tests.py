#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run focused RESTORE tests against exact extracted kernel functions offline."""
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
FUNCTIONS = ('mixer_instance_init', 'crc16_x25', 'crc_lengths_set',
             'crc_lengths_get', 'xput_bytes', 'xget_bytes', 'spi_open',
             'spi_release', 'spi_read', 'ctrl_open', 'ctrl_release',
             'mixer_owned_next', 'ctrl_ioctl', 'ctrl_write')


def sha(data):
    return hashlib.sha256(data.encode() if isinstance(data, str) else data).hexdigest()


def extract(src, name):
    match = re.search(r'^static [^\n]*\b' + name + r'\([^;]*?\)\s*\{', src, re.M)
    assert match, name
    tokens = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', re.S)
    depth = 0
    for token in tokens.finditer(src, src.index('{', match.start())):
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if not depth:
                return src[match.start():token.end()] + '\n'
    raise AssertionError(name)


def integration(src):
    checks = {
        'control_handler_registered': '.unlocked_ioctl = ctrl_ioctl,' in src,
        'no_compat_handler': '.compat_ioctl' not in src,
        'opt_in_default_zero': 'static bool allow_overlay_ownership;' in src,
        'opt_in_immutable_mode': 'module_param(allow_overlay_ownership, bool, 0444);' in src,
        'crc_callback_registered': 'module_param_cb(spi_crc_len, &crc_lengths_ops, NULL, 0644);' in src,
        'old_crc_array_removed': 'module_param_array(spi_crc_len' not in src,
        'crc_array_metadata_once': re.findall(r'__MODULE_PARM_TYPE\(\s*spi_crc_len\s*,[^)]*\)', src) == ['__MODULE_PARM_TYPE(spi_crc_len, "array of int")'],
        'instance_init_before_device_allocation': bool(re.search(r'int ret, i, n_added = 0, ctrl_added = 0;\s*ret = mixer_instance_init\(\);\s*if \(ret\)\s*return ret;\s*ret = alloc_chrdev_region', extract(src, 'subucom_virt_init'))),
        'instance_init_registered': 'module_init(subucom_virt_init);' in src,
        'rng_kernel_header': '#include <linux/random.h>' in src,
        'crc_ops_set_get': '.set = crc_lengths_set,' in src and '.get = crc_lengths_get,' in src,
    }
    for expr in (
        'sizeof(struct az_overlay_command) == 48', 'sizeof(struct az_overlay_frame) == 200',
        'sizeof(struct az_overlay_state) == 208', 'sizeof(struct az_overlay_restore) == 208',
        'offsetof(struct az_overlay_command, instance) == 8',
        'offsetof(struct az_overlay_command, run_id) == 24',
        'offsetof(struct az_overlay_command, epoch) == 32',
        'offsetof(struct az_overlay_command, sequence) == 40',
        'offsetof(struct az_overlay_frame, frame) == 48',
        'offsetof(struct az_overlay_state, instance) == 8',
        'offsetof(struct az_overlay_state, frame) == 56',
        'offsetof(struct az_overlay_restore, injected) == 48',
        'offsetof(struct az_overlay_restore, reserved) == 52',
        'offsetof(struct az_overlay_restore, frame) == 56', 'AZ_OVERLAY_BYTES == MISO_SIZE',
    ):
        checks['static_assert ' + expr] = 'static_assert(' + expr + ');' in src
    assert all(checks.values()), checks
    return checks


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emu', type=Path, required=True)
    p.add_argument('--modes', nargs='+', choices=['plain', 'asan-ubsan', 'tsan'],
                   default=['plain', 'asan-ubsan', 'tsan'])
    p.add_argument('--result', type=Path)
    args = p.parse_args()
    run = HERE / 'private/runs' / uuid.uuid4().hex
    run.mkdir(parents=True)
    names = ('run-tests.py', 'test-restore.c', 'prepare.py', 'controls.json',
             'test-inventory.json', 'candidate-pins.json', 'parent-inputs.json',
             'overlay-restore.patch')
    inputs = {n: (HERE / n).read_bytes() for n in names}
    for name, data in inputs.items():
        (run / name).write_bytes(data)
    # Execute the captured helper. Its exact manifest/patch hashes and each
    # intermediate inventory constrain inputs opened from the original location.
    helper = runpy.run_path(str(run / 'prepare.py'))['prepare']
    helper.__globals__['HERE'] = HERE
    receipt = helper(args.emu, run / 'prepared')
    (run / 'prepare-receipt.json').write_text(json.dumps(receipt, sort_keys=True, indent=2) + '\n')
    assert (run / 'prepared/applied-3.patch').read_bytes() == inputs['overlay-restore.patch']
    for name in ('parent-inputs.json', 'candidate-pins.json'):
        assert (run / 'prepared' / name).read_bytes() == inputs[name]
    mod = run / 'prepared/guest/modules/subucom_virt'
    src = (mod / 'subucom_virt.c').read_text()
    assert {n: sha((mod / n).read_bytes()) for n in receipt['candidate_sha256']} == receipt['candidate_sha256']
    anchors = integration(src)
    methods = {name: extract(src, name) for name in FUNCTIONS}
    declarations = ''
    for name in ('spi_crc_len', 'n_crc', 'interval_us', 'testmode_held'):
        matches = re.findall(r'^static [^;\n]*\b' + name + r'[^;\n]*;', src, re.M)
        assert len(matches) == 1, (name, matches)
        declarations += matches[0] + '\n'
    start = src.index('static DEFINE_SPINLOCK(miso_lock);')
    end = src.index('/* MOSI:', start)
    prefix = declarations + src[start:end].replace(methods['mixer_instance_init'], '')
    (run / 'global-declarations.inc').write_text(prefix)
    (run / 'module-init.c').write_text(extract(src, 'subucom_virt_init'))
    (run / 'functions').mkdir()
    for name, data in methods.items():
        (run / 'functions' / (name + '.c')).write_text(data)
    (run / 'linux').mkdir()
    (run / 'linux/types.h').write_text('#include <stdint.h>\ntypedef uint32_t __u32;typedef int32_t __s32;typedef uint8_t __u8;\n#define __aligned_u64 uint64_t __attribute__((aligned(8)))\n')
    (run / 'linux/ioctl.h').write_text('#define _IOW(t,n,s) ((1u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n#define _IOR(t,n,s) ((2u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n')
    for name in ('subucom_ownership.h', 'subucom_frames.h'):
        (run / name).write_bytes((mod / name).read_bytes())
    body = prefix + '\n'.join(methods.values())
    inventory = json.loads(inputs['test-inventory.json'])
    controls = json.loads(inputs['controls.json'])
    variants = [('candidate', body, 'all', [])]
    for control in controls:
        changed = dict(methods)
        fn, old, new = control['function'], control['old'], control['new']
        assert changed[fn].count(old) == 1, control['name']
        changed[fn] = changed[fn].replace(old, new)
        variants.append((control['name'], prefix + '\n'.join(changed.values()),
                         control['case'], [control['label']]))
    cc = os.environ.get('CC', 'cc')
    compiler = subprocess.check_output([cc, '--version'], text=True)
    (run / 'compiler.txt').write_text(compiler)
    result = {
        'prepare': receipt, 'inputs_sha256': {n: sha(d) for n, d in inputs.items()},
        'compiler': compiler, 'source_integration_checks': anchors,
        'extracted_function_sha256': {n: sha(d) for n, d in methods.items()},
        'declarations_sha256': sha(prefix),
        'module_init_sha256': sha(extract(src, 'subucom_virt_init')),
        'shim_header_sha256': {n: sha((run / 'linux' / n).read_bytes()) for n in ('types.h', 'ioctl.h')},
        'runs': [],
        'scope': 'Focused exact-source RESTORE interaction tests with pthread lock, owned usercopy and kernel-service shims. Native64 ABI only. Synthetic payloads and forced schedules; inherited functions compiled but old suites not repeated. No kernel execution, native consumption, baseline authority, real transport/recovery, installation or endpoint contact.'}
    modes = {'plain': [], 'asan-ubsan': ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'],
             'tsan': ['-fsanitize=thread', '-fno-omit-frame-pointer']}
    for variant, source, case, expected in variants:
        directory = run / variant
        directory.mkdir()
        (directory / 'candidate.inc').write_text(source)
        for mode in args.modes:
            exe = directory / mode
            command = [cc, '-std=gnu11', '-O1', '-g', '-pthread', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', '-Wno-unused-function', *modes[mode],
                       '-I' + str(directory), '-I' + str(run), str(run / 'test-restore.c'), '-o', str(exe)]
            compiled = subprocess.run(command, capture_output=True, text=True)
            (directory / (mode + '-compile.txt')).write_text(compiled.stdout + compiled.stderr)
            if compiled.returncode:
                raise RuntimeError('compile failed; retained ' + str(directory))
            proc = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=45)
            output = proc.stdout + proc.stderr
            (directory / (mode + '.txt')).write_text(output)
            if re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|LeakSanitizer|runtime error:|DEADLYSIGNAL', output):
                raise RuntimeError('unexpected sanitizer diagnostic; retained ' + str(directory))
            failed = sorted(re.findall(r'^FAIL (.+)$', proc.stderr, re.M))
            match = re.search(r'^checks=(\d+) failures=(\d+)$', proc.stdout, re.M)
            if (not match or int(match[1]) != inventory['checks'][case]
                    or failed != sorted(expected) or int(match[2]) != len(expected)
                    or proc.returncode != bool(expected)):
                raise RuntimeError('unexpected functional result; retained ' + str(directory))
            result['runs'].append({'variant': variant, 'mode': mode, 'case': case,
                                   'checks': int(match[1]), 'failures': failed,
                                   'exit_code': proc.returncode, 'output_sha256': sha(output),
                                   'extraction_sha256': sha(source)})
            print(variant, mode, match[0], flush=True)
    for name, data in inputs.items():
        assert (HERE / name).read_bytes() == data, 'source changed during run: ' + name
    raw = json.dumps(result, indent=2, sort_keys=True) + '\n'
    (run / 'result.json').write_text(raw)
    if args.result:
        args.result.write_text(raw)
    print('RESULT', sha(raw), 'private snapshot', run)


if __name__ == '__main__':
    main()

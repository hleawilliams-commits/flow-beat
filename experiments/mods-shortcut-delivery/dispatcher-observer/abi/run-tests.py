#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute dispatcher entry assembly and discriminating controls offline on ARM64.

Uses an installed pinned Docker image with --pull=never and --network none.
No AZ emulator/device access. Generated source/binaries stay in out-history/.
"""
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import uuid

HERE = Path(__file__).resolve().parent
IMAGE = 'azmod-build:18.04'
EXPECTED = 'sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90'


def run(command):
    return subprocess.run(command, text=True, capture_output=True, timeout=90)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


check = run(['docker', 'image', 'inspect', IMAGE, '--format', '{{.Id}} {{.Architecture}}'])
assert check.returncode == 0, check.stderr
assert check.stdout.strip() == EXPECTED + ' arm64', check.stdout
names = ['entry.S', 'test_fixture.S', 'test_abi.cpp', 'run-tests.py']
sources = {name: digest(HERE / name) for name in names}
out = HERE / 'out-history' / str(uuid.uuid4())
inputs = out / 'inputs'
inputs.mkdir(parents=True)
for name in names:
    (inputs / name).write_bytes((HERE / name).read_bytes())
assert {name: digest(inputs / name) for name in names} == sources


def docker(command):
    return run(['docker', 'run', '--rm', '--pull=never', '--network', 'none',
                '--platform', 'linux/arm64', '-v', str(inputs) + ':/src:ro',
                '-v', str(out) + ':/out', IMAGE, 'sh', '-c', command])


def output_of(result, name):
    text = result.stdout + result.stderr
    (out / (name + '-process.txt')).write_text(text)
    return text


def assert_run(result, expected):
    text = result.stdout + result.stderr
    failures = re.findall(r'^FAIL line \d+: (.+)$', text, re.M)
    summary = re.findall(r'^AArch64 ABI assertions: (\d+) passed, (\d+) failed; helper calls=(\d+); native calls=(\d+)$', text, re.M)
    assert len(summary) == 1, text
    passed, failed, helper, native = map(int, summary[0])
    assert passed + failed == 55 and helper == 12 and native == 11, text
    assert failed == len(expected) and Counter(failures) == Counter(expected), text
    assert result.returncode == bool(expected), (result.returncode, text)
    assert not re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|LeakSanitizer|runtime error:|DEADLYSIGNAL', text), text
    return {'assertions': passed + failed, 'passed': passed, 'failed': failed,
            'helper_calls': helper, 'native_calls': native,
            'exit_code': result.returncode, 'expected_failures': expected}


commands = '''set -eu
uname -m
g++ --version | head -n 1
g++ -std=c++17 -O2 -g -Wall -Wextra -Werror -fexceptions -fasynchronous-unwind-tables -fPIC -c /src/entry.S -o /out/entry.o
g++ -std=c++17 -O2 -g -Wall -Wextra -Werror -fexceptions -fasynchronous-unwind-tables /out/entry.o /src/test_fixture.S /src/test_abi.cpp -o /out/test_abi
/out/test_abi
objdump -d /out/entry.o > /out/entry-disassembly.txt
readelf --debug-dump=frames /out/entry.o > /out/entry-cfi.txt
readelf --wide -r /out/entry.o > /out/entry-relocations.txt
'''
candidate = docker(commands)
candidate_text = output_of(candidate, 'candidate')
print(candidate_text, end='', flush=True)
candidate_result = assert_run(candidate, [])
dis = (out / 'entry-disassembly.txt').read_text()
cfi = (out / 'entry-cfi.txt').read_text()
rel = (out / 'entry-relocations.txt').read_text()
instructions = [(m.group(1), m.group(2).strip()) for m in re.finditer(
    r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(\S+)\s*(.*)$', dis, re.M)]
assert len(instructions) == 46, instructions
assert sum(m == 'bl' for m, _ in instructions) == 1
assert sum(m == 'br' for m, _ in instructions) == 1
assert instructions[-1] == ('br', 'x16') and instructions[-2] == ('ldar', 'x16, [x16]')
assert instructions[0] == ('sub', 'sp, sp, #0x140')
assert ('add', 'sp, sp, #0x140') in instructions
assert ('mov', 'x5, x30') in instructions and ('mov', 'x4, x30') not in instructions
assert not any(m in ['ret', 'blr'] for m, _ in instructions)
assert 'r30 (x30) at cfa-168' in cfi and 'DW_CFA_def_cfa_offset: 320' in cfi
assert 'DW_CFA_restore: r30 (x30)' in cfi and 'DW_CFA_def_cfa_offset: 0' in cfi
assert 'az_dispatch_observe_entry' in rel and 'az_dispatch_original' in rel

register_check = 'fixture_register_capture.x[i]==fixture_expected_registers[i]'
integer_check = 'fixture_entry_integer(a,2,c,4,5,6,7,8,9,10,11)==a+3*2+5*c+7*4+11*5+13*6+17*7+19*8+23*9+29*10+31*11'
integer_capture = 'events[before].a==a&&events[before].b==2&&events[before].c==c&&events[before].d==4&&events[before].e==5'
nested_capture = 'events[before+i].a==3-i&&events[before+i].b==10&&events[before+i].c==20&&events[before+i].d==30&&events[before+i].e==40'
entry = (inputs / 'entry.S').read_text()
controls = [
    ('missing-x2-x3-restore', '    ldp x2, x3, [sp, #16]\n', '',
     [register_check] * 2 + [integer_check,
      'l.a==3&&l.b==7&&l.c==11&&l.d==15&&l.e==0x123456789abcdef0ULL',
      'fixture_entry_nested(3,10,20,30,40)==103'] + [nested_capture] * 3),
    ('wrong-lr-register', '    mov x5, x30\n', '    mov x4, x30\n',
     ['fixture_register_capture.lr==events[0].lr && events[0].lr!=0',
      'events[0].e==static_cast<std::uint32_t>(fixture_expected_registers[4])',
      integer_capture] + [nested_capture] * 4),
    ('truncated-x2-capture', '    mov x5, x30\n', '    mov w2, w2\n    mov x5, x30\n',
     ['events[0].c==fixture_expected_registers[2]', integer_capture]),
]
control_results = []
control_logs = []
for name, original, replacement, expected in controls:
    assert entry.count(original) == 1
    (out / (name + '.S')).write_text(entry.replace(original, replacement))
    result = docker('g++ -std=c++17 -O2 -g -Wall -Wextra -Werror -fexceptions '
                    '-fasynchronous-unwind-tables /out/' + name + '.S '
                    '/src/test_fixture.S /src/test_abi.cpp -o /out/' + name
                    + ' && /out/' + name)
    text = output_of(result, name)
    verified = assert_run(result, expected)
    verified.update({'name': name, 'source_sha256': digest(out / (name + '.S')),
                     'output_sha256': hashlib.sha256(text.encode()).hexdigest()})
    control_results.append(verified)
    control_logs.append('\nNegative control: ' + name + '\n' + text)
    print(name + ': exact ' + str(len(expected)) + ' expected failures', flush=True)

report = {'image': IMAGE, 'image_id': EXPECTED, 'platform': 'linux/arm64',
          'source_sha256': sources, 'object_sha256': digest(out / 'entry.o'),
          'test_elf_sha256': digest(out / 'test_abi'), 'candidate': candidate_result,
          'actual_assembly_executed': True, 'instruction_audit_passed': True,
          'cfi_audit_passed': True, 'negative_controls': control_results,
          'capture_shape': ['x0', 'x1', 'x2', 'w3', 'w4', 'original LR in x5'],
          'no_az_emulator_or_physical_device_access': True,
          'native_target_signature_inferred': False,
          'native_counter_ordering_or_queued_delivery_proven': False,
          'private_run_id': out.name}
(HERE / 'test-results.txt').write_text(candidate_text + ''.join(control_logs))
(HERE / 'test-result.json').write_text(json.dumps(report, indent=2) + '\n')
(out / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
for name in ['entry-disassembly.txt', 'entry-cfi.txt', 'entry-relocations.txt']:
    (HERE / name).write_text((out / name).read_text().replace('/out', '<OUTPUT>'))
assert {name: digest(HERE / name) for name in names} == sources, 'Source changed during run'

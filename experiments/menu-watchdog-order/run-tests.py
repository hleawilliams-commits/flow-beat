#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the pinned complete bus read path with the upstream fixture's stand-ins.

No device/emulator access; the upstream checkout is read with git show, never modified.
Each run keeps an immutable local extraction/build snapshot in ignored out-history/.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid

BASE = '3e0c5cb010454228f4ee63226b60d060494eeaae'
HERE = Path(__file__).resolve().parent
REL = 'src/azmod/bus_interpose.c'


def sha(data):
    return hashlib.sha256(data.encode() if isinstance(data, str) else data).hexdigest()


def func(src, name):
    match = re.search(r'^static [^\n]*\b' + name + r'\([^;]*?\)\s*\{', src, re.M)
    assert match, name
    tokens = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', re.S)
    depth = 0
    for m in tokens.finditer(src, src.index('{', match.start())):
        if m.group() == '{':
            depth += 1
        elif m.group() == '}':
            depth -= 1
            if depth == 0:
                return src[match.start():m.end()] + '\n'
    raise RuntimeError(name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--upstream', type=Path, required=True)
    ap.add_argument('--modes', nargs='+', choices=['plain', 'asan-ubsan', 'tsan'], default=['plain', 'asan-ubsan', 'tsan'])
    ap.add_argument('--result', type=Path)
    args = ap.parse_args()
    run = HERE / 'out-history' / str(uuid.uuid4())
    run.mkdir(parents=True)
    def source(rel):
        return subprocess.check_output(['git', 'show', BASE + ':' + rel], cwd=args.upstream, text=True)
    baseline = source(REL)
    core = run / REL
    core.parent.mkdir(parents=True)
    core.write_text(baseline)
    subprocess.run(['git', 'apply', '--check', str(HERE / 'watchdog-order.patch')], cwd=run, check=True)
    subprocess.run(['git', 'apply', str(HERE / 'watchdog-order.patch')], cwd=run, check=True)
    candidate = core.read_text()
    # Everything except the complete frame dispatcher is byte-identical.
    baseline_frame = func(baseline, 'bus_mixer_frame')
    candidate_frame = func(candidate, 'bus_mixer_frame')
    assert baseline.replace(baseline_frame, '') == candidate.replace(candidate_frame, '')
    assert candidate_frame.count('mixer_watchdog()') == 1
    assert candidate_frame.index('if (r >= 98 && !crc_in)') < candidate_frame.index('mixer_watchdog()')
    assert candidate_frame.index('if (r < 12)') < candidate_frame.index('mixer_watchdog()') < candidate_frame.index('mixer_browse_knob(')
    fixture = source('tests/unit/busread_trace_test.c')
    stub_prefix = fixture[:fixture.index('static void stamp(')]
    # Preserve the exact upstream fixture and prefix as artifacts. Only the following
    # two existing SDK stand-ins are instrumented; no production code is substituted.
    (run / 'upstream-busread_trace_test.c').write_text(fixture)
    (run / 'upstream-stubs.c').write_text(stub_prefix)
    instrumented = stub_prefix.replace('static long g_filt_unchecked;', '''static long g_filt_unchecked;
static unsigned raw_calls, run_calls;
static int filter_edit;
static uint8_t raw_seen[160], run_seen[160];''')
    original_run = '''    (void) f;
    g_filt_unchecked += bus == 3 /* AZ_BUS_MIXER_IN */ && len < 98;
    return 0;'''
    replacement_run = '''    g_filt_unchecked += bus == 3 /* AZ_BUS_MIXER_IN */ && len < 98;
    run_calls++;
    memcpy(run_seen, f, (size_t) len);
    if (filter_edit) f[80] ^= 0x55;
    return filter_edit;'''
    original_raw = '''    (void) f;
    g_filt_unchecked += bus == 3 && len < 98;'''
    replacement_raw = '''    g_filt_unchecked += bus == 3 && len < 98;
    raw_calls++;
    memcpy(raw_seen, f, (size_t) len);'''
    assert instrumented.count(original_run) == instrumented.count(original_raw) == 1
    instrumented = instrumented.replace(original_run, replacement_run).replace(original_raw, replacement_raw)
    (run / 'instrumented-stubs.c').write_text(instrumented)
    (run / 'fixture.c').write_text(instrumented + '\n' + (HERE / 'test-frame.c').read_text())
    headers = {}
    for name in ['bus_cfg.h', 'drag_handoff.h']:
        text = source('src/azmod/' + name)
        (run / name).write_text(text)
        headers[name] = sha(text)
    extractions = {}
    for name, text in [('unpatched', baseline), ('patched', candidate)]:
        readpath = text[text.index('/* ---- read(): the panel input buses'):]
        crc = func(text, 'crc16_x25')
        (run / (name + '-read.inc')).write_text(readpath)
        (run / (name + '-crc.inc')).write_text(crc)
        extractions[name] = {'full_source_sha256': sha(text), 'read_path_sha256': sha(readpath), 'crc_sha256': sha(crc), 'frame_function_sha256': sha(func(text, 'bus_mixer_frame'))}
    cc = os.environ.get('CC', 'cc')
    manifest = {'upstream_commit': BASE, 'source': REL, 'patch_sha256': sha((HERE / 'watchdog-order.patch').read_bytes()),
                'test_sha256': sha((HERE / 'test-frame.c').read_bytes()), 'runner_sha256': sha(Path(__file__).read_bytes()),
                'upstream_fixture_sha256': sha(fixture), 'upstream_stub_prefix_sha256': sha(stub_prefix),
                'instrumented_stubs_sha256': sha(instrumented), 'headers': headers, 'extractions': extractions,
                'scope': 'serial bus-thread fixture; no overlay concurrency, native dispatch, runtime or hardware proof',
                'runs': []}
    modes = {'plain': [], 'asan-ubsan': ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'], 'tsan': ['-fsanitize=thread', '-fno-omit-frame-pointer']}
    for mode in args.modes:
        for variant in ['unpatched', 'patched']:
            exe = run / (variant + '-' + mode)
            command = [cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter', *modes[mode], '-I' + str(run), '-DBI_INC="' + variant + '-read.inc"', '-DCRC_INC="' + variant + '-crc.inc"', str(run / 'fixture.c'), '-o', str(exe)]
            subprocess.run(command, check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            output = result.stdout + result.stderr
            (run / (variant + '-' + mode + '.txt')).write_text(output)
            print(variant + '-' + mode + ':\n' + output, end='', flush=True)
            # UBSan can recover and still return the control's expected exit code 1.
            # A strict negative control permits only its named assertion failures.
            if re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|LeakSanitizer|runtime error:|DEADLYSIGNAL', output):
                raise RuntimeError('Sanitizer diagnostic; preserved at ' + str(run))
            failures = sorted(re.findall(r'^FAIL (.+)$', result.stderr, re.M))
            expected = sorted((HERE / 'expected-unpatched-failures.txt').read_text().splitlines()) if variant == 'unpatched' else []
            match = re.search(r'^checks=(\d+) failures=(\d+)$', result.stdout, re.M)
            if not match or failures != expected or int(match[2]) != len(expected) or result.returncode != bool(expected):
                raise RuntimeError('Unexpected result; preserved at ' + str(run))
            manifest['runs'].append({'mode': mode, 'variant': variant, 'checks': int(match[1]), 'failures': len(failures), 'expected_failures': failures, 'exit_code': result.returncode, 'output_sha256': sha(output)})
    (run / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if args.result:
        args.result.write_text(json.dumps(manifest, indent=2) + '\n')
    print('Expected unpatched failures and patched assertions verified; private local snapshot retained.')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Build the inactive broker-wire successor and run reviewed host cases.

Runs new broker-wire cases only, with synthetic C broker calls over private pipes and controlled streams.
Never runs main, ctrl_thread, a window, socket/backend adapter or live settings.
Full candidate sources, logs and binaries stay under private/.
Cargo is locked and offline; missing cached host dependencies fail this run.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import types

if not __debug__:
    raise RuntimeError('optimized Python disables frozen parent pin assertions')

HERE = Path(__file__).resolve().parent
INPUTS = ('prepare-candidate.py', 'parent-inputs.json', 'candidate-pins.json',
          'broker_wire.rs', 'broker_wire_tests.rs', 'controls.json',
          'test-inventory.json', 'run-tests.py', 'SPEC.md',
          'wire.h', 'wire.c', 'guest_dispatch.h', 'guest_dispatch.c',
          'wire_tests.c', 'fixture_peer.c')

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(value, message):
    if not value:
        raise RuntimeError(message)

def run_command(command, log, env, timeout=120):
    try:
        result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        # A synchronization deadlock/timeout never counts as an expected control.
        output = (error.stdout or b'') + (error.stderr or b'')
        log.write_bytes(output if isinstance(output, bytes) else output.encode())
        raise
    log.write_text(result.stdout + result.stderr)
    return result


def run_c(output, inputs, env, inventory, control_spec):
    source_dir = output/'wire-src'
    original = {p.name:p.read_bytes() for p in source_dir.iterdir()}
    source_pins = {n:sha(d) for n,d in original.items()}
    require(source_pins == inventory['c_source_sha256'], 'C source inventory mismatch')
    controls = control_spec['c_controls']
    require(len(controls) == inventory['c_control_count']
            and len({c['name'] for c in controls}) == len(controls), 'C controls changed')
    cc = 'cc'
    compiler = run_command([cc, '--version'], output/'cc.log', env)
    require(compiler.returncode == 0, 'C compiler unavailable')
    result = {'compiler':compiler.stdout.strip(), 'source_sha256':source_pins,
              'runs':[], 'standalone_compiles':[], 'control_count':len(controls)}
    modes = {'plain':[], 'asan-ubsan':['-fsanitize=address,undefined','-fno-omit-frame-pointer'],
             'tsan':['-fsanitize=thread','-fno-omit-frame-pointer']}
    production = ['wire.c','guest_dispatch.c','broker.c']
    base = [cc,'-std=c11','-O1','-g','-pthread','-Wall','-Wextra','-Werror']
    for mode,flags in modes.items():
        for name in production:
            log = output/(mode+'-'+name+'-standalone.log')
            compiled = run_command(base+flags+['-I'+str(source_dir),'-c',str(source_dir/name),
                         '-o',str(output/(mode+'-'+name+'.o'))], log, env)
            require(compiled.returncode == 0 and not (compiled.stdout+compiled.stderr),
                    'C standalone compile failure/diagnostic: '+mode+'/'+name)
            result['standalone_compiles'].append({'mode':mode,'file':name,'exit':0,
                                                  'log_sha256':sha(log.read_bytes())})
    fixture_log = output/'fixture-compile.log'
    fixture = run_command(base+['-I'+str(source_dir)]+[str(source_dir/n) for n in production]
            +[str(source_dir/'fixture_peer.c'),'-o',str(output/'fixture-peer')], fixture_log, env)
    require(fixture.returncode == 0 and not (fixture.stdout+fixture.stderr), 'C fixture compile failed')
    result['fixture_compile_exit'] = 0
    result['fixture_compile_log_sha256'] = sha(fixture_log.read_bytes())
    for control in [None]+controls:
        name = control['name'] if control else 'candidate'
        directory = output/('c-'+name)
        directory.mkdir()
        variant = dict(original)
        if control:
            require(control['file'] in production, 'C mutation outside production sources')
            content = variant[control['file']].decode()
            require(content.count(control['old']) == 1 and control['old'] != control['new'],
                    'C control anchor not unique/noop: '+name)
            variant[control['file']] = content.replace(control['old'], control['new']).encode()
        for n,d in variant.items():
            (directory/n).write_bytes(d)
        case = control['case'] if control else 'all'
        expected = control['expected_failures'] if control else []
        require(case in inventory['c_checks'] and bool(expected) == bool(control),
                'C case/failure inventory mismatch: '+name)
        for mode,flags in modes.items():
            exe = directory/mode
            log = directory/(mode+'-compile.log')
            compiled = run_command(base+flags+['-DMBW_TEST','-I'+str(directory)]+[str(directory/n) for n in production]
                         +[str(directory/'wire_tests.c'),'-o',str(exe)], log, env)
            require(compiled.returncode == 0 and not (compiled.stdout+compiled.stderr),
                    'C compile failure is not a passing control: '+name+'/'+mode)
            testlog = directory/(mode+'-test.log')
            tested = run_command([str(exe),case], testlog, env, 45)
            raw = tested.stdout+tested.stderr
            require(not re.search(r'AddressSanitizer|ThreadSanitizer|UndefinedBehaviorSanitizer|'
                                  r'LeakSanitizer|runtime error:|DEADLYSIGNAL',raw),
                    'C sanitizer diagnostic: '+name+'/'+mode)
            summary = re.findall(r'^checks=(\d+) failures=(\d+)$',tested.stdout,re.M)
            failed = re.findall(r'^FAIL (.+)$',tested.stderr,re.M)
            require(len(summary)==1 and tuple(map(int,summary[0])) == (inventory['c_checks'][case],len(expected))
                    and sorted(failed)==sorted(expected) and tested.returncode==int(bool(expected)),
                    'C exact functional result mismatch: '+name+'/'+mode)
            result['runs'].append({'name':name,'case':case,'mode':mode,'exit':tested.returncode,
                'checks':int(summary[0][0]),'failures':failed,
                'source_sha256':{n:sha(d) for n,d in variant.items()},
                'compile_log_sha256':sha(log.read_bytes()),'test_log_sha256':sha(testlog.read_bytes())})
            print('C',name,mode,summary[0],flush=True)
        require(all((directory/n).read_bytes()==d for n,d in variant.items()), 'C variant changed')
    require(all((source_dir/n).read_bytes()==d for n,d in original.items()), 'C candidate changed')
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emu-source', type=Path, required=True)
    parser.add_argument('--result', type=Path)
    args = parser.parse_args()
    inputs = {name: (HERE/name).read_bytes() for name in INPUTS}
    inventory = json.loads(inputs['test-inventory.json'])
    controls = json.loads(inputs['controls.json'])['controls']
    pins = json.loads(inputs['candidate-pins.json'])
    control_pins = json.loads(inputs['controls.json'])['candidate_sha256']
    require(all(pins[n] == h for n, h in control_pins.items()), 'control source pins differ')
    selected_count = inventory['selected_count']
    total_count = inventory['total_count']
    control_count = inventory['control_count']
    require(len(inventory['selected']) == selected_count > 0
            and len(inventory['all']) == total_count >= selected_count, 'reviewed inventory changed')
    require(inventory['selected_prefixes'] == ['broker_wire::tests::'],
            'only the new broker-wire prefix may execute')
    require(inventory['all'] == sorted(set(inventory['all']))
            and inventory['selected'] == sorted(set(inventory['selected'])),
            'reviewed inventory must be sorted and unique')
    require(inventory['selected'] == [n for n in inventory['all']
            if n.startswith('broker_wire::tests::')]
            and total_count - selected_count == 252,
            'inherited inventory must remain compiled/listed only')
    require(len(controls) == control_count > 0
            and len({c['name'] for c in controls}) == control_count, 'reviewed controls changed')
    prepare = types.ModuleType('prepare_preview')
    prepare.__file__ = str(HERE/'prepare-candidate.py')
    exec(compile(inputs['prepare-candidate.py'], prepare.__file__, 'exec'), prepare.__dict__)
    output = prepare.prepare(args.emu_source.resolve())
    print('Private snapshot:', output, flush=True)
    (output/'inputs').mkdir(exist_ok=True)
    for name, data in inputs.items():
        (output/'inputs'/name).write_bytes(data)
    original = {name: (output/'tree'/name).read_bytes() for name in pins}
    require({n: sha(d) for n, d in original.items()} == pins, 'captured compilation inputs differ from pins')
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('AZ_', 'AZMB_', 'CARGO_TARGET_', 'RUSTFLAGS', 'RUSTDOCFLAGS'))
           and k not in {'CARGO_ENCODED_RUSTFLAGS', 'CARGO_BUILD_RUSTFLAGS',
                         'RUSTC_WRAPPER', 'RUSTC_WORKSPACE_WRAPPER'}}
    env['CARGO_TARGET_DIR'] = str(output/'target')
    env['CARGO_NET_OFFLINE'] = 'true'
    compiler = run_command(['rustc', '--version', '--verbose'], output/'rustc.log', env)
    require(compiler.returncode == 0, 'rustc unavailable')
    cargo = run_command(['cargo', '--version'], output/'cargo.log', env)
    require(cargo.returncode == 0, 'cargo unavailable')
    result = {
        'scope': 'Pinned actual az-preview host crate with inactive bounded wire backend, independent evidence and caller-supplied streams; actual frozen C broker through new borrowed serial dispatcher/codecs with synthetic backend. C host plain/ASan-UBSan/TSan unit cases; Rust nonsanitized host cases and private stdio-pipe C fixture. No main wiring, socket, endpoint, native adapter/driver, emulator/device, authentication, external exclusion, guest startup/acquisition, restore, durable custody or physical timing.',
        'inputs_sha256': {n: sha(d) for n, d in inputs.items()},
        'candidate_files_sha256': pins,
        'rustc': compiler.stdout.strip(), 'cargo': cargo.stdout.strip(),
        'test_inventory': inventory, 'positive_count': selected_count,
        'strict_control_count': control_count, 'runs': [],
        'raw_output_hashes': 'Exact private logs include paths, elapsed durations and runtime thread IDs; '
                             'independent functional agreement does not imply identical raw hashes.',
    }
    result['c'] = run_c(output, inputs, env, inventory, json.loads(inputs['controls.json']))
    # The production cfg is compiled separately; tests do not execute its main.
    checked = run_command(['cargo', 'check', '--locked', '--offline', '--manifest-path',
        str(output/'tree/az-preview/Cargo.toml')], output/'production-check.log', env, 180)
    require(checked.returncode == 0, 'production cfg failed to compile')
    result['production_check_exit'] = checked.returncode
    result['production_check_log_sha256'] = sha((output/'production-check.log').read_bytes())
    variants = [None] + controls
    for control in variants:
        name = control['name'] if control else 'candidate'
        directory = output/name
        directory.mkdir()
        tree = directory/'tree'
        variant = dict(original)
        if control:
            edits = control.get('edits', [control])
            require(isinstance(edits, list) and len(edits) > 0, 'empty control edits: ' + name)
            for edit in edits:
                require(edit['file'] in variant, 'mutation source outside candidate: ' + name)
                text = variant[edit['file']].decode()
                require(text.count(edit['old']) == 1, 'mutation anchor is not unique: ' + name)
                require(edit['old'] != edit['new'], 'no-op mutation: ' + name)
                variant[edit['file']] = text.replace(edit['old'], edit['new']).encode()
            require(control['test'] in inventory['selected'], 'unreviewed control test: ' + name)
        assertion_location = None
        if control:
            assertion_file = control.get('assertion_file') or control.get('file')
            require(assertion_file in variant, 'assertion source is outside candidate: ' + name)
            mutated = variant[assertion_file].decode()
            function = control['test'].rsplit('::', 1)[1]
            matches = list(re.finditer(r'\bfn\s+' + re.escape(function) + r'\s*\(', mutated))
            require(len(matches) == 1, 'ambiguous selected test source: ' + name)
            start = matches[0].start()
            following = mutated.find('#[test]', start)
            end = following if following >= 0 else len(mutated)
            anchor = control['assertion_anchor']
            require(mutated[start:end].count(anchor) == 1, 'ambiguous assertion anchor: ' + name)
            position = mutated.index(anchor, start, end)
            macros = list(re.finditer(r'\bassert(?:_eq|_ne)?!', anchor))
            require(len(macros) == 1, 'anchor must identify one assertion macro: ' + name)
            position += macros[0].start()
            line = mutated.count('\n', 0, position) + 1
            column = position - mutated.rfind('\n', 0, position)
            assertion_location = Path(assertion_file).name + ':' + str(line) + ':' + str(column) + ':'
        for path, data in variant.items():
            destination = tree/path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        compiled = run_command(['cargo', 'test', '--locked', '--offline', '--manifest-path',
            str(tree/'az-preview/Cargo.toml'), '--no-run', '--message-format=json'],
            directory/'compile.log', env, 180)
        require(compiled.returncode == 0, 'compile failure is not a passing control: ' + name)
        artifacts = [json.loads(line) for line in compiled.stdout.splitlines() if line.startswith('{')]
        executables = [a['executable'] for a in artifacts if a.get('reason') == 'compiler-artifact'
                       and a.get('executable') and a.get('profile', {}).get('test')]
        require(len(executables) == 1, 'ambiguous test executable: ' + name)
        executable = executables[0]
        listing = run_command([executable, '--list'], directory/'list.log', env, 30)
        listed = sorted(re.findall(r'^(\S+): test$', listing.stdout, re.M))
        require(listing.returncode == 0 and listed == inventory['all'], 'test inventory mismatch: ' + name)
        filters = ['--exact', control['test']] if control else inventory['selected_prefixes']
        fixtures = directory/'fixtures'
        fixtures.mkdir()
        test_env = dict(env)
        test_env['AZMB_WIRE_FIXTURE'] = str(output/'fixture-peer')
        executed = run_command([executable, *filters, '--test-threads=1'], directory/'test.log', test_env, 60)
        summary = re.search(r'^test result: (ok|FAILED)\. (\d+) passed; (\d+) failed; '
            r'(\d+) ignored; (\d+) measured; (\d+) filtered out;', executed.stdout, re.M)
        wanted = (0, 1, 0, 0, total_count-1) if control else (selected_count, 0, 0, 0, total_count-selected_count)
        require(summary and tuple(map(int, summary.groups()[1:])) == wanted, 'wrong exact summary: ' + name)
        passed = re.findall(r'^test (\S+) \.\.\. ok$', executed.stdout, re.M)
        failed = re.findall(r'^test (\S+) \.\.\. FAILED$', executed.stdout, re.M)
        require(passed == ([] if control else inventory['selected']), 'wrong passing test names: ' + name)
        require(failed == ([control['test']] if control else []), 'wrong failed test names: ' + name)
        require(executed.returncode == (101 if control else 0), 'wrong process exit: ' + name)
        if control:
            panics = list(re.finditer(r'^thread [^\n]* panicked at ([^\n]+)\n([^\n]*)',
                                      executed.stdout, re.M))
            require(panics and panics[-1].group(2).startswith('assertion '),
                    'final panic is not a functional assertion: ' + name)
            actual_location = panics[-1].group(1)
            require(actual_location == assertion_location
                    or actual_location.endswith('/' + assertion_location),
                    'final panic failed at a different assertion: ' + name)
        require(not re.search(r'fatal runtime error|stack overflow|AddressSanitizer|ThreadSanitizer|'
                              r'UndefinedBehaviorSanitizer|runtime error:', executed.stdout + executed.stderr),
                'unexpected runtime diagnostic: ' + name)
        result['runs'].append({'name': name, 'compile_exit': 0, 'test_exit': executed.returncode,
            'source_sha256': {p: sha(d) for p, d in variant.items()},
            'passed': passed, 'failed': failed, 'summary_counts': wanted,
            'expected_assertion_location': assertion_location,
            'test_output_sha256': sha((directory/'test.log').read_bytes()),
            'compile_output_sha256': sha((directory/'compile.log').read_bytes())})
        require(all((tree/p).read_bytes() == d for p, d in variant.items()), 'variant changed during run')
        print(name, 'passed=' + str(wanted[0]), 'failed=' + str(wanted[1]), flush=True)
    require(all((HERE/n).read_bytes() == d for n, d in inputs.items()), 'public inputs changed during run')
    require(all((output/'tree'/p).read_bytes() == d for p, d in original.items()), 'candidate changed during run')
    for name, expected in json.loads(inputs['parent-inputs.json']).items():
        require(sha((HERE.parent/name).read_bytes()) == expected, 'frozen input changed during run: '+name)
    raw = json.dumps(result, indent=2, sort_keys=True) + '\n'
    (output/'result.json').write_text(raw)
    if args.result:
        args.result.write_text(raw)
    print('Verified result', sha(raw.encode()), flush=True)

if __name__ == '__main__':
    main()

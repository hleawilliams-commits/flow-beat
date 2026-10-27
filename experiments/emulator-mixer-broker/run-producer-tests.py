#!/usr/bin/env python3
"""Compile/test the actual standalone Rust producer with strict source controls.

Only local rustc and private owned temporary output are used. No crates, device,
network, UI, filesystem settings or broker operations are performed by tests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import uuid

HERE = Path(__file__).resolve().parent
TESTS = [
    'altered_same_id_is_conflict_not_replacement',
    'callback_panic_poison_fails_closed',
    'concurrent_creation_assigns_one_contiguous_queue',
    'counters_never_wrap',
    'default_closed_and_bounded_capacity',
    'every_baseline_component_is_bound',
    'every_frame_ack_component_is_bound',
    'exact_adoption_ack_and_release_required',
    'exact_known_rejection_closes_but_can_transition',
    'full_queue_and_lost_message_do_not_skip_ids',
    'key_validity_and_no_same_epoch_rearm',
    'loss_retains_unknown_no_replay_or_adoption',
    'no_enqueue_or_socket_success_ack',
    'queued_work_refuses_after_kernel_sequence_exhaustion',
    'reauthoring_old_bytes_is_an_explicit_limit',
    'same_generation_settings_order_is_an_explicit_limit',
    'settings_and_closure_share_the_transition_lock',
    'stale_mpsc_and_cached_replay_cannot_retag',
    'stale_or_failed_settings_callback_is_gated',
]

def sha(data):
    return hashlib.sha256(data.encode() if isinstance(data, str) else data).hexdigest()

def once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('source anchor count differs: ' + old)
    return source.replace(old, new)

def normalized(output, run):
    # Retain originals privately. Only diagnostic file location and measured
    # duration are removed for deterministic public output fingerprints.
    output = output.replace(str(run), '<private-run>')
    return re.sub(r'finished in [0-9.]+s', 'finished in <elapsed>s', output)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rustc', default='rustc')
    parser.add_argument('--result', type=Path)
    args = parser.parse_args()
    run = HERE / 'private' / 'producer-runs' / uuid.uuid4().hex
    run.mkdir(parents=True)
    source = (HERE / 'producer.rs').read_bytes()
    runner = Path(__file__).read_bytes()
    (run / 'producer.rs').write_bytes(source)
    (run / 'runner.py').write_bytes(runner)
    src = source.decode()
    variants = [('candidate', src, None)]
    def mutation(name, old, new, test):
        variants.append((name, once(src, old, new), test))
    mutation('retag-at-dequeue',
        '        if s.last_key != Some(delivered.key) { return Err(Error::StaleKey); }',
        '        let delivered = Envelope { key: s.last_key.unwrap(), ..delivered };\n'
        '        if s.last_key != Some(delivered.key) { return Err(Error::StaleKey); }',
        'stale_mpsc_and_cached_replay_cannot_retag')
    mutation('baseline-key-only', '        if expected != observed {',
        '        if expected.key != observed.key {', 'every_baseline_component_is_bound')
    mutation('close-erases-unresolved', '        self.open = false;',
        '        self.open = false;\n        self.unresolved = None;',
        'loss_retains_unknown_no_replay_or_adoption')
    old = '''    pub fn transport_written(&self, request: Envelope) -> Result<(), Error> {
        let s = self.lock()?;
        if s.unresolved.map(|p| p.request) != Some(request) { return Err(Error::NoUnresolved); }
        Ok(())
    }'''
    new = old.replace('let s =', 'let mut s =').replace('        Ok(())', '        s.unresolved = None;\n        Ok(())')
    mutation('socket-write-is-ack', old, new, 'no_enqueue_or_socket_success_ack')
    mutation('same-epoch-readoption',
        '            if baseline.key.epoch <= old.epoch { return Err(Error::NotNewEpoch); }',
        '            if baseline.key.epoch < old.epoch { return Err(Error::NotNewEpoch); }',
        'key_validity_and_no_same_epoch_rearm')
    mutation('skip-release-barrier',
        '        if !released { return Err(Error::HeldControls); }', '        let _ = released;',
        'exact_adoption_ack_and_release_required')
    mutation('queue-full-consumes-id', '            return Err(Error::QueueFull);',
        '            s.next_request_id += 1;\n            return Err(Error::QueueFull);',
        'full_queue_and_lost_message_do_not_skip_ids')
    mutation('exhausted-ack-keeps-admission',
        '''        if s.kernel_sequence == u64::MAX || ack.request.request_id == u64::MAX - 1 {
            s.close();
        }''', '', 'queued_work_refuses_after_kernel_sequence_exhaustion')
    compiler = subprocess.run([args.rustc, '--version', '--verbose'], text=True, capture_output=True, check=True)
    (run / 'compiler.txt').write_text(compiler.stdout + compiler.stderr)
    result = {'source_sha256': sha(source), 'runner_sha256': sha(runner),
        'compiler': compiler.stdout.strip(), 'positive_tests': ['tests::' + t for t in TESTS],
        'test_count': len(TESTS), 'strict_control_count': len(variants) - 1, 'runs': [],
        'scope': 'Actual standalone std-only Rust producer; synthetic frames and mpsc/thread callbacks. No preview/forwarder patch, real broker/FFI/ABI, I/O, native delivery, settings ordering, receipt authenticity, hardware release, queue drain or recovery proof.',
        'fingerprint_normalization': 'Private run root and libtest elapsed duration only; exact output retained privately.'}
    for name, body, test in variants:
        directory = run / name
        directory.mkdir()
        file = directory / 'producer.rs'
        file.write_text(body)
        exe = directory / 'fixture'
        compiled = subprocess.run([args.rustc, '--edition=2021', '--test', '-D', 'warnings', str(file), '-o', str(exe)], text=True, capture_output=True)
        (directory / 'compile.txt').write_text(compiled.stdout + compiled.stderr)
        if compiled.returncode:
            raise RuntimeError('compile failed: ' + str(directory))
        listing = subprocess.run([str(exe), '--list'], text=True, capture_output=True, timeout=30)
        (directory / 'list.txt').write_text(listing.stdout + listing.stderr)
        listed = re.findall(r'^(tests::\w+): test$', listing.stdout, re.M)
        if listing.returncode or listed != result['positive_tests']:
            raise RuntimeError('test inventory mismatch: ' + str(directory))
        command = [str(exe), '--test-threads=1']
        if test:
            command += ['--exact', 'tests::' + test]
        executed = subprocess.run(command, text=True, capture_output=True, timeout=45)
        output = executed.stdout + executed.stderr
        (directory / 'output.txt').write_text(output)
        successes = re.findall(r'^test (tests::\w+) \.\.\. ok$', executed.stdout, re.M)
        failed = re.findall(r'^test (tests::\w+) \.\.\. FAILED$', executed.stdout, re.M)
        summary = re.search(r'^test result: (ok|FAILED)\. (\d+) passed; (\d+) failed; (\d+) ignored; (\d+) measured; (\d+) filtered out;', executed.stdout, re.M)
        expected = (0, 1, 0, 0, len(TESTS)-1) if test else (len(TESTS), 0, 0, 0, 0)
        if not summary or tuple(map(int, summary.groups()[1:])) != expected:
            raise RuntimeError('wrong summary: ' + str(directory))
        if executed.returncode != (101 if test else 0) or successes != ([] if test else result['positive_tests']) or failed != (['tests::'+test] if test else []):
            raise RuntimeError('wrong exact result: ' + str(directory))
        if re.search(r'AddressSanitizer|ThreadSanitizer|UndefinedBehaviorSanitizer|runtime error:|fatal runtime error|stack overflow', output):
            raise RuntimeError('unexpected runtime diagnostic: ' + str(directory))
        result['runs'].append({'name': name, 'source_sha256': sha(body),
            'passed': expected[0], 'failed': failed, 'filtered': expected[4],
            'exit_code': executed.returncode,
            'normalized_output_sha256': sha(normalized(output, run))})
        print(name, 'passed='+str(expected[0]), 'failed='+str(expected[1]), flush=True)
    if (HERE / 'producer.rs').read_bytes() != source or Path(__file__).read_bytes() != runner:
        raise RuntimeError('inputs changed during run; all snapshots retained')
    raw = json.dumps(result, sort_keys=True, indent=2) + '\n'
    (run / 'result.json').write_text(raw)
    if args.result:
        args.result.write_text(raw)
    print('RESULT', sha(raw), 'private snapshot', run)

if __name__ == '__main__':
    main()

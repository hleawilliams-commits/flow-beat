#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline pure-policy tests. Raw host templates/settings are never published.

Only this runner reads supplied local source files. It never imports the driver,
config, preview, runtime adapters, device transport or saved execution receipts.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import types
import uuid

HERE = Path(__file__).resolve().parent
checks = []
rejections = []


def check(ok, name):
    if not ok:
        raise AssertionError(name)
    checks.append(name)


def reject(name, call, reason=None):
    try:
        call()
    except ValueError as error:
        check(reason is None or str(error) == reason, name + ': rejected as expected')
        rejections.append({'name': name, 'reason': str(error)})
    else:
        raise AssertionError(name + ': unexpectedly accepted')


def load(raw, name='pure_overlay_v2'):
    module = types.ModuleType(name)
    exec(compile(raw, name, 'exec'), module.__dict__)
    return module


def literal(node):
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.List):
        return [literal(x) for x in node.elts]
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        return literal(node.left) + literal(node.right)
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Mult):
        return literal(node.left) * literal(node.right)
    raise ValueError('unexpected pinned literal AST')


def idle_from_driver(raw):
    values = [literal(n.value) for n in ast.parse(raw).body if isinstance(n, ast.Assign)
              and any(isinstance(t, ast.Name) and t.id == 'SPI3_IDLE' for t in n.targets)]
    check(len(values) == 1, 'single literal driver template')
    return bytes(values[0])


def analog_schema(raw):
    text = raw.decode()
    at = re.search(r'pub const MX_AT:.*?= \[(.*?)\n\];', text, re.S)
    ten = re.search(r'pub const MX10:.*?= &\[(.*?)\n\];', text, re.S)
    check(at is not None and ten is not None, 'two named analog declarations')
    pairs = re.findall(r'\((0x[0-9a-f]+), (?:Some\(\((0x[0-9a-f]+), ([0246])\)\)|(None))\)', at.group(1))
    more = re.findall(r'\(\w+, (0x[0-9a-f]+), (0x[0-9a-f]+), ([0246]), "[^"]+"\)', ten.group(1))
    check(len(pairs) == 17 and len(more) == 23, 'complete 17 MX_AT and 23 MX10 entries')
    highs, masks = set(), {}
    for high, low, shift, absent in pairs:
        highs.add(int(high, 16))
        if not absent:
            low, shift = int(low, 16), int(shift)
            check(masks.get(low, 0) & (3 << shift) == 0, 'distinct MX_AT packed bits ' + high)
            masks[low] = masks.get(low, 0) | (3 << shift)
    for high, low, shift in more:
        highs.add(int(high, 16)); low, shift = int(low, 16), int(shift)
        check(masks.get(low, 0) & (3 << shift) == 0, 'distinct MX10 packed bits ' + high)
        masks[low] = masks.get(low, 0) | (3 << shift)
    check(len(highs) == 40 and len(masks) == 11 and not highs.intersection(masks), '40 high plus 11 independent packed bytes')
    check('pub const FX_TIME_BYTES: (usize, usize) = (0x1a, 0x1b);' in text, 'FX TIME pair source binding')
    check('f[lo] = (f[lo] & !(3 << shift)) | (((v & 3) as u8) << shift);' in text, 'set10 preserves other packed bits')
    return highs, masks


def stamp(p, frame):
    result = bytearray(frame)
    result[96:98] = p.crc16_x25(result[:96]).to_bytes(2, 'little')
    return bytes(result)


def encoded(state):
    return json.dumps({str(k): v for k, v in state.items()}, sort_keys=True).encode()


def payload_oracle(p, observed, kind):
    original = bytes(observed)
    first, release = p.action_payloads(observed, kind)
    target = observed[34] + {'browse': 0, 'cw': 1, 'ccw': -1}[kind]
    expected_first, expected_release = bytearray(observed), bytearray(observed)
    expected_first[6] = 0
    if kind == 'browse':
        expected_first[32] = 0x40
    expected_first[34] = expected_release[34] = target
    check(type(first) is bytes and type(release) is bytes, kind + ': immutable payload pair')
    check(first == bytes(expected_first), kind + ': first differs only by approved deltas')
    check(release == bytes(expected_release), kind + ': release retains target and every other observed byte')
    check(observed == original, kind + ': input not mutated')
    check(first[26:28] == release[26:28] == observed[26:28], kind + ': independent FX TIME unchanged')
    check(first[96:98] == release[96:98] == observed[96:98], kind + ': observed cosmetic CRC copied unchanged')


def main(args, out):
    source = (HERE / 'overlay-policy.py').read_bytes()
    runner = Path(__file__).read_bytes()
    p = load(source)
    driver, controls, config, frames = (Path(x).read_bytes() for x in (args.driver, args.controls, args.config, args.frames))
    inputs = {'policy': hashlib.sha256(source).hexdigest(), 'runner': hashlib.sha256(runner).hexdigest()}
    for name, raw, expected in [('driver', driver, p.DRIVER_SHA256), ('controls', controls, p.CONTROLS_SHA256),
                                 ('config', config, p.CONFIG_SHA256),
                                 ('frames', frames, '4d9d833e583b7c6a5aa3f12de0fea2b7245e9dac915477ab96b9cfdfa75c2c0b')]:
        digest = hashlib.sha256(raw).hexdigest()
        check(digest == expected, 'exact ' + name + ' source hash')
        inputs[name] = digest
    # Private immutable source snapshots permit independent reproduction; never tracked.
    for name, raw in [('overlay-policy.py', source), ('test-policy.py', runner), ('driver.py', driver),
                      ('controls.rs', controls), ('config.rs', config), ('frames.h', frames)]:
        (out / name).write_bytes(raw)
    (out / 'inputs.json').write_text(json.dumps(inputs, sort_keys=True, indent=2) + '\n')
    tree = ast.parse(source)
    imports = [n.names[0].name for n in ast.walk(tree) if isinstance(n, ast.Import)]
    check(imports == ['hashlib', 'json', 're'] and not any(isinstance(n, ast.ImportFrom) for n in ast.walk(tree)), 'policy imports only pure standard utilities')
    check(not any(isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id in ('open', 'exec', 'eval', '__import__', 'input') for n in ast.walk(tree)), 'no direct ambient I/O or dynamic execution calls in policy')
    idle = idle_from_driver(driver)
    check(len(idle) == 152 and hashlib.sha256(idle).hexdigest() == p.IDLE_SHA256, 'exact source-extracted idle template')
    highs, masks = analog_schema(controls)
    check(p.ANALOG_HIGH == highs and p.ANALOG_LOW_MASKS == masks, 'policy exactly matches source-derived analog masks')
    expected = highs | set(masks) | {22, 26, 27}
    check(p.MIXER_OFFSETS == expected and len(expected) == 54, 'complete 54 supported offsets only')
    check(p.VALUE_MASKS == {**dict.fromkeys(highs, 255), **masks, 22: 240, 26: 255, 27: 255}, 'all selected value masks exact')
    check(idle[16] & 252 == idle[80] & 15 == idle[22] & 15 == 0, 'reserved and active bits pinned zero')
    protected = bytes(v & ~(255 if k in (34, 96, 97) else p.VALUE_MASKS.get(k, 0)) for k, v in enumerate(idle))
    check(hashlib.sha256(protected).hexdigest() == p.PROTECTED_SHA256, 'masked protected-template digest independently derived')
    check(p.crc16_x25(b'123456789') == 0x906e, 'CRC X25 known answer')
    check(p.parse_mixer_state(None) == p.parse_mixer_state(b'{}') == {}, 'optional missing or empty settings')
    check(p.parse_mixer_state(b' { "10": 0, "22": 240 }\n') == {10: 0, 22: 240}, 'canonical JSON whitespace')

    states = [{}, {k: 0 for k in expected}, dict(p.VALUE_MASKS), {k: ((k * 71 + 93) & p.VALUE_MASKS[k]) for k in expected}]
    states += [{26: value & 255, 27: value >> 8} for value in (0, 1, 32767, 32768, 65535)]
    for index, state in enumerate(states):
        before = dict(state)
        check(p.parse_mixer_state(encoded(state)) == state, 'settings round trip ' + str(index))
        planned = p.neutral_frame(idle, state, 128)
        check(state == before and len(planned) == 152, 'constructor retains caller state ' + str(index))
        check(all(planned[k] == state.get(k, idle[k]) for k in range(152) if k != 34), 'complete settings representation without normalization ' + str(index))
        observed = stamp(p, planned)
        check(p.validate_frames(observed, observed, planned) == 128, 'paired full-frame comparison ' + str(index))
        for kind in ('browse', 'cw', 'ccw'):
            payload_oracle(p, observed, kind)

    # Every legal byte value is covered at the three restricted offsets. No clamping.
    for offset, mask in ((16, 3), (80, 240), (22, 240)):
        for value in range(256):
            state = {offset: value}
            if value & ~mask:
                reject(f'reserved state {offset}/{value}', lambda s=state: p.parse_mixer_state(encoded(s)), 'active or reserved mixer bits')
            else:
                check(p.parse_mixer_state(encoded(state)) == state, f'allowed packed value {offset}/{value}')
    for offset in (26, 27):
        reject('partial FX TIME ' + str(offset), lambda k=offset: p.parse_mixer_state(encoded({k: 0})), 'FX TIME requires both bytes or neither')
        reject('partial direct FX TIME ' + str(offset), lambda k=offset: p.neutral_frame(idle, {k: 0}, 128), 'FX TIME requires both bytes or neither')
    for offset in range(152):
        if offset not in expected:
            reject('unreviewed persisted offset ' + str(offset), lambda k=offset: p.parse_mixer_state(encoded({k: idle[k]})), 'unreviewed mixer offset')

    invalid = [('empty', b''), ('oversize', b' ' * 4097), ('text', '{}'), ('mutable', bytearray(b'{}')),
               ('utf8', b'\xff'), ('JSON', b'{'), ('BOM', b'\xef\xbb\xbf{}'), ('array', b'[]'), ('null', b'null'),
               ('duplicate', b'{"10":1,"10":2}'), ('escaped duplicate', b'{"10":1,"\\u0031\\u0030":2}'),
               ('leading zero', b'{"010":1}'), ('signed key', b'{"+10":1}'), ('negative key', b'{"-10":1}'),
               ('space key', b'{" 10":1}'), ('unicode key', b'{"\\u0661\\u0660":1}'),
               ('float', b'{"10":1.0}'), ('bool', b'{"10":true}'), ('string', b'{"10":"1"}'),
               ('negative', b'{"10":-1}'), ('oversized byte', b'{"10":256}'), ('value null', b'{"10":null}'),
               ('nested', b'{"10":{"10":1}}'), ('list value', b'{"10":[1]}'),
               ('nan', b'{"10":NaN}'), ('infinity', b'{"10":Infinity}'), ('deep', b'[' * 1200 + b']' * 1200)]
    for name, raw in invalid:
        reject('malformed ' + name, lambda raw=raw: p.parse_mixer_state(raw))
    for state in ([], {True: 1}, {'10': 1}, {10: True}, {16: 4}, {80: 1}, {22: 2}, {10: 1.5}, dict.fromkeys(range(55), 0)):
        reject('direct settings ' + repr(state)[:30], lambda state=state: p.neutral_frame(idle, state, 128))
    for value in (0, 255, -1, 256, True, 1.0, '128'):
        reject('invalid counter ' + repr(value), lambda value=value: p.neutral_frame(idle, {}, value), 'counter outside 1..254')
    changed = bytearray(idle); changed[10] ^= 1
    reject('different constructor template', lambda: p.neutral_frame(bytes(changed), {}, 128), 'idle template hash mismatch')
    plan = p.neutral_frame(idle, states[3], 128)
    observed = stamp(p, plan)
    for slot in range(3):
        for name, bad in [('short', observed[:-1]), ('long', observed+b'\0'), ('mutable', bytearray(observed))]:
            values = [observed, observed, plan]; values[slot] = bad
            reject(f'frame length/type {slot}/{name}', lambda values=values: p.validate_frames(*values))
    for offset in (96, 97):
        changed = bytearray(observed); changed[offset] ^= 1
        reject('CRC change ' + str(offset), lambda changed=bytes(changed): p.validate_frames(changed, observed, plan), 'mixer CRC-16/X25 mismatch')
    for counter in (1, 203, 254):
        copied = bytearray(plan); copied[34] = counter; copied = stamp(p, bytes(copied))
        check(p.validate_frames(copied, copied, plan) == counter, 'observed independent browse baseline ' + str(counter))
    changed = bytearray(observed); changed[34] += 1; changed = stamp(p, bytes(changed))
    reject('differing observed browse counters', lambda: p.validate_frames(observed, changed, plan), 'observed counters differ')
    cosmetic = bytearray(plan); cosmetic[96] ^= 1; cosmetic[97] ^= 2
    check(p.validate_frames(observed, observed, bytes(cosmetic)) == 128, 'planned cosmetic CRC ignored')

    # All protected bits, including the uncovered tail, must stay pinned even if
    # both observed copies and the proposed plan agree on the corrupt value.
    for offset in range(152):
        mask = 255 if offset in (34, 96, 97) else p.VALUE_MASKS.get(offset, 0)
        for bit in range(8):
            if mask & (1 << bit):
                continue
            changed = bytearray(observed); changed[offset] ^= 1 << bit; changed = stamp(p, bytes(changed))
            reject(f'protected bit matched plan {offset}/{bit}', lambda changed=changed: p.validate_frames(changed, changed, changed))
            reject(f'protected action baseline {offset}/{bit}', lambda changed=changed: p.action_payloads(changed, 'browse'))
    # Each compared byte independently rejects a changed observation; restamp
    # CRC so the comparison/shape, not a stale checksum, must reject it.
    for offset in range(152):
        if offset in (34, 96, 97):
            continue
        changed = bytearray(observed); changed[offset] ^= 1; changed = stamp(p, bytes(changed))
        reject('full equality byte ' + str(offset), lambda changed=changed: p.validate_frames(changed, observed, plan))
    for value in (0, 255):
        changed = bytearray(observed); changed[34] = value; changed = stamp(p, bytes(changed))
        reject('action baseline counter ' + str(value), lambda changed=changed: p.action_payloads(changed, 'browse'))
    for kind, counter in (('cw', 254), ('ccw', 1)):
        changed = bytearray(observed); changed[34] = counter; changed = stamp(p, bytes(changed))
        reject('action no wrap ' + kind, lambda kind=kind, changed=changed: p.action_payloads(changed, kind), 'counter outside 1..254')
    for kind in ('align', 'CW', '', None, 1, True):
        reject('invalid kind ' + repr(kind), lambda kind=kind: p.action_payloads(observed, kind), 'unsupported action')
    for raw in (observed[:-1], observed+b'\0', bytearray(observed)):
        reject('action immutable length', lambda raw=raw: p.action_payloads(raw, 'browse'))
    changed = bytearray(observed); changed[96] ^= 1
    reject('action invalid CRC', lambda: p.action_payloads(bytes(changed), 'browse'), 'mixer CRC-16/X25 mismatch')
    match = re.search(rb'az_frame_spi3_mixer\[152\]\s*=\s*\{([^}]+)\}', frames)
    check(match is not None, 'kernel placeholder source selected')
    kernel = bytes(int(v.strip(), 16) for v in match.group(1).split(b',') if v.strip())
    reject('kernel placeholder not normalized', lambda: p.validate_frames(stamp(p, kernel), stamp(p, kernel), plan))

    # Strict mutation controls run only targeted valid operations; no unrelated
    # assertion or sanitizer failure can satisfy the discrimination.
    controls_result = []
    mutations = [
        ('drop-fx-time', '        result[offset] = value', '        if offset not in (26,27): result[offset] = value',
         lambda m: m.neutral_frame(idle, {26: 171, 27: 205}, 128)[26:28] == bytes((171,205))),
        ('ignore-fx-time-comparison', 'COMPARISON_EXCLUDED = CRC_BYTES | {34}', 'COMPARISON_EXCLUDED = CRC_BYTES | {34,26,27}', None),
        ('old-counter-release', 'first[34] = release[34] = target', 'first[34] = target',
         lambda m: m.action_payloads(observed, 'cw')[1][34] == observed[34]+1),
        ('erase-packed-low', '    first[6] = 0', '    first[6] = 0\n    first[40] = release[40] = 0',
         lambda m: m.action_payloads(observed, 'browse')[0][40] == observed[40]),
    ]
    for name, old, new, oracle in mutations:
        check(source.decode().count(old) == 1, 'strict mutation has exact one anchor ' + name)
        mutant_raw = source.decode().replace(old, new).encode(); mutant = load(mutant_raw, name)
        if name == 'ignore-fx-time-comparison':
            # Masked template digest must reflect mutant's intended whitelist;
            # otherwise a stale binding, not removed equality, would trip first.
            mutant.PROTECTED_SHA256 = hashlib.sha256(bytes(v & ~(255 if k in mutant.COMPARISON_EXCLUDED else mutant.VALUE_MASKS.get(k,0)) for k,v in enumerate(idle))).hexdigest()
            foreign = bytearray(observed); foreign[26] ^= 1; foreign = stamp(mutant, bytes(foreign))
            accepted = mutant.validate_frames(foreign, foreign, plan)
            check(accepted == 128, 'mutant accepts independently changed FX TIME')
            reject('unmodified refuses independently changed FX TIME', lambda: p.validate_frames(foreign, foreign, plan))
        else:
            check(oracle(p), 'unmodified targeted invariant ' + name)
            check(not oracle(mutant), 'strict mutant loses targeted invariant ' + name)
        (out / ('mutant-'+name+'.py')).write_bytes(mutant_raw)
        controls_result.append({'name':name,'status':'DISCRIMINATED','mutant_sha256':hashlib.sha256(mutant_raw).hexdigest()})
    check((HERE/'overlay-policy.py').read_bytes() == source and Path(__file__).read_bytes() == runner, 'sources unchanged during run')
    result = {'schema':1,'status':'PASS','policy_version':2,'checks':len(checks),'rejections':len(rejections),
              'strict_mutation_controls':controls_result,'inputs':inputs,
              'scope':'pure candidate functions; pinned host-source literal/schema extraction; synthetic copied frames and local files only',
              'device_or_network_access':False,'runtime_wiring_changed':False,'raw_templates_published':False,
              'supported_offsets':54,'analog_high_offsets':40,'packed_low_offsets':11,
              'check_names':checks,'rejection_cases':rejections}
    (out/'result.json').write_text(json.dumps(result,sort_keys=True,indent=2)+'\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('driver','controls','config','frames'):
        parser.add_argument('--'+name, required=True)
    parser.add_argument('--out-parent', default=str(HERE/'out-history'))
    args = parser.parse_args()
    parent = Path(args.out_parent); parent.mkdir(parents=True, exist_ok=True)
    out = parent/('run-'+uuid.uuid4().hex); out.mkdir()
    try:
        result = main(args,out)
    except BaseException as error:
        (out/'failure.json').write_text(json.dumps({'type':type(error).__name__,'error':str(error)},sort_keys=True,indent=2)+'\n')
        print('FAILED',out)
        raise
    print(json.dumps({'out':str(out),'checks':result['checks'],'rejections':result['rejections'],
                      'strict_controls':len(result['strict_mutation_controls']),
                      'result_sha256':hashlib.sha256((out/'result.json').read_bytes()).hexdigest()},sort_keys=True))

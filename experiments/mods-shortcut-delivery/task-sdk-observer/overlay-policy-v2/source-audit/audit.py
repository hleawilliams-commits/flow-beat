#!/usr/bin/env python3
"""Offline source binding. Never import the driver or read saved user settings."""
import argparse
import ast
import hashlib
import io
import json
import re
from pathlib import Path

PINS = {
    'az-preview/src/controls.rs': 'b9145e4b41439761abbfdec461b854412748af2fe95183fd5843ad34d4298445',
    'az-preview/src/config.rs': '0a1986611ec2642379a42f1f517d8d1decf1764e0cee8ab2f37c3e6c04782992',
    'az-preview/src/app.rs': 'f6a7bb28c3f2dbce759582ac2c7b1147adf93f5137838a833ed5d35f1834864b',
    'tools/az-drive.py': '6f88798ec54785dc2ce9180e5c2839a1a8e8ec370c997bc7f48e8d1cc06e640f',
}
FUNCTIONS = {
    'controls.rs': {
        'mixer_levels': '7546f78ba4a30872e675b5676532e5e269de6b191af1f04ddb366ff9d1d3d527',
        'initial_mixer_frame': '8c01adcb324a897899e35aba78fb0e01ce573fd83a85c41fd397fef2d3cf442d',
        'set10': 'f1abb42e79d55ae82940ebd9121d0ceb227879c93851792c2ff457f323ff6da3',
        'get10': '8e3078d4072b9c15a70b0364e1ecf76d67526f8a6e65244ccf62f5c157d2d6e0',
        'gray4': 'c0f204f809c85bb98c084f22b9175622e4e8634f7518a79825c3ace8a465b395',
    },
    'config.rs': {
        'mixer_state_store': 'a3c1c6776ba88265474178b1968c941452898b630d42ed7a623e03af25a5a49a',
        'parse_byte_map': 'dd3f2135e589dacf2c1ebd1f69005289d643113c00dc6023cf0846a6d544fcf9',
    },
    'app.rs': {
        'fx_select': 'ae6eab34a3baf65138a631b61514a5198b27f37c0cfc6ec7f200191d36b395d8',
        'fx_time': '13852256a9077d125c399d8a04501ae938e32b6779d3503579ffe9334dd46d80',
        'send_frames': '7c5297a058455c5e3ba9ddb24361c5bc91e06980a0bea8231c3e32c134d82b76',
    },
}
PY_FUNCTIONS = {
    '_mixer': '12ec85e1f5848e55804a83167668ed10b23a70978aaae2dca73e9538e2d7efd5',
    '_mixer_state': '70b331639ae9e5933baa62e54232c84174e06b0c8cc9d79afaf1639f4f1707bb',
    'mixer_set': 'c281098cdcdbe15366ab6e0bcdedf4cdacc42890a697bbc383b500f7445fde85',
}
EXPECTED_AT = [(10,14,6),(11,14,4),(36,40,6),(41,45,6),(37,40,4),
 (38,40,2),(39,40,0),(42,45,4),(43,45,2),(44,45,0),(61,65,6),
 (62,65,4),(78,None,None),(15,16,0),(12,14,2),(13,14,0),(56,60,6)]
EXPECTED_10 = [(46,50,6),(47,50,4),(48,50,2),(49,50,0),(51,55,6),
 (52,55,4),(53,55,2),(54,55,0),(57,60,4),(58,60,2),(59,60,0),
 (63,65,2),(64,65,0),(66,70,6),(67,70,4),(68,70,2),(69,70,0),
 (71,75,6),(72,75,4),(73,75,2),(74,75,0),(76,80,6),(77,80,4)]
EXPECTED_MASKS = {14:255,16:3,40:255,45:255,50:255,55:255,60:255,
                  65:255,70:255,75:255,80:240}
CHECKS = []
CONTROLS = []


def check(ok, label):
    if not ok:
        raise ValueError(label)
    CHECKS.append(label)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def rust_fn(s, name):
    """Bounded lexical function extraction, including brace character literals."""
    m = re.search(r'\bfn\s+' + re.escape(name) + r'\s*\(', s)
    if not m:
        raise ValueError('missing function ' + name)
    start = m.start()
    i = s.index('{', m.end())
    depth = 0
    while i < len(s):
        if s.startswith('//', i):
            i = s.index('\n', i)
            continue
        if s.startswith('/*', i):
            i = s.index('*/', i + 2) + 2
            continue
        if s[i] == '"':
            i += 1
            while i < len(s):
                if s[i] == '\\':
                    i += 2
                elif s[i] == '"':
                    i += 1
                    break
                else:
                    i += 1
            continue
        char = re.match(r"'(?:[^'\\\n]|\\.)'", s[i:]) if s[i] == "'" else None
        if char:
            i += len(char[0])
            continue
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
            if depth == 0:
                return s[start:i+1]
        i += 1
    raise ValueError('unterminated function ' + name)


def table(s, name):
    m = re.search(r'pub const ' + name + r'\b[^=]*=\s*&?\[(.*?)\];', s, re.S)
    if not m:
        raise ValueError('missing table ' + name)
    return m[1]


def scalar(s, name):
    m = re.search(r'pub const ' + name + r'\b[^=]*=\s*(0x[0-9a-f]+|[0-9]+);', s)
    if not m:
        raise ValueError('missing scalar ' + name)
    return int(m[1], 0)


def pair(s, name):
    m = re.search(r'pub const ' + name + r'\b[^=]*=\s*\((0x[0-9a-f]+|[0-9]+),\s*(0x[0-9a-f]+|[0-9]+)\);', s)
    if not m:
        raise ValueError('missing pair ' + name)
    return tuple(int(v, 0) for v in m.groups())


def extract_schema(s):
    at = []
    for line in table(s, 'MX_AT').splitlines():
        m = re.fullmatch(r'\s*\((0x[0-9a-f]+),\s*(?:Some\(\((0x[0-9a-f]+),\s*([0-9]+)\)\)|None)\),\s*//\s*(.*?)\s*', line)
        if not line.strip():
            continue
        if not m:
            raise ValueError('unrecognized MX_AT row')
        at.append((int(m[1],0), int(m[2],0) if m[2] else None,
                   int(m[3]) if m[3] else None, m[4]))
    ten = []
    for line in table(s, 'MX10').splitlines():
        m = re.fullmatch(r'\s*\([A-Z0-9_]+,\s*(0x[0-9a-f]+),\s*(0x[0-9a-f]+),\s*([0-9]+),\s*"([^"]+)"\),\s*', line)
        if not line.strip():
            continue
        if not m:
            raise ValueError('unrecognized MX10 row')
        ten.append((int(m[1],0),int(m[2],0),int(m[3]),m[4]))
    check([v[:3] for v in at] == EXPECTED_AT, 'MX_AT exact coordinates')
    check([v[:3] for v in ten] == EXPECTED_10, 'MX10 exact coordinates')
    masks = {}
    rows = at + ten
    for hi,lo,shift,_ in rows:
        if lo is not None:
            masks[lo] = masks.get(lo,0) | (3 << shift)
    check(masks == EXPECTED_MASKS, 'packed low masks')
    highs = sorted(r[0] for r in rows)
    analog = sorted(set(highs) | set(masks))
    check(len(highs) == len(set(highs)) == 40, 'forty distinct high bytes')
    check(not set(highs) & set(masks), 'high and low fields disjoint')
    check(len(analog) == 51, 'fifty-one analog serialized bytes')
    check(pair(s,'FX_TIME_BYTES') == (26,27), 'independent FX TIME LE pair')
    check(scalar(s,'MX_FXSEL_BYTE') == 22, 'FX selector byte')
    check(scalar(s,'KNOB_BYTE') == 34, 'browse counter byte')
    check(pair(s,'KNOB_PUSH') == (31,1), 'browse push bit')
    check(scalar(s,'LATCH_BYTE') == 6, 'shared latch byte')
    check(pair(s,'MIXER_SETTINGS') == (24,16), 'settings bit')
    check((scalar(s,'XPAD_POS_BYTE'),scalar(s,'XPAD_ZONE_BYTE')) == (29,30), 'X-PAD fields')
    buttons = []
    for name,count in [('PANEL_BUTTONS',14),('MIXER_BUTTONS',31)]:
        found = re.findall(r'pb\([A-Z0-9_]+,\s*(0x[0-9a-f]+|[0-9]+),\s*(0x[0-9a-f]+),\s*"([^"]+)"\)',table(s,name))
        check(len(found) == count, name + ' count')
        buttons += [(int(b,0),int(m,0),n) for b,m,n in found]
    buttons += [(31,1,'BROWSE PUSH'),(24,16,'MIXER SETTINGS'),(22,1,'FX ON/OFF')]
    active = {}
    for b,m,_ in buttons:
        check(m and m & (m-1) == 0, 'single button bit')
        check(not active.get(b,0) & m, 'distinct button bit')
        active[b] = active.get(b,0) | m
    check(active == {20:191,21:255,22:15,23:255,24:252,31:1,32:252,33:255}, 'exact active control masks')
    allowed = sorted(set(analog) | {22,26,27})
    check(len(allowed) == 54, 'proposed complete saved offset set')
    return {'analog_controls':[{'name':n,'high':h,'low':l,'low_shift':sh} for h,l,sh,n in rows],
            'analog_offsets':analog,'analog_high_offsets':highs,'packed_low_masks':masks,
            'buttons':[{'name':n,'offset':b,'mask':m} for b,m,n in buttons],
            'active_masks':active,'proposed_saved_offsets':allowed}


def bind_functions(sources):
    for file,items in FUNCTIONS.items():
        for name,expected in items.items():
            check(digest(rust_fn(sources['az-preview/src/'+file],name).encode()) == expected,
                  file+' function '+name)
    tree = ast.parse(sources['tools/az-drive.py'])
    nodes = {n.name:n for n in tree.body if isinstance(n,ast.FunctionDef)}
    for name,expected in PY_FUNCTIONS.items():
        check(digest(ast.dump(nodes[name],include_attributes=False).encode()) == expected,
              'driver AST '+name)
    return tree,nodes


def pin_sources(sources):
    for path,expected in PINS.items():
        check(digest(sources[path].encode()) == expected, 'whole source '+path)


def reject(name, operation):
    before = len(CHECKS)
    try:
        operation()
    except (ValueError,KeyError,SyntaxError):
        del CHECKS[before:]
        CONTROLS.append(name)
    else:
        raise AssertionError('negative control accepted: '+name)


def driver_tests(tree,nodes,idle,offsets):
    env = {'SPI3_IDLE':list(idle),'_mixer_state':lambda:{},'_knob_get':lambda:137}
    mod = ast.Module(body=[nodes['_mixer']],type_ignores=[])
    exec(compile(mod,'<selected _mixer AST>','exec'),env)
    mix = env['_mixer']
    f = mix()
    check(len(f) == 152 and f[34] == 137, 'actual mixer counter override')
    check(f[:34]+f[35:] == list(idle[:34]+idle[35:]), 'actual mixer absent fields use idle')
    saved = {i:(i*37+11)&255 for i in offsets}
    env['_mixer_state'] = lambda:saved
    f = mix()
    check(all(f[i] == v for i,v in saved.items()), 'actual mixer preserves all fifty-four overlay values')
    check(all(f[i] == idle[i] for i in range(152) if i not in offsets and i != 34), 'actual mixer untouched bytes')
    env['_mixer_state'] = lambda:{34:1,32:64,6:0,151:99,-1:42,152:42}
    f = mix()
    check(f[34] == 137, 'saved browse counter replaced from separate counter')
    check((f[6],f[32],f[151]) == (0,64,99), 'actual driver allows active and tail override: policy required')
    check(f[0] == idle[0] and len(f) == 152, 'actual driver ignores out-of-range offsets')
    f = mix(32,128,True)
    check(f[6] == 0 and f[32] == 128, 'actual driver pressed byte assignment replaces mask')
    check(idle[6] == 8 and idle[29] == 255 and idle[30] == 0 and idle[35] == 0, 'idle latch X-PAD and byte35')
    check(idle[16] & 252 == 0 and idle[80] & 15 == 0, 'reserved packed bits idle zero')
    check(all(idle[i] == 0 for i in (20,21,23,24,31,32,33)) and idle[22]&15 == 0, 'idle momentary controls released')
    check(set((p^(p>>1)) for p in range(16)) == set(range(16)), 'all high nibbles represent a Gray selector position')
    env = {'json':json,'_MIXER_STATE':'synthetic-only','open':lambda _:io.StringIO('{"22":258,"32":64,"026":3,"26":4,"10":true}')}
    exec(compile(ast.Module(body=[nodes['_mixer_state']],type_ignores=[]),'<selected _mixer_state AST>','exec'),env)
    check(env['_mixer_state']() == {22:2,32:64,26:4,10:1}, 'actual driver coercion and canonical-key collision are permissive')
    env['open'] = lambda _:io.StringIO('{"26":3,"26":9}')
    check(env['_mixer_state']() == {26:9}, 'actual JSON duplicate silently takes last: strict parser required')
    env['open'] = lambda _:io.StringIO('malformed')
    check(env['_mixer_state']() == {}, 'actual malformed saved state falls back empty')


def run(root):
    sources = {p:(root/p).read_bytes().decode('utf-8') for p in PINS}
    pin_sources(sources)
    schema = extract_schema(sources['az-preview/src/controls.rs'])
    tree,nodes = bind_functions(sources)
    idle_node = next(n.value for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='SPI3_IDLE' for t in n.targets))
    # Only this pinned literal arithmetic expression is evaluated; no imports or calls.
    check(all(isinstance(n,(ast.Expression,ast.BinOp,ast.Add,ast.Mult,ast.List,ast.Constant,ast.Load)) for n in ast.walk(ast.Expression(idle_node))), 'idle AST limited to literal arithmetic')
    idle = bytes(eval(compile(ast.Expression(idle_node),'<idle literal>','eval'),{'__builtins__':{}}))
    rust_idle = bytes(int(v,0) for v in re.findall(r'0x[0-9a-f]+|\d+',table(sources['az-preview/src/controls.rs'],'SPI3_IDLE')))
    check(len(idle) == 152 and idle == rust_idle, 'Python and Rust pinned idle identity')
    driver_tests(tree,nodes,idle,schema['proposed_saved_offsets'])
    for path in PINS:
        mutated = dict(sources); mutated[path] += '\n'
        reject('whole-source drift '+path,lambda m=mutated:pin_sources(m))
    for file,items in FUNCTIONS.items():
        for name in items:
            mutated = dict(sources); path='az-preview/src/'+file
            body=rust_fn(mutated[path],name)
            mutated[path]=mutated[path].replace(body,body.replace('{','{ ',1),1)
            reject('selected-function drift '+file+':'+name,lambda m=mutated:bind_functions(m))
    for name in PY_FUNCTIONS:
        mutated=dict(sources)
        mutated['tools/az-drive.py']=mutated['tools/az-drive.py'].replace('def '+name+'(', 'def changed_'+name+'(',1)
        reject('missing driver AST '+name,lambda m=mutated:bind_functions(m))
    s=sources['az-preview/src/controls.rs']
    # These controls bypass whole-file hashes and exercise the extracted semantics.
    mutations = {
        'analog high alias':('(0x0a, Some','(0x0b, Some'),
        'analog low displacement':('(0x0e, 6)','(0x10, 6)'),
        'analog low shift':('(0x0e, 6)','(0x0e, 4)'),
        'unknown low bits made supported':('(0x10, 0)','(0x10, 2)'),
        'headphone reserved bits made supported':('(MIXER_HP_LEVEL, 0x4d, 0x50, 4','(MIXER_HP_LEVEL, 0x4d, 0x50, 2'),
        'FX time swapped':('(0x1a, 0x1b);','(0x1b, 0x1a);'),
        'FX selector moved':('MX_FXSEL_BYTE: usize = 0x16','MX_FXSEL_BYTE: usize = 0x17'),
        'browse counter moved':('KNOB_BYTE: usize = 34','KNOB_BYTE: usize = 35'),
        'button duplicated':('pb(FX_FREQ_LOW, 0x16, 0x02','pb(FX_FREQ_LOW, 0x16, 0x01'),
        'button invades selector':('pb(FX_FREQ_LOW, 0x16, 0x02','pb(FX_FREQ_LOW, 0x16, 0x10'),
        'latch moved':('LATCH_BYTE: usize = 6','LATCH_BYTE: usize = 7'),
        'X-PAD moved':('XPAD_ZONE_BYTE: usize = 0x1e','XPAD_ZONE_BYTE: usize = 0x1f'),
    }
    for label,(old,new) in mutations.items():
        if old not in s: raise AssertionError('control not applied '+label)
        reject(label,lambda text=s.replace(old,new,1):extract_schema(text))
    return {
        'scope':'offline pinned-source extraction and selected Python AST execution with synthetic inputs; no driver import, saved settings, network, runtime or firmware',
        'source_sha256':PINS,'rust_function_sha256':FUNCTIONS,'python_function_ast_sha256':PY_FUNCTIONS,
        'idle_sha256':digest(idle),'checks_passed':len(CHECKS),'negative_controls_passed':len(CONTROLS),
        'negative_controls':CONTROLS,'schema':schema,
        'proposed_value_contract':{
            'strict_byte_values':'integer 0..255, excluding booleans and coercion',
            'packed_reserved_bits':{'16':'outside 0x03 equal pinned idle zero','80':'outside 0xf0 equal pinned idle zero'},
            'FX_selector_22':'high nibble preserved; all low-nibble button bits zero',
            'FX_TIME_26_27':'both saved bytes present or both absent; preserve 16-bit LE counter bit pattern unchanged',
            'unknown_keys':'reject even when value equals idle; do not discard or normalize',
            'absent_fields':'driver uses pinned idle; this is only a planned default until full fresh frame equality validates it',
            'action_preservation':'only latch6, BROWSE bit6 of byte32, or browse counter34 may change; every other byte copied from validated observed frame',
        },
        'limits':[
            'Derived coordinate meanings are pinned source claims, not newly validated physical or native behavior.',
            'Function fingerprints bind manually reviewed Rust code; Rust functions are not executed here.',
            'The source union matches the reported count of54; no saved user keys or values were read by this verifier.',
            'Preview serialization merges existing keys; arbitrary driver offsets mean saved-file provenance is unproved.',
            'FX TIME is an independent endless encoder, not a browse counter or static analog level.',
            'Exact byte preservation prevents new unrelated byte changes relative to the validated baseline, not later external-writer changes or native side effects.',
            'No input authorization, live readiness, successful send, native delivery, controller ownership, or hardware timing follows from this audit.',
        ],
    }


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--source-root',type=Path,required=True)
    parser.add_argument('--result',type=Path,required=True)
    args=parser.parse_args()
    result=run(args.source_root)
    args.result.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
    print(json.dumps({'checks_passed':result['checks_passed'],'negative_controls_passed':result['negative_controls_passed'],'result_sha256':digest(args.result.read_bytes())},sort_keys=True))

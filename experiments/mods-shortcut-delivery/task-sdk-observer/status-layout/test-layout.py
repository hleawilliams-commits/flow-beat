#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact local ELF + fabricated maps/auxv and in-memory rejection controls.

No runtime module import, process access, transport or firmware execution.
"""
import argparse
import copy
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
from elftools.elf.elffile import ELFFile
import layout as target

checks = []
controls = []


def check(value, label):
    if not value:
        raise AssertionError(label)
    checks.append(label)


def reject(label, reason, fn):
    try:
        fn()
    except ValueError as error:
        check(str(error) == reason, label + ': exact rejection')
        controls.append(dict(name=label, rejected_by=reason))
    else:
        raise AssertionError(label + ': accepted')


def aux(page=4096):
    return struct.pack('<QQQQQQ', 3, 0x1000, 6, page, 0, 0)


def synthetic_maps(model, page, *, bias=0x70000000, split=False, gaps=False):
    """All returned addresses and inode values are fabricated."""
    down = lambda value: value & ~(page - 1)
    up = lambda value: (value + page - 1) & ~(page - 1)
    rows = []
    for segment in model['loads']:
        start = bias + down(segment['vaddr'])
        file_end = bias + up(segment['vaddr'] + segment['filesz'])
        memory_end = bias + up(segment['vaddr'] + segment['memsz'])
        offset = down(segment['offset'])
        if segment['filesz']:
            if segment['flags'] & 2 and split and start + page < file_end:
                rows.append(f'{start:x}-{start+page:x} r--p {offset:x} 08:01 7 {PATH}')
                start += page
                offset += page
            perms = 'rw-p' if segment['flags'] & 2 else 'r-xp'
            rows.append(f'{start:x}-{file_end:x} {perms} {offset:x} 08:01 7 {PATH}')
        if memory_end > file_end:
            rows.append(f'{file_end:x}-{memory_end:x} rw-p 0 00:00 0')
    if gaps:
        a, b = model['loads']
        lo = bias + up(a['vaddr'] + a['memsz'])
        hi = bias + down(b['vaddr'])
        if lo < hi:
            rows.append(f'{lo:x}-{hi:x} ---p {lo-bias:x} 08:01 7 {PATH}')
    return '\n'.join(rows)


PATH = '/synthetic/encoder-task-observer.so'


def main(binary):
    raw = binary.read_bytes()
    frozen_source = (target.MODULE / 'observer.cpp').read_bytes()
    model = target.binary_layout(binary)
    receipt = json.loads((target.MODULE / 'build-result.json').read_text())
    check(receipt['binary_sha256'] == target.BINARY_SHA256, 'pinned build receipt binary')
    check(receipt['source_sha256']['observer.cpp'] == target.SOURCE_SHA256,
          'pinned build receipt adapter source')
    check(len(model['symbols']) == 8 and sum(s['size'] for s in model['symbols'].values()) == 56,
          'eight named fields / 56 exact bytes')
    check(not any(name in model['symbols'] for name in ('storage', 'published', 'core', 'az_task_call_original', 'az_task_exec_original')),
          'native pointers and entire transport storage excluded')
    for label, (value, width) in target.FIELDS.items():
        check(model['symbols'][label] == dict(name=target.NAMES[label], value=value, size=width),
              'actual local OBJECT offset/width ' + label)

    plans = []
    for page, split, gaps in ((4096, False, False), (4096, True, True),
                              (16384, False, True), (65536, False, False)):
        maps = synthetic_maps(model, page, split=split, gaps=gaps)
        result = target.plan_reads(binary, maps, PATH, aux(page))
        check(result['load_bias'] == 0x70000000 and result['page_size'] == page,
              f'fabricated layout bias/page {page}/{split}/{gaps}')
        check(result['owned_scalar_bytes'] == 56 and len(result['reads']) == 8
              and not result['coherent_snapshot'] and not result['native_objects']
              and not result['transport_storage'] and not result['record_slots']
              and not result['follows_pointers'],
              f'bounded noncoherent plan {page}/{split}/{gaps}')
        check(all(v['address'] == 0x70000000 + target.FIELDS[k][0]
                  and v['size'] == target.FIELDS[k][1] and v['mapping'] == 'file'
                  for k, v in result['reads'].items()),
              f'exact fabricated read ranges {page}/{split}/{gaps}')
        plans.append(dict(page_size=page, relro_split=split, inaccessible_reservation=gaps,
                          planned_fields=8, planned_bytes=56, mapping='file-backed final-page BSS'))

    samples = {label: ((1 << (size * 8)) - 1).to_bytes(size, 'little')
               for label, (_, size) in target.FIELDS.items()}
    decoded = target.decode_samples(samples)
    check(all(decoded[label] == (1 << (size * 8)) - 1
              for label, (_, size) in target.FIELDS.items()), 'unsigned full-width buffer decoding')
    check(target.page_size(aux() + bytes(32)) == 4096, 'zero auxiliary padding allowed')
    for label in target.FIELDS:
        changed = dict(samples); changed[label] = samples[label][:-1]
        reject('short sample ' + label, 'scalar sample width mismatch: ' + label,
               lambda c=changed: target.decode_samples(c))
    changed = dict(samples); changed['heartbeat'] += b'\0'
    reject('oversized sample', 'scalar sample width mismatch: heartbeat', lambda: target.decode_samples(changed))
    for label, change in [('extra sample', lambda c: c.update(storage=b'\0'*8)),
                           ('missing sample', lambda c: c.pop('state'))]:
        changed = dict(samples); change(changed)
        reject(label, 'sample allowlist mismatch', lambda c=changed: target.decode_samples(c))

    for label, value, reason in [
        ('empty auxv', b'', 'invalid AArch64 auxv width'),
        ('short auxv', aux()[:-1], 'invalid AArch64 auxv width'),
        ('missing page size', bytes(16), 'missing or duplicate AT_PAGESZ'),
        ('missing terminator', aux()[:-16], 'missing or duplicate AT_PAGESZ'),
        ('duplicate page size', aux()[:-16] + struct.pack('<QQQQ',6,4096,0,0), 'missing or duplicate AT_PAGESZ'),
        ('page after terminator', aux() + struct.pack('<QQ',6,4096), 'nonzero auxv after terminator'),
        ('nonzero terminator', aux()[:-8] + struct.pack('<Q',1), 'invalid auxv terminator'),
        ('zero page size', aux(0), 'unsupported AArch64 page size'),
        ('nonpower page size', aux(6000), 'unsupported AArch64 page size'),
        ('unsupported small page', aux(1024), 'unsupported AArch64 page size'),
    ]:
        reject(label, reason, lambda v=value: target.page_size(v))

    changed_source = frozen_source + b'\n'
    reject('source drift', 'adapter source hash mismatch', lambda: target.source_contract(changed_source))
    with tempfile.TemporaryDirectory(prefix='eto-offline-layout-') as tmp:
        changed_binary = Path(tmp) / 'mutated.so'
        changed_binary.write_bytes(raw[:-1] + bytes([raw[-1] ^ 1]))
        reject('binary drift', 'observer binary hash mismatch', lambda: target.binary_layout(changed_binary))

    # In-memory structural controls reach extraction behind the public hash gate.
    elf = ELFFile(io.BytesIO(raw))
    table = elf.get_section_by_name('.symtab')
    offsets = {s.name: table['sh_offset'] + i * table['sh_entsize']
               for i, s in enumerate(table.iter_symbols()) if s.name}
    state = offsets[target.NAMES['state']]
    core = offsets['_ZN3etoL4coreE']
    state_name = struct.unpack_from('<I', raw, state)[0]
    patches = [
        ('wrong architecture', 18, '<H', 62, 'expected AArch64 LE shared object'),
        ('wrong ELF type', 16, '<H', 2, 'expected AArch64 LE shared object'),
        ('scalar function type', state+4, '<B', 2, 'not a local defined OBJECT: state'),
        ('global scalar binding', state+4, '<B', 17, 'not a local defined OBJECT: state'),
        ('undefined scalar', state+6, '<H', 0, 'not a local defined OBJECT: state'),
        ('missing scalar', state, '<I', 0, 'missing or ambiguous scalar: state'),
        ('duplicate scalar', core, '<I', state_name, 'missing or ambiguous scalar: state'),
        ('pointer alias instead of scalar', state+8, '<Q', 0x13088, 'scalar identity/offset/width mismatch: state'),
        ('wrong scalar width', state+16, '<Q', 8, 'scalar identity/offset/width mismatch: state'),
        ('scalar outside BSS', state+8, '<Q', 0, 'scalar outside writable non-executable BSS: state'),
    ]
    for label, offset, fmt, value, reason in patches:
        changed = bytearray(raw); struct.pack_into(fmt, changed, offset, value)
        reject(label, reason, lambda c=bytes(changed): target._extract(c))
    changed = bytearray(raw)
    last_load = [i for i,s in enumerate(elf.iter_segments()) if s['p_type']=='PT_LOAD'][-1]
    struct.pack_into('<Q', changed, elf['e_phoff'] + last_load*elf['e_phentsize'] + 32, len(raw)+1)
    reject('truncated segment', 'missing or truncated PT_LOAD file range', lambda: target._extract(bytes(changed)))

    maps = synthetic_maps(model, 4096)
    base_rows = target._reference().parse_maps(maps)
    def render(rows):
        return '\n'.join(f'{r["start"]:x}-{r["end"]:x} {r["perms"]} {r["offset"]:x} {r["dev"]} {r["inode"]} {r["path"]}'.rstrip() for r in rows)
    def mutate_row(index, **updates):
        rows=copy.deepcopy(base_rows); rows[index].update(updates); return render(rows)
    map_controls = [
        ('shared module mapping', mutate_row(1,perms='rw-s'), 'module mapping is not private'),
        ('mixed file identity', mutate_row(1,inode=8), 'missing or mixed module identity'),
        ('deleted module', maps.replace(PATH,PATH+' (deleted)'), 'deleted module mapping'),
        ('unaligned map', mutate_row(1,start=base_rows[1]['start']+1), 'unaligned maps row'),
        ('wrong file displacement', mutate_row(1,offset=base_rows[1]['offset']+4096), 'module map does not match PT_LOAD file range or inaccessible reservation'),
        ('missing executable anchor', render(base_rows[1:]), 'missing or ambiguous executable anchor'),
        ('incomplete scalar file prefix', render(base_rows[:1]), 'scalar PT_LOAD file prefix is incomplete'),
        ('scalar readonly map', mutate_row(1,perms='r--p'), 'scalar mapping is not private read/write: state'),
        ('scalar unreadable map', mutate_row(1,perms='---p'), 'scalar not readable in one map'),
        ('oversized named file map', mutate_row(1,end=base_rows[1]['end']+4096), 'module map does not match PT_LOAD file range or inaccessible reservation'),
        ('overlapping maps', maps+'\n'+maps.splitlines()[0], 'overlapping map rows'),
        ('malformed maps', 'not maps', 'malformed maps row'),
    ]
    for label, text, reason in map_controls:
        reject(label, reason, lambda t=text: target._plan_model(model,t,PATH,4096))
    for path in ('/synthetic/other.so', 'relative/encoder-task-observer.so', '/synthetic/../encoder-task-observer.so'):
        reject('unexpected module path '+path, 'unexpected module path', lambda p=path: target._plan_model(model,maps,p,4096))
    for label, change, reason in [
        ('added embedded transport scalar', lambda m:m['symbols'].update(reserved=dict(name='storage+12',value=0x130c4,size=4)), 'scalar allowlist mismatch'),
        ('missing diagnostic', lambda m:m['symbols'].pop('worker_cycles'), 'scalar allowlist mismatch'),
        ('altered named width', lambda m:m['symbols']['heartbeat'].update(size=4), 'scalar identity/offset/width mismatch: heartbeat'),
        ('altered named offset', lambda m:m['symbols']['heartbeat'].update(value=0x130b0), 'scalar identity/offset/width mismatch: heartbeat'),
        ('scalar executable segment', lambda m:m['loads'][1].update(flags=7), 'scalar outside unique writable non-executable PT_LOAD'),
    ]:
        changed = copy.deepcopy(model); change(changed)
        reject(label, reason, lambda m=changed: target._plan_model(m,maps,PATH,4096))

    check(binary.read_bytes()==raw and (target.MODULE/'observer.cpp').read_bytes()==frozen_source,
          'inputs unchanged through offline tests')
    return dict(result='PASS', checks=len(checks), failures=0,
                strict_rejection_control_count=len(controls), strict_rejection_controls=controls,
                check_labels=checks, binary_sha256=target.BINARY_SHA256,
                adapter_source_sha256=target.SOURCE_SHA256, reference_planner_sha256=target.REFERENCE_SHA256,
                source_sha256={name:hashlib.sha256((target.HERE/name).read_bytes()).hexdigest()
                               for name in ('layout.py','test-layout.py')},
                symbols=model['symbols'], loads=model['loads'], owned_scalar_bytes=56,
                synthetic_maps=plans, runtime_maps_observed=False, process_access=False,
                transport_storage=False, native_objects=False, coherent_snapshot=False,
                scope='Offline exact ELF extraction, supplied auxv/maps planning, and buffer validation only')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--result',type=Path)
    args=parser.parse_args()
    result=main(args.binary)
    output=json.dumps(result,indent=2,sort_keys=True)+'\n'
    if args.result: args.result.write_text(output)
    else: print(output,end='')

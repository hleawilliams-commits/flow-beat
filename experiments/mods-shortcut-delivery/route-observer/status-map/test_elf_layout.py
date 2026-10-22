#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic ELF extraction checks; optional local built-observer inspection.

No process inspection or transport. The optional ELF's maps are fabricated for
an offline integration check, never presented as observed runtime mappings.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
from layout import elf_layout, parse_maps, plan_reads

checks = 0

def check(value, label):
    global checks
    checks += 1
    if not value:
        raise AssertionError(label)


def fixture(*, elfclass=2, big=False, machine=183, etype=3, table=True,
            duplicate=False, kind=1, section_index=1, size=4, value=0x12100,
            data_flags=3, truncate_load=False, loads=True):
    raw = bytearray(0x3000)
    order = '>' if big else '<'
    ident = b'\x7fELF' + bytes([elfclass, 2 if big else 1, 1, 0]) + b'\x00' * 8
    struct.pack_into(order+'16sHHIQQQIHHHHHH', raw, 0, ident, etype, machine,
                     1, 0, 64, 0x2700, 0, 64, 56, 2, 64, 6, 5)
    for off, values in [(64, (1 if loads else 4, 5, 0, 0, 0, 0x1cc0, 0x1cc0, 0x1000)),
                        (120, (1 if loads else 4, 6, 0x1d50, 0x11d50, 0x11d50,
                               0x4000 if truncate_load else 0x500,
                               0x4000 if truncate_load else 0x2800, 0x1000))]:
        struct.pack_into(order+'IIQQQQQQ', raw, off, *values)
    strings = b'\x00state\x00sequence\x00'
    raw[0x2300:0x2300+len(strings)] = strings
    symbols = [(0,0,0,0,0,0), (1,kind,0,section_index,value,size),
               (7,1,0,2,0x13108,8)]
    if duplicate:
        symbols.append(symbols[1])
    for i, symbol in enumerate(symbols):
        struct.pack_into(order+'IBBHQQ', raw, 0x2340+i*24, *symbol)
    section_names = b'\x00.data\x00.bss\x00.strtab\x00.symtab\x00.shstrtab\x00'
    if not table:
        section_names = section_names.replace(b'.symtab', b'.hidden')
    raw[0x2500:0x2500+len(section_names)] = section_names
    sections = [(0,0,0,0,0,0,0,0,0,0),
                (1,1,data_flags,0x11d50,0x1d50,0x500,0,0,8,0),
                (7,8,3,0x12250,0x2250,0x2300,0,0,8,0),
                (12,3,0,0,0x2300,len(strings),0,0,1,0),
                (20,2,0,0,0x2340,len(symbols)*24,3,len(symbols),8,24),
                (28,3,0,0,0x2500,len(section_names),0,0,1,0)]
    # Names are measured from the string table rather than assumed offsets.
    for index, name in enumerate((b'.data', b'.bss', b'.strtab', b'.symtab' if table else b'.hidden', b'.shstrtab'),1):
        values=list(sections[index]); values[0]=section_names.index(name)
        struct.pack_into(order+'IIQQQQIIQQ',raw,0x2700+index*64,*values)
    return bytes(raw)


def run_fixture_tests(directory):
    path=directory/'synthetic.so'
    names={'state':'state','sequence':'sequence'}
    path.write_bytes(fixture())
    model=elf_layout(path,names)
    check(model['symbols']['state']['value']==0x12100, '32-bit OBJECT value')
    check(model['symbols']['state']['size']==4, '32-bit OBJECT width')
    check(model['symbols']['sequence']['size']==8, '64-bit NOBITS OBJECT width')
    check(model['sha256']==hashlib.sha256(path.read_bytes()).hexdigest(), 'ELF hash binding')
    check(len(model['loads'])==2 and model['loads'][1]['vaddr']-model['loads'][1]['offset']==0x10000, 'distinct RW displacement')
    cases=[('ELF32',{'elfclass':1}), ('big endian',{'big':True}),
           ('wrong architecture',{'machine':62}), ('ET_EXEC',{'etype':2}),
           ('missing symtab',{'table':False}), ('duplicate symbol',{'duplicate':True}),
           ('function symbol',{'kind':2}), ('undefined symbol',{'section_index':0}),
           ('absolute symbol',{'section_index':0xfff1}), ('invalid section index',{'section_index':9}),
           ('nonallocated section',{'data_flags':1}), ('outside defining section',{'value':0x13000}),
           ('unsupported width',{'size':16}), ('truncated load file range',{'truncate_load':True}),
           ('no PT_LOAD',{'loads':False})]
    for label, options in cases:
        path.write_bytes(fixture(**options))
        try:
            elf_layout(path,names)
        except ValueError:
            check(True,label)
        else:
            check(False,label)
    path.write_bytes(fixture())
    try:
        elf_layout(path,{'missing':'absent'})
    except ValueError:
        check(True,'missing requested symbol')
    else:
        check(False,'missing requested symbol')


def inspect_local_binary(path):
    fields=('state live heartbeat native_attempted native_state_gated native_live_gated '
            'dropped producer_busy nested_dropped write_index read_index off_state off_skipped tid_failures '
            'raw_attempted raw_state_gated raw_live_gated raw_heartbeat raw_invalid raw_dropped '
            'raw_nested_dropped raw_producer_busy raw_write_index raw_read_index sequence raw_sequence').split()
    model=elf_layout(path,{name:f'_ZN3eroL{len(name)}{name}E' for name in fields})
    for label, symbol in model['symbols'].items():
        check(symbol['size']==(8 if label in ('sequence','raw_sequence') else 4), 'actual OBJECT width '+label)
    page=4096
    down=lambda v:v & ~(page-1)
    up=lambda v:(v+page-1) & ~(page-1)
    bias=0x70000000
    module='/synthetic/encoder-route-observer.so'
    rows=[]
    for seg in model['loads']:
        start=bias+down(seg['vaddr'])
        file_end=bias+up(seg['vaddr']+seg['filesz'])
        memory_end=bias+up(seg['vaddr']+seg['memsz'])
        if seg['filesz']:
            # Split writable data at its first page to exercise RELRO union.
            if seg['flags'] & 2:
                first_end=min(start+page,file_end)
                rows.append(f'{start:x}-{first_end:x} r--p {down(seg["offset"]):08x} 08:01 7 {module}')
                if first_end<file_end:
                    rows.append(f'{first_end:x}-{file_end:x} rw-p {down(seg["offset"])+first_end-start:08x} 08:01 7 {module}')
            else:
                perms='r-xp' if seg['flags'] & 1 else 'r--p'
                rows.append(f'{start:x}-{file_end:x} {perms} {down(seg["offset"]):08x} 08:01 7 {module}')
        if memory_end>file_end:
            rows.append(f'{file_end:x}-{memory_end:x} rw-p 00000000 00:00 0')
    result=plan_reads(model,parse_maps('\n'.join(rows)),module,page)
    check(result['load_bias']==bias and len(result['reads'])==26,'actual ELF plus fabricated mappings')
    return dict(binary_sha256=model['sha256'], symbol_count=len(model['symbols']),
                scalar_widths={label:symbol['size'] for label,symbol in model['symbols'].items()},
                loads=model['loads'], synthetic_map_page_size=page, runtime_maps_observed=False,
                planned_mapping_counts={kind:sum(v['mapping']==kind for v in result['reads'].values()) for kind in ('file','anonymous-bss')})


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary',type=Path)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='az-offline-elf-fixture-') as tmp:
        run_fixture_tests(Path(tmp))
    fixture_checks=checks
    actual=inspect_local_binary(args.binary) if args.binary else None
    print(json.dumps(dict(checks=checks, failures=0, fixture_checks=fixture_checks,
        actual_binary_checks=checks-fixture_checks, actual_binary=actual,
        source_sha256=hashlib.sha256(Path(__file__).with_name('layout.py').read_bytes()).hexdigest(),
        scope='offline ELF extraction and synthetic maps only'),indent=2))

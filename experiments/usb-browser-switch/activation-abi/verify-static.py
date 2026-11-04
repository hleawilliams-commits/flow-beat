#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce bounded ABI/teardown facts from the private exact ELF.

No native call, hook, emulator access, compilation or previous test replay.
The reviewer pins bind complete extents. Anchors and inventories do not prove
transitive ABI transparency, live receiver identity, retention or cancellation.
"""
import argparse
import hashlib
import io
import json
import re
import struct
from collections import Counter
from pathlib import Path

import capstone
import elftools
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from elftools.dwarf.callframe import FDE
from elftools.elf.elffile import ELFFile


def require(condition, label):
    if not condition:
        raise ValueError(label)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def number(value):
    return int(value, 0) if isinstance(value, str) else value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--pins', type=Path, default=Path(__file__).with_name('review-pins.json'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists(), 'refuse output replacement')
    pin_bytes = args.pins.read_bytes()
    pins = json.loads(pin_bytes)
    raw = args.elf.read_bytes()
    require(sha(raw) == pins['elf_sha256'], 'exact ELF SHA-256')
    elf = ELFFile(io.BytesIO(raw))
    require(elf.elfclass == 64 and elf.little_endian and
            elf['e_machine'] == 'EM_AARCH64' and elf['e_type'] == 'ET_EXEC', 'ELF identity')
    segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']

    def read(at, size):
        matches = [s for s in segments if s['p_vaddr'] <= at and
                   at + size <= s['p_vaddr'] + s['p_filesz']]
        require(size >= 0 and len(matches) == 1, 'single file-backed segment')
        offset = matches[0]['p_offset'] + at - matches[0]['p_vaddr']
        return raw[offset:offset + size]

    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = True
    eh = elf.get_section_by_name('.eh_frame')
    require(eh is not None, 'EH frame section')
    fdes = {}
    for entry in elf.get_dwarf_info().EH_CFI_entries():
        if isinstance(entry, FDE):
            at = entry['initial_location']
            fdes.setdefault(at, []).append(entry)

    def lsda(fde):
        at = fde.lsda_pointer
        if at is None:
            return None
        pos = at

        def byte():
            nonlocal pos
            out = read(pos, 1)[0]
            pos += 1
            return out

        def uleb():
            value = 0
            for shift in range(0, 70, 7):
                b = byte()
                value |= (b & 0x7f) << shift
                if b < 128:
                    require(value < 2**64, 'bounded ULEB')
                    return value
            raise ValueError('unterminated ULEB')

        # This parser intentionally supports only these reviewed cleanup tables.
        require([byte(), byte(), byte()] == [255, 255, 1], 'LSDA encodings')
        size = uleb()
        require(size < 4096, 'bounded LSDA call-site table')
        end = pos + size
        start = fde['initial_location']
        limit = start + fde['address_range']
        rows = []
        while pos < end:
            offset, length, landing, action = [uleb() for _ in range(4)]
            require(pos <= end, 'call-site table boundary')
            require(length > 0 and start <= start + offset < start + offset + length <= limit,
                    'call-site interval in FDE')
            require(landing == 0 or start <= start + landing < limit, 'landing in FDE')
            require(action == 0, 'cleanup-only action')
            rows.append({'start': hex(start + offset), 'length': length,
                         'landing': hex(start + landing) if landing else None})
        require(pos == end, 'complete LSDA table')
        require(all(number(a['start']) + a['length'] <= number(b['start'])
                    for a, b in zip(rows, rows[1:])), 'ordered nonoverlapping call-site intervals')
        return {'address': hex(at), 'size': end - at, 'sha256': sha(read(at, end - at)),
                'callsite_bytes': size, 'rows': len(rows),
                'cleanup_rows': sum(x['landing'] is not None for x in rows),
                'no_landing_rows': sum(x['landing'] is None for x in rows),
                'distinct_landing_pcs': len({x['landing'] for x in rows if x['landing']}),
                'all_actions_zero': True, 'type_table_present': False,
                'intervals': rows}

    reports = {}
    for name, spec in pins['functions'].items():
        at, size = number(spec['address']), number(spec['size'])
        require(at in fdes and len(fdes[at]) == 1, name + ' unique FDE')
        fde = fdes[at][0]
        require(fde['address_range'] == size, name + ' FDE extent')
        code = read(at, size)
        require(sha(code) == spec['sha256'], name + ' complete code hash')
        instructions = list(md.disasm(code, at))
        require(len(instructions) * 4 == size and
                all(i.size == 4 and i.address == at + n * 4 for n, i in enumerate(instructions)),
                name + ' full fixed-width decode')
        counts = Counter(i.mnemonic for i in instructions)
        explicit_simd = sorted({r for i in instructions for r in
                                re.findall(r'\b(?:[bhsdqv][0-9]+)\b', i.op_str)})
        external_b = [hex(i.address) for i in instructions if i.mnemonic == 'b' and
                      not at <= int(i.op_str[1:], 16) < at + size]
        fde_address = eh['sh_addr'] + fde.offset
        cie_address = eh['sh_addr'] + fde.cie.offset
        # These entries use the 32-bit initial-length format; the length excludes itself.
        require(read(fde_address, 4) != b'\xff' * 4 and read(cie_address, 4) != b'\xff' * 4,
                '32-bit CFI record length')
        facts = {'address': hex(at), 'size': size, 'sha256': sha(code),
                 'instructions': len(instructions), 'direct_calls': counts['bl'],
                 'indirect_calls': counts['blr'], 'returns':
                 [hex(i.address) for i in instructions if i.mnemonic == 'ret'],
                 'indirect_branches': counts['br'], 'external_plain_branches': external_b,
                 'explicit_simd_registers': explicit_simd,
                 'fde_address': hex(fde_address), 'fde_size': fde['length'] + 4,
                 'fde_sha256': sha(read(fde_address, fde['length'] + 4)),
                 'cie_address': hex(cie_address), 'cie_size': fde.cie['length'] + 4,
                 'cie_sha256': sha(read(cie_address, fde.cie['length'] + 4)),
                 'cie_augmentation': fde.cie['augmentation'].decode('ascii'),
                 'cfi_rows': len(fde.get_decoded().table),
                 'lsda_pointer': hex(fde.lsda_pointer) if fde.lsda_pointer else None,
                 'lsda_decoded': 'lsda' in spec,
                 'lsda': lsda(fde) if 'lsda' in spec else None}
        for key, expected in spec.get('facts', {}).items():
            require(facts[key] == expected, name + ' fact: ' + key)
        for key, expected in spec.get('lsda', {}).items():
            require(facts['lsda'] is not None and facts['lsda'][key] == expected,
                    name + ' LSDA: ' + key)
        for call in spec.get('no_landing_calls', []):
            call = number(call)
            matches = [row for row in facts['lsda']['intervals']
                       if number(row['start']) <= call and call + 4 <= number(row['start']) + row['length']]
            require(len(matches) == 1 and matches[0]['landing'] is None,
                    name + ' explicit no-local-landing call-site')
        # Public summaries retain boundaries/counts; the full decoded code stays private.
        if facts['lsda'] is not None:
            del facts['lsda']['intervals']
        reports[name] = facts

    for anchor in pins['anchors']:
        at = number(anchor['address'])
        ins = list(md.disasm(read(at, 4), at))
        require(len(ins) == 1 and [ins[0].mnemonic, ins[0].op_str] ==
                [anchor['mnemonic'], anchor['operands']], 'anchor ' + hex(at))
    for pointer in pins['pointers']:
        value = struct.unpack('<Q', read(number(pointer['address']), 8))[0]
        require(value == number(pointer['value']), 'pointer ' + pointer['address'])
    for string in pins['strings']:
        value = string['value'].encode('ascii') + b'\0'
        require(read(number(string['address']), len(value)) == value, 'RTTI string')
    relocations = elf.get_section_by_name('.rela.plt')
    require(relocations is not None, 'PLT relocation section')
    symbols = elf.get_section(relocations['sh_link'])
    imported = {r['r_offset']: symbols.get_symbol(r['r_info_sym']).name
                for r in relocations.iter_relocations()}
    for relation in pins['plt_symbols']:
        at = number(relation['address'])
        ins = list(md.disasm(read(at, 16), at))
        require(len(ins) == 4 and [x.mnemonic for x in ins] == ['adrp', 'ldr', 'add', 'br'],
                'reviewed PLT shape')
        require(ins[0].op_str.startswith('x16, #') and ins[1].op_str.startswith('x17, [x16') and
                ins[3].op_str == 'x17', 'reviewed PLT registers')
        slot = ins[0].operands[1].imm + ins[1].operands[1].mem.disp
        require(imported.get(slot) == relation['symbol'], 'PLT imported symbol')
    result = {'schema': 1, 'scope': 'static exact-ELF ABI and concrete timer teardown only',
              'elf_sha256': pins['elf_sha256'], 'review_pins_sha256': sha(pin_bytes),
              'verifier_sha256': sha(Path(__file__).read_bytes()),
              'tools': {'capstone': capstone.__version__, 'pyelftools': elftools.__version__},
              'functions': reports, 'anchors': len(pins['anchors']),
              'pointer_relations': len(pins['pointers']), 'rtti_strings': len(pins['strings']),
              'plt_symbol_relations': len(pins['plt_symbols']),
              'native_calls': 0, 'runtime_claims': 0, 'runtime_gate': 'CLOSED',
              'wrapper_abi_and_unwind_proven': False,
              'native_completion_or_cancellation_proven': False}
    with args.output.open('x') as stream:
        stream.write(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(json.dumps({'functions': len(reports), 'anchors': result['anchors'],
                      'runtime_gate': result['runtime_gate']}))


if __name__ == '__main__':
    main()

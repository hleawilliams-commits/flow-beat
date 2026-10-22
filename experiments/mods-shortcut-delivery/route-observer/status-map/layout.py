#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline plan for named scalar OBJECTs in one AArch64 shared object.

No process access or transport. Caller must bind the supplied maps/ELF to the
same live process and unchanged file. Returned reads are not a coherent snapshot.
"""
from pathlib import Path
import hashlib
import io


def require(value, reason):
    if not value:
        raise ValueError(reason)


def parse_maps(text):
    rows = []
    for line in text.splitlines():
        fields = line.split(None, 5)
        require(len(fields) >= 5, 'malformed maps row')
        span, perms, offset, dev, inode = fields[:5]
        start, end = (int(v, 16) for v in span.split('-'))
        require(0 <= start < end <= 1 << 64 and len(perms) == 4, 'invalid map bounds/permissions')
        require(perms[0] in 'r-' and perms[1] in 'w-' and perms[2] in 'x-' and perms[3] in 'ps', 'invalid permissions')
        offset, inode = int(offset, 16), int(inode)
        require(0 <= offset < 1 << 64 and inode >= 0, 'invalid offset/inode')
        rows.append(dict(start=start, end=end, perms=perms, offset=offset,
                         dev=dev, inode=inode, path=fields[5] if len(fields) == 6 else ''))
    rows.sort(key=lambda r: r['start'])
    require(all(a['end'] <= b['start'] for a, b in zip(rows, rows[1:])), 'overlapping map rows')
    return rows


def elf_layout(path, names):
    from elftools.elf.elffile import ELFFile
    raw = Path(path).read_bytes()
    elf = ELFFile(io.BytesIO(raw))
    require(elf.elfclass == 64 and elf.little_endian and elf['e_machine'] == 'EM_AARCH64'
            and elf['e_type'] == 'ET_DYN', 'expected AArch64 LE shared object')
    table = elf.get_section_by_name('.symtab')
    require(table is not None, 'full symbol table required')
    symbols = {}
    for label, name in names.items():
        found = table.get_symbol_by_name(name) or []
        require(len(found) == 1, 'missing or ambiguous symbol: ' + name)
        s = found[0]
        require(s['st_info']['type'] == 'STT_OBJECT' and isinstance(s['st_shndx'], int)
                and 0 < s['st_shndx'] < elf.num_sections(), 'not a defined OBJECT: ' + name)
        section = elf.get_section(s['st_shndx'])
        require(section['sh_flags'] & 2 and section['sh_addr'] <= s['st_value']
                and s['st_value'] + s['st_size'] <= section['sh_addr'] + section['sh_size'],
                'symbol outside its allocated section: ' + name)
        require(s['st_size'] in (4, 8), 'not an allowed scalar width: ' + name)
        symbols[label] = dict(name=name, value=s['st_value'], size=s['st_size'])
    loads = [dict(vaddr=s['p_vaddr'], offset=s['p_offset'], filesz=s['p_filesz'],
                  memsz=s['p_memsz'], flags=s['p_flags']) for s in elf.iter_segments()
             if s['p_type'] == 'PT_LOAD']
    require(loads and all(s['offset'] + s['filesz'] <= len(raw) for s in loads),
            'missing or truncated PT_LOAD file range')
    return dict(sha256=hashlib.sha256(raw).hexdigest(), loads=loads, symbols=symbols)


def plan_reads(layout, rows, module_path, page_size=4096):
    require(page_size > 0 and page_size & (page_size-1) == 0, 'invalid page size')
    down = lambda n: n & ~(page_size-1)
    up = lambda n: (n + page_size-1) & ~(page_size-1)
    for r in rows:
        require(r['start'] % page_size == 0 and r['end'] % page_size == 0
                and r['offset'] % page_size == 0, 'unaligned maps row')
    loads = layout['loads']
    require(loads and module_path and not module_path.endswith(' (deleted)'), 'missing/deleted module')
    for s in loads:
        require(0 <= s['filesz'] <= s['memsz'] and s['memsz'] > 0, 'invalid segment size')
        require(0 <= s['vaddr'] < 1 << 64 and 0 <= s['offset'] < 1 << 64
                and s['vaddr'] + s['memsz'] <= 1 << 64
                and s['offset'] + s['filesz'] <= 1 << 64, 'invalid segment bounds')
        require(s['vaddr'] % page_size == s['offset'] % page_size, 'incongruent PT_LOAD')
    require(not any(r['path'] == module_path + ' (deleted)' for r in rows), 'deleted module mapping')
    named = [r for r in rows if r['path'] == module_path]
    require(named and len({(r['dev'], r['inode']) for r in named}) == 1
            and all(r['inode'] > 0 for r in named), 'missing or mixed module identity')
    biases = set()
    for r in named:
        if 'x' not in r['perms']:
            continue
        for s in loads:
            if s['flags'] & 1 and down(s['offset']) <= r['offset'] < up(s['offset'] + s['filesz']):
                biases.add(r['start'] - (down(s['vaddr']) + r['offset'] - down(s['offset'])))
    require(len(biases) == 1, 'missing or ambiguous executable anchor')
    bias = biases.pop()
    require(bias >= 0 and bias % page_size == 0, 'invalid load bias')
    # The loader may retain inaccessible file-backed reservation rows between
    # segments. Recognize only gaps in the union of rounded memory extents,
    # with the original file mapping's displacement. These rows never count
    # toward scalar file-prefix coverage or scalar reads.
    spans = []
    for lo, hi in sorted((down(s['vaddr']), up(s['vaddr'] + s['memsz'])) for s in loads):
        if spans and lo <= spans[-1][1]:
            spans[-1] = (spans[-1][0], max(spans[-1][1], hi))
        else:
            spans.append((lo, hi))
    gaps = [(left[1], right[0]) for left, right in zip(spans, spans[1:])]
    origin_vaddr, origin_offset = min((down(s['vaddr']), down(s['offset']))
                                     for s in loads if s['filesz'] > 0)
    matching_rows = {i: [] for i in range(len(loads))}
    for r in named:
        matches = [i for i, s in enumerate(loads)
                   if s['filesz'] > 0 and down(s['offset']) <= r['offset'] < up(s['offset'] + s['filesz'])
                   and r['start'] == bias + down(s['vaddr']) + r['offset'] - down(s['offset'])
                   and r['end'] <= bias + up(s['vaddr'] + s['filesz'])
                   and ('w' not in r['perms'] or s['flags'] & 2)
                   and ('x' not in r['perms'] or s['flags'] & 1)]
        reservation = (r['perms'] == '---p'
                       and any(bias + lo <= r['start'] and r['end'] <= bias + hi for lo, hi in gaps)
                       and r['offset'] == origin_offset + r['start'] - (bias + origin_vaddr))
        require(matches or reservation, 'module map does not match PT_LOAD file range or inaccessible reservation')
        for i in matches:
            matching_rows[i].append(r)

    def covers(ranges, start, end):
        cursor = start
        for lo, hi in sorted(ranges):
            if hi <= cursor:
                continue
            if lo > cursor:
                return False
            cursor = hi
            if cursor >= end:
                return True
        return cursor >= end

    reads = {}
    require(layout['symbols'], 'no requested scalar symbols')
    for label, symbol in layout['symbols'].items():
        value, size = symbol['value'], symbol['size']
        require(size in (4, 8) and value % size == 0, 'invalid scalar size/alignment')
        segments = [(i, s) for i, s in enumerate(loads)
                    if s['vaddr'] <= value and value+size <= s['vaddr']+s['memsz']]
        require(len(segments) == 1 and segments[0][1]['flags'] & 2 and not segments[0][1]['flags'] & 1, 'scalar outside unique writable non-executable PT_LOAD')
        index, s = segments[0]
        # A union permits the loader's read-only RELRO / writable data split.
        # A bare anonymous range is not enough to attribute a data segment.
        prefix_complete = s['filesz'] > 0 and covers(
            [(r['start'], r['end']) for r in matching_rows[index]],
            bias + down(s['vaddr']), bias + up(s['vaddr'] + s['filesz']))
        require(prefix_complete, 'scalar PT_LOAD file prefix is incomplete')
        address = bias + value
        containing = [r for r in rows if r['start'] <= address and address+size <= r['end']]
        require(len(containing) == 1 and containing[0]['perms'][0] == 'r', 'scalar not readable in one map')
        r = containing[0]
        file_backed = r in matching_rows[index]
        anonymous_bss = (r['path'] == '' and r['inode'] == 0 and r['dev'] == '00:00'
                         and r['offset'] == 0 and 'w' in r['perms'] and 'x' not in r['perms']
                         and address >= bias + up(s['vaddr']+s['filesz']))
        require(file_backed or anonymous_bss, 'scalar mapping belongs to another object')
        reads[label] = dict(symbol=symbol['name'], address=address, size=size,
                            mapping='file' if file_backed else 'anonymous-bss')
    return dict(load_bias=bias, binary_sha256=layout['sha256'], reads=reads,
                coherent_snapshot=False, native_objects=False)

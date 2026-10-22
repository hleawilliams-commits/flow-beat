#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline, exact-binary plan for eight owned atomic diagnostic scalars.

Consumes supplied ELF/maps/auxv bytes. Does not access a process, open a transport,
follow a pointer, inspect native task objects, or sample Transport storage.
"""
from pathlib import Path, PurePosixPath
import hashlib
import io
import struct
import types

HERE = Path(__file__).resolve().parent
MODULE = HERE.parent
REFERENCE = MODULE.parent / 'route-observer/status-map/layout.py'
REFERENCE_SHA256 = 'bb1182de30ce8d437e7b25d35723f901f1751b31bf59258dce3c35fc3a1e3c7c'
SOURCE_SHA256 = '538574af5f357131e0fa9ec6e26d54cfef0ec2a57f31baddff1e8e5bc7c134ed'
BINARY_SHA256 = '71d7fc088f4def67d1307e4faeef301020aed04bfa3da7ae4bc6a422ece3e255'
MODULE_BASENAME = 'encoder-task-observer.so'
# Link-time ELF values, not observed runtime addresses. unsigned values only.
FIELDS = {
    'state': (0x13070, 4),
    'heartbeat': (0x130a8, 8),
    'attempted': (0x13068, 8),
    'state_gated': (0x13078, 8),
    'caller_gated': (0x13080, 8),
    'tid_failed': (0x13090, 8),
    'worker_cycles': (0x130a0, 8),
    'worker_exit_reason': (0x13098, 4),
}
NAMES = {label: f'_ZN3etoL{len(label)}{label}E' for label in FIELDS}


def require(value, reason):
    if not value:
        raise ValueError(reason)


def _reference():
    raw = REFERENCE.read_bytes()
    require(hashlib.sha256(raw).hexdigest() == REFERENCE_SHA256, 'reference planner hash mismatch')
    module = types.ModuleType('eto_pinned_maps_layout')
    # Only the hash-pinned, offline planner is evaluated. No runtime module imports.
    exec(compile(raw, str(REFERENCE), 'exec'), module.__dict__)
    return module


def source_contract(raw):
    require(hashlib.sha256(raw).hexdigest() == SOURCE_SHA256, 'adapter source hash mismatch')
    source = raw.decode('utf-8')
    require('static std::atomic<unsigned> state{Never};' in source and
            'static std::atomic<unsigned> worker_exit_reason{WorkerRunning};' in source,
            '32-bit atomic source declarations')
    require('static std::atomic<uint64_t> heartbeat{0}, attempted{0}, state_gated{0}, caller_gated{0}, tid_failed{0}, worker_cycles{0};' in source,
            '64-bit atomic source declarations')


def _model_contract(model):
    require(set(model['symbols']) == set(FIELDS), 'scalar allowlist mismatch')
    for label, (value, size) in FIELDS.items():
        symbol = model['symbols'][label]
        require(symbol == dict(name=NAMES[label], value=value, size=size),
                'scalar identity/offset/width mismatch: ' + label)


def _extract(raw):
    """Structural checks also exercised independently of the public hash gate."""
    from elftools.elf.elffile import ELFFile
    elf = ELFFile(io.BytesIO(raw))
    require(elf.elfclass == 64 and elf.little_endian and elf['e_machine'] == 'EM_AARCH64'
            and elf['e_type'] == 'ET_DYN', 'expected AArch64 LE shared object')
    table = elf.get_section_by_name('.symtab')
    require(table is not None, 'full symbol table required')
    symbols = {}
    for label, name in NAMES.items():
        found = table.get_symbol_by_name(name) or []
        require(len(found) == 1, 'missing or ambiguous scalar: ' + label)
        symbol = found[0]
        require(symbol['st_info']['type'] == 'STT_OBJECT'
                and symbol['st_info']['bind'] == 'STB_LOCAL'
                and isinstance(symbol['st_shndx'], int)
                and 0 < symbol['st_shndx'] < elf.num_sections(),
                'not a local defined OBJECT: ' + label)
        section = elf.get_section(symbol['st_shndx'])
        require(section['sh_type'] == 'SHT_NOBITS' and section['sh_flags'] & 3 == 3
                and not section['sh_flags'] & 4
                and section['sh_addr'] <= symbol['st_value']
                and symbol['st_value'] + symbol['st_size'] <= section['sh_addr'] + section['sh_size'],
                'scalar outside writable non-executable BSS: ' + label)
        symbols[label] = dict(name=name, value=symbol['st_value'], size=symbol['st_size'])
    loads = [dict(vaddr=s['p_vaddr'], offset=s['p_offset'], filesz=s['p_filesz'],
                  memsz=s['p_memsz'], flags=s['p_flags']) for s in elf.iter_segments()
             if s['p_type'] == 'PT_LOAD']
    require(loads and all(s['offset'] + s['filesz'] <= len(raw) for s in loads),
            'missing or truncated PT_LOAD file range')
    model = dict(sha256=hashlib.sha256(raw).hexdigest(), loads=loads, symbols=symbols)
    _model_contract(model)
    return model


def binary_layout(binary_path):
    source_contract((MODULE / 'observer.cpp').read_bytes())
    raw = Path(binary_path).read_bytes()
    require(hashlib.sha256(raw).hexdigest() == BINARY_SHA256, 'observer binary hash mismatch')
    return _extract(raw)


def page_size(auxv):
    require(isinstance(auxv, bytes) and len(auxv) > 0 and len(auxv) % 16 == 0,
            'invalid AArch64 auxv width')
    values = []
    terminated = False
    for off in range(0, len(auxv), 16):
        kind, value = struct.unpack_from('<QQ', auxv, off)
        if terminated:
            require(kind == 0 and value == 0, 'nonzero auxv after terminator')
        elif kind == 0:
            require(value == 0, 'invalid auxv terminator')
            terminated = True
        elif kind == 6:
            values.append(value)
    require(terminated and len(values) == 1, 'missing or duplicate AT_PAGESZ')
    require(values[0] in (4096, 16384, 65536), 'unsupported AArch64 page size')
    return values[0]


def _plan_model(model, maps_text, module_path, pages):
    _model_contract(model)
    path = PurePosixPath(module_path)
    require(isinstance(module_path, str) and module_path.startswith('/')
            and str(path) == module_path and '..' not in path.parts
            and path.name == MODULE_BASENAME, 'unexpected module path')
    planner = _reference()
    rows = planner.parse_maps(maps_text)
    require(all(r['perms'].endswith('p') for r in rows if r['path'] == module_path),
            'module mapping is not private')
    plan = planner.plan_reads(model, rows, module_path, pages)
    for label, read in plan['reads'].items():
        row = next(r for r in rows if r['start'] <= read['address'] and
                   read['address'] + read['size'] <= r['end'])
        require(row['perms'] == 'rw-p', 'scalar mapping is not private read/write: ' + label)
    return dict(**plan, page_size=pages, module_path=module_path,
                owned_scalar_bytes=sum(size for _, size in FIELDS.values()),
                transport_storage=False, record_slots=False, follows_pointers=False)


def plan_reads(binary_path, maps_text, module_path, auxv):
    """Caller supplies snapshots and owns PID/file/map freshness checks."""
    return _plan_model(binary_layout(binary_path), maps_text, module_path, page_size(auxv))


def decode_samples(samples):
    """Validate eight supplied buffers; does not read the planned addresses."""
    require(set(samples) == set(FIELDS), 'sample allowlist mismatch')
    values = {}
    for label, (_, size) in FIELDS.items():
        data = samples[label]
        require(isinstance(data, bytes) and len(data) == size, 'scalar sample width mismatch: ' + label)
        values[label] = int.from_bytes(data, 'little', signed=False)
    return values

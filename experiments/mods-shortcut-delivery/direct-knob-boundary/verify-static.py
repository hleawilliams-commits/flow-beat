#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify a possible native input boundary in the owner's exact firmware.

Read-only. Emits addresses/hashes and explicit limits, never firmware bytes.
Requires pyelftools and capstone. Does not install or call native functions.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from elftools.dwarf.callframe import FDE
from elftools.elf.elffile import ELFFile

PIN = "3e0c5cb010454228f4ee63226b60d060494eeaae"
FIRMWARE = "6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925"
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--elf", type=Path, required=True)
ap.add_argument("--upstream", type=Path, required=True)
args = ap.parse_args()
raw = args.elf.read_bytes()
assert hashlib.sha256(raw).hexdigest() == FIRMWARE, "Different firmware; no fallback"
elf = ELFFile(io.BytesIO(raw))
assert elf.elfclass == 64 and elf.little_endian and elf['e_machine'] == 'EM_AARCH64'
header = subprocess.check_output(
    ["git", "show", PIN + ":src/azmod/ep147addr.h"], cwd=args.upstream).decode()
names = re.findall(r"\bEPI_([a-f0-9]+),", header.split("EPI_N")[0])
values = [int(v, 16) for v in re.findall(
    r"^\s*(0x[a-f0-9]+),", header.split("ep147_addr_v200")[1].split("};")[0], re.M)]
assert len(names) == len(values)
mapped = dict(zip(names, values))
assert mapped['19ac568'] == 0x1b10860 and mapped['29d67a0'] == 0x2d4ea68
assert mapped['19d6760'] == 0x1b3aaa0
ro = elf.get_section_by_name('.rodata')
data, origin = ro.data(), ro['sh_addr']


def word(address):
    assert origin <= address <= origin + len(data) - 8
    return struct.unpack_from('<Q', data, address - origin)[0]


def refs(address):
    return [origin + i for i in range(0, len(data) - 7, 8)
            if struct.unpack_from('<Q', data, i)[0] == address]


name = b'N3gui8waveform12WaveformViewE\0'
assert data.count(name) == 1
name_at = origin + data.index(name)
typeinfos = [v - 8 for v in refs(name_at)]
assert typeinfos == [0x2d616d0]
primary = [v + 8 for v in refs(typeinfos[0]) if word(v - 8) == 0]
assert primary == [0x2d61ae0]
vt = primary[0]
assert word(vt + 0xd0) == mapped['19d6760']
assert word(vt + 0x198) == 0x1b3ab60
assert word(mapped['29d67a0'] + 0x198) == mapped['19ac568']

expected = {0x1b3ab60: 0x84, 0x1b10860: 0x140, 0x1adea38: 0x4a8, 0x1adeee0: 0x588}
found = {e['initial_location']: e['address_range']
         for e in elf.get_dwarf_info().EH_CFI_entries()
         if isinstance(e, FDE) and e['initial_location'] in expected}
assert found == expected
text = elf.get_section_by_name('.text')
code, code_origin = text.data(), text['sh_addr']
decoder = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)


def instructions(start, length):
    return {i.address: (i.mnemonic, i.op_str) for i in decoder.disasm(
        code[start-code_origin:start-code_origin+length], start)}


body = instructions(0x1b3ab60, 0x84)
# A three-argument Browse wrapper would lose the Waveform handler's live w3.
assert body[0x1b3ab84] == ('mov', 'x20, x1')
assert body[0x1b3ab90] == ('mov', 'w22, w3')
assert body[0x1b3ab94] == ('mov', 'w21, w2')
assert body[0x1b3aba8] == ('mov', 'w3, w22')
assert body[0x1b3abbc] == ('ldp', 'x19, x20, [sp, #0x10]')
assert body[0x1b3abcc] == ('br', 'x4')
for start, site in [(0x1adea38, 0x1adeaa4), (0x1adeee0, 0x1adef4c)]:
    ins = instructions(start, expected[start])
    assert ins[site][0] == 'ldr' and '#0x198]' in ins[site][1]
    assert ins[site + 8] == ('blr', 'x4')

result = {
    'upstream_commit': PIN,
    'firmware_sha256': FIRMWARE,
    'address_table_sha256': hashlib.sha256(header.encode()).hexdigest(),
    'source_symbol_mapping': {n: hex(mapped[n]) for n in ['19ac568', '29d67a0', '19d6760']},
    'waveform_typeinfo': hex(typeinfos[0]),
    'waveform_primary_vtable': hex(vt),
    'shared_slot_offset': '0x198',
    'waveform_candidate': '0x1b3ab60',
    'candidate_body_sha256': hashlib.sha256(code[0x1b3ab60-code_origin:0x1b3abe4-code_origin]).hexdigest(),
    'fde_extents': {hex(k): hex(v) for k, v in found.items()},
    'candidate_uses_four_integer_arguments': True,
    'dispatcher_virtual_call_sites': ['0x1adeaac', '0x1adef54'],
    'runtime_validation': False,
    'new_hook_or_core_patch': False,
    'limits': [
        'Matching interface slot is not proof of physical knob provenance',
        'Native counter update and queued-event ownership remain unproven',
        'Watchdog release, view cancellation, modal precedence and return semantics remain open',
        'Do not install or replace the existing Browse knob hook from these anchors'
    ]
}
print(json.dumps(result, indent=2))

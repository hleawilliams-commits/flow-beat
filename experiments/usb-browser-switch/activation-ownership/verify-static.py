#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check native value-routing and lifetime limits in the private, exact ELF.

This reads a file and executes only extracted C instruction-policy predicates
with host integer fixtures. It makes no native call, hook, or runtime read.
Requires capstone and pyelftools. The output contains no firmware dump.
"""
import argparse
import hashlib
import io
import json
import struct
import subprocess
import tempfile
from pathlib import Path

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from elftools.dwarf.callframe import FDE
from elftools.elf.elffile import ELFFile

PIN = '3e0c5cb010454228f4ee63226b60d060494eeaae'
FW = '6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--elf', type=Path, required=True)
parser.add_argument('--upstream', type=Path, required=True)
parser.add_argument('--output', type=Path)
parser.add_argument('--cc', default='cc')
args = parser.parse_args()
raw = args.elf.read_bytes()
digest = lambda value: hashlib.sha256(value).hexdigest()
assert digest(raw) == FW
elf = ELFFile(io.BytesIO(raw))
assert elf.elfclass == 64 and elf.little_endian
assert elf['e_machine'] == 'EM_AARCH64' and elf['e_type'] == 'ET_EXEC'
segments = [dict(s.header) for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
expected_extents = {0x1be7e38: 0xbe4, 0x20a1550: 0x1024, 0x1b2e538: 4}
found_extents = {
    entry['initial_location']: entry['address_range']
    for entry in elf.get_dwarf_info().EH_CFI_entries()
    if isinstance(entry, FDE) and entry['initial_location'] in expected_extents
}
assert found_extents == expected_extents


def read(address, size):
    segment = next(s for s in segments if s['p_vaddr'] <= address and
                   address + size <= s['p_vaddr'] + s['p_filesz'])
    offset = segment['p_offset'] + address - segment['p_vaddr']
    return raw[offset:offset + size]


def q(address):
    return struct.unpack('<Q', read(address, 8))[0]


md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
# Minimal anchors, not a disassembly export. The whole image hash above binds
# the intervening instructions as well; conclusions remain explicitly scoped.
anchors = {
    # DeviceLibrary row -> packed MediaID -> final guarded action.
    0x1be8a70: ('ldr', 'x1, [x19, #0x88]'),
    0x1be8a74: ('sbfiz', 'x0, x0, #5, #0x20'),
    0x1be8a80: ('ldr', 'w1, [x1, #0x14]'),
    0x1be8a88: ('bl', '#0x1be5798'),
    0x1be8a8c: ('str', 'x0, [sp, #0x38]'),
    0x1be8a94: ('add', 'x2, sp, #0x38'),
    0x1be8a9c: ('bl', '#0x1be7e38'),
    0x1be8be8: ('bl', '#0x1be5798'),
    0x1be8bf4: ('add', 'x2, sp, #0x70'),
    0x1be8bfc: ('bl', '#0x1be7e38'),
    0x1be57b4: ('add', 'x1, x20, #8'),
    0x1be57c0: ('ldr', 'x2, [x2, #0x50]'),
    0x1be5800: ('ldr', 'w0, [x20, #0x14]'),
    0x1be585c: ('ldr', 'x1, [x1, #0x18]'),
    0x1be5954: ('ldr', 'x1, [x1, #0x10]'),
    0x1be591c: ('mov', 'w1, #0'),
    0x1be58f8: ('mov', 'w1, #1'),
    0x1be58d4: ('mov', 'w1, #2'),
    0x1be582c: ('mov', 'w1, #3'),
    0x1be5a0c: ('ldr', 'w2, [x24]'),
    0x1be5a14: ('cmp', 'w2, w0'),
    0x1be5a20: ('ubfx', 'x2, x0, #0x20, #8'),
    0x1be5a30: ('ubfx', 'x0, x0, #0x28, #8'),
    0x1be9094: ('str', 'w0, [x23, #0x14]'),
    0x1be908c: ('bl', '#0x1be31a8'),
    0x1be31a8: ('ldrb', 'w1, [x0, #4]'),
    0x1be31ac: ('and', 'w1, w1, #0x1f'),
    # Guarded action receives a pointer to the caller's scalar, not a row.
    0x1be7e50: ('mov', 'w21, w1'),
    0x1be7e58: ('mov', 'x24, x2'),
    0x1be7ee4: ('mov', 'x1, x24'),
    0x1be7ee8: ('ldr', 'x2, [x2, #0xd0]'),
    0x1be7ef0: ('ands', 'w19, w0, #0xff'),
    0x1be7ef4: ('b.ne', '#0x1be7f7c'),
    0x1be81b0: ('ldr', 'x19, [x20, #0x48]'),
    0x1be81bc: ('add', 'x0, sp, #0x80'),
    0x1be81c0: ('ldr', 'x2, [x24]'),
    0x1be81c4: ('mov', 'w1, #1'),
    0x1be81c8: ('ldr', 'x20, [x3, #0x10]'),
    0x1be81cc: ('bl', '#0x1b00160'),
    0x1be81d0: ('ldr', 'w2, [sp, #0x88]'),
    0x1be81d4: ('mov', 'x0, x19'),
    0x1be81d8: ('ldr', 'x1, [sp, #0x80]'),
    0x1be81dc: ('blr', 'x20'),
    # Source is a 12-byte value: discriminator then an eight-byte carrier.
    0x1b00160: ('str', 'w1, [x0]'),
    0x1b00164: ('cmp', 'w1, #1'),
    0x1b00168: ('stur', 'x2, [x0, #4]'),
    0x1b00198: ('and', 'w2, w1, #0xff'),
    0x1b0019c: ('cbz', 'w2, #0x1b001ac'),
    0x1b001a0: ('ldrb', 'w2, [x0, #8]'),
    0x1b001a4: ('tst', 'x2, #0x1f'),
    0x1b001ac: ('str', 'wzr, [x0]'),
    # Concrete setSource implementation: register value, direct or queued.
    0x20a1564: ('bfxil', 'w3, w2, #0, #0x20'),
    0x20a1578: ('mov', 'w24, w1'),
    0x20a157c: ('lsr', 'x23, x1, #0x20'),
    0x20a1598: ('bl', '#0x249b0f0'),
    0x20a15a0: ('bl', '#0x2503090'),
    0x20a15b0: ('b.eq', '#0x20a1760'),
    0x20a1660: ('str', 'x19, [sp, #0xc0]'),
    0x20a1664: ('stp', 'w24, w23, [sp, #0xc8]'),
    0x20a166c: ('ldp', 'x2, x3, [sp, #0xc0]'),
    0x20a1670: ('stp', 'x2, x3, [x26, #0x18]'),
    0x20a1674: ('str', 'x0, [x1, #0x10]'),
    0x20a16c4: ('bl', '#0x237a440'),
    0x20a176c: ('stp', 'w24, w23, [x19, #0x24]'),
    0x20a1774: ('strb', 'w20, [x19, #0x2c]'),
    0x20a177c: ('strb', 'w22, [x19, #0x2d]'),
    0x20a1878: ('blr', 'x3'),
    0x20a1884: ('ldr', 'w0, [x19, #0x68]'),
    0x20a18f4: ('blr', 'x3'),
    0x20a1900: ('ldr', 'w0, [x19, #0x68]'),
    0x20a258c: ('ldr', 'x20, [x0, #0x18]'),
    0x20a2598: ('ldr', 'w3, [x0, #0x20]'),
    0x20a259c: ('ldr', 'w0, [x20, #0x28]'),
    # Timer task retains only a raw timer pointer in this capture.
    0x1ae8de8: ('str', 'x24, [x21, #0x10]'),
    0x1ae8dec: ('str', 'w22, [x21, #0x18]'),
    0x1ae8df0: ('str', 'x19, [x21, #0x20]'),
    0x1ae8e44: ('bl', '#0x237a440'),
    0x1ae8b0c: ('ldr', 'x21, [x20, #0x20]'),
    0x1ae8b48: ('blr', 'x1'),
    0x1ae8b54: ('ldr', 'w0, [x21, #0x38]'),
    0x1ae90a8: ('blr', 'x1'),
    0x1ae90b4: ('ldr', 'w0, [x19, #0x38]'),
    0x1ae86e8: ('add', 'x19, x0, #0x700'),
    0x1ae86e0: ('adrp', 'x0, #0x3fe9000'),
    0x1ae86f8: ('add', 'x0, x19, #8'),
    0x1ae8700: ('sub', 'w1, w1, #1'),
    0x1ae8710: ('str', 'wzr, [x20, #8]'),
}
for at, expected in anchors.items():
    ins = next(md.disasm(read(at, 4), at))
    assert (ins.mnemonic, ins.op_str) == expected, hex(at)

# Type names and slot relation identify the concrete interface, without assuming
# that a cached runtime interface will always resolve to this implementation.
assert read(q(0x317ddc8 + 8), 100).split(b'\0')[0] == b'N3gui13source_select18BrowseSourceSelectE'
assert q(0x317dde0) == 0x317dd88 and q(0x317dde8) == 2
assert read(q(0x317dd88 + 8), 100).split(b'\0')[0] == b'N3gui13source_select19IBrowseSourceSelectE'
assert q(0x317de58) == 0x317ddc8
assert q(0x317de60 + 0x10) == 0x20a1550
assert read(0x317df30, 120).split(b'\0')[0] == b'*ZN3gui13source_select18BrowseSourceSelect9setSourceENS0_6SourceEEUlvE_'
assert q(0x317e100) == 0x20a2578
assert q(0x2d41370) == 0x1ae8a40
assert q(0x2d41360) == 0x1ae86c8
assert q(0x2d5b030) == 0x1b2e538

source = subprocess.check_output(['git', 'show', PIN + ':src/azmod/inline_hooks.c'], cwd=args.upstream).decode()
vslot_source = subprocess.check_output(['git', 'show', PIN + ':src/azmod/sdk_vslot.c'], cwd=args.upstream).decode()


def extract(name):
    start = source.index('static int ' + name + '(')
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


policy = '\n\n'.join(extract(name) for name in ['azm_pcrel_kind', 'azm_ends_fn', 'azm_patch_ok'])
assert 'for (int i = 0; i < 4; i++)' in extract('azm_patch_ok')
assert 'if (!azm_patch_ok(func, 1))' in extract('azm_install_hook_at')
entries = [0x1be7e38, 0x20a1550]
fixture_arrays = ',\n'.join('{' + ','.join(hex(w) + 'u' for w in struct.unpack('<4I', read(at, 16))) + '}' for at in entries)
fixture = '''
#include <stdint.h>
#include <stdio.h>
''' + policy + '''
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) return checks; } while (0)
int main(void) {
    const uint32_t entries[2][4] = {''' + fixture_arrays + '''};
    for (int n = 0; n < 2; ++n) {
        for (int i = 0; i < 4; ++i) CHECK(azm_pcrel_kind(entries[n][i]) == 0);
        CHECK(azm_patch_ok(entries[n], 1) == 1);
        CHECK(azm_patch_ok(entries[n], 4) == 1);
    }
    for (int pos = 0; pos < 4; ++pos) {
        uint32_t control[4] = {entries[0][0], entries[0][1], entries[0][2], entries[0][3]};
        control[pos] = 0x36080064u; /* TBZ fixture, never written to ELF */
        CHECK(azm_patch_ok(control, 1) == 0);
        CHECK(azm_patch_ok(control, 4) == 0);
    }
    printf("%d\\n", checks);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='az-usb-owned-policy-') as tmp:
    c = Path(tmp) / 'policy.c'
    exe = Path(tmp) / 'policy'
    c.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    count = int(subprocess.check_output([str(exe)], text=True))
assert count == 20

# A data-section label does not override the actual ELF segment flags. This
# checks the same non-executable range rule already executed by the knob suite.
assert 'if (ph->p_type != PT_LOAD || (ph->p_flags & PF_X))\n            continue;' in vslot_source
timer_segment = next(s for s in segments if s['p_vaddr'] <= 0x2d5b030 and 0x2d5b038 <= s['p_vaddr'] + s['p_memsz'])
assert timer_segment['p_flags'] == 5
assert not any(s['p_vaddr'] <= 0x2d5b030 and 0x2d5b038 <= s['p_vaddr'] + s['p_memsz'] for s in segments if not s['p_flags'] & 1)

result = {
    'firmware_sha256': FW, 'firmware_tag': 'ep147-9e9454e357389ece',
    'elf': {'machine': 'EM_AARCH64', 'class': 64, 'little_endian': True, 'type': 'ET_EXEC'},
    'verified_fde_extents': {hex(at): hex(size) for at, size in found_extents.items()},
    'upstream_commit': PIN, 'verified_instruction_anchors': len(anchors),
    'verifier_sha256': digest(Path(__file__).read_bytes()),
    'inline_policy_source_sha256': digest(source.encode()),
    'inline_policy_extraction_sha256': digest(policy.encode()),
    'sdk_vslot_source_sha256': digest(vslot_source.encode()),
    'instruction_policy_host_assertions': count,
    'native_value_route': {
        'row_stride_bytes': 32, 'row_kind_offset': '0x14',
        'resolver': '0x1be5798', 'guarded_action': '0x1be7e38',
        'action_argument_2': 'pointer to caller-stack 8-byte MediaID carrier',
        'source_constructor': '0x1b00160', 'source_value_size': 12,
        'source_discriminator_requested': 1,
        'source_constructor_can_normalize_discriminator_to_zero': True,
        'concrete_interface': 'gui::source_select::IBrowseSourceSelect',
        'concrete_implementation': 'gui::source_select::BrowseSourceSelect::setSource(Source)',
        'concrete_entry': '0x20a1550', 'vtable': '0x317de60', 'slot_offset': '0x10',
        'abi_evidence': 'this in x0; Source bytes 0..7 in x1 and bytes 8..11 in w2',
        'meaningful_media_fields': '32-bit word and two bytes; carrier padding is not identity',
        'runtime_receiver_vtable_observed': False, 'full_wrapper_abi_audited': False,
        'final_media_event_correlation_proven': False,
    },
    'lifetime': {
        'set_source_queued_task': 'value copy plus raw service pointer at +0x18',
        'queued_execute_entry': '0x20a2578',
        'set_source_reads_service_after_listener_return': True,
        'timer_queued_task': 'timer id at +0x18; raw timer pointer at +0x20',
        'timer_queued_execute': '0x1ae8a40',
        'timer_reads_timer_after_callback_on_both_routes': True,
        'timer_task_destructor_decrements_global_task_counter': True,
        'view_or_service_lifetime_pin_proven': False,
    },
    'candidate_boundaries': [
        {'entry': hex(at), 'prologue_sha256': digest(read(at, 16)),
         'instruction_policy_accepts_one_and_four_words': True,
         'runtime_claim_or_observation': False,
         'ownership_island_mapping_and_full_abi_still_required': True}
        for at in entries
    ],
    'timer_listener': {
        'slot': '0x2d5b030', 'segment_flags': 5,
        'sdk_vslot_eligible': False,
        'refusal_reason': 'slot lies in executable PT_LOAD',
        'ret_only_original_not_a_four_instruction_function': True,
    },
    'limits': [
        'No native function called, no runtime read, no device access, no hook installed.',
        'Instruction-policy eligibility is not runtime claim success or exclusive ownership.',
        'Row-to-USB uniqueness, missing-media handling and live receiver identity remain unobserved here.',
        'Source dispatch is not asynchronous browser completion; cancellation remains unproven.',
        'No timer retention pin, native activation adapter, USB button or release is supplied.',
    ],
}
encoded = json.dumps(result, indent=2) + '\n'
print(encoded, end='')
if args.output:
    args.output.write_text(encoded)

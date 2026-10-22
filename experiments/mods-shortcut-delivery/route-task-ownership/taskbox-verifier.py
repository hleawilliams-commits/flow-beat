#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline exact-ELF TimerTaskBox/AsyncTaskBox ownership anchors.

No firmware execution, process reads, device/network contact or hook installation.
This checks static instruction/data relationships, not a runtime lifetime proof.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from elftools.elf.elffile import ELFFile
from elftools.dwarf.callframe import FDE

FIRMWARE = '6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
FUNCTIONS = {
    0x1b00d20: 0xec, 0x1b00e10: 0x94, 0x1b01cf8: 0x2e8,
    0x237a440: 0x38, 0x237a140: 0xa0, 0x237fa50: 0x368,
    0x237eac0: 0x1e0, 0x2379cc0: 0x148, 0x237f770: 0x18c,
    0x237f900: 0x14c, 0x237e9f0: 0xcc, 0x2376130: 0x288,
    0x237fdc0: 0x24, 0x237d4f0: 0x75c, 0x2380a20: 0x44,
    0x237a610: 0x1b0, 0x1e61cd8: 0x34, 0x99ca38: 0x2c,
    0x1ade328: 0xc0, 0x2503090: 0xc, 0x249b0f0: 0x4,
}
# Each contiguous block is an explicit expected disassembly, not generated from
# the input. Names are used by discriminating rejection controls below.
BLOCKS = {
 'entry_calls_post_slot': (0x1b01d20, '''bl #0x1b00e10
mov x20, x0
ldr x0, [x0]
ldr x23, [x0, #0x30]'''),
 'entry_calls_post': (0x1b01df8, '''mov x1, x19
mov x0, x20
blr x23
cbz x0, #0x1b01e58
ldr x0, [x20, #0x18]
cbz x0, #0x1b01e58'''),
 'entry_optional_notification': (0x1b01e3c, '''ldr x1, [x20, #0x18]
mov x0, x1
ldr x1, [x1]
ldr x1, [x1, #0x10]
blr x1'''),
 'singleton_return': (0x1b00e2c, '''adrp x20, #0x3fe8000
add x20, x20, #0xf88
mov x0, x20'''),
 'singleton_ctor': (0x1b00e58, '''mov x0, x20
bl #0x1b00d20'''),
 'pool_control_initialization': (0x1b00d58, '''movi v0.2s, #1
mov x21, x0
adrp x0, #0x2900000
add x0, x0, #0xd38
str d0, [x20, #8]
str x0, [x21], #0x10'''),
 'timer_vptr_and_pool': (0x1b00d88, '''adrp x1, #0x321a000
add x1, x1, #0xf58
adrp x0, #0x321b000
add x0, x0, #0x108
stp x0, x21, [x19]
mov w0, #1
stp x20, xzr, [x19, #0x10]
str xzr, [x19, #0x20]
str x1, [x20, #0x10]
str w2, [x20, #0x18]
bl #0x237a140'''),
 'timer_post_forward': (0x237a450, '''mov w0, #0
str x1, [sp, #0x28]
bl #0x237a140
ldr x1, [sp, #0x28]
mov x2, x1
add x1, x0, #0x20
mov x0, x19
ldr x19, [sp, #0x10]
ldp x29, x30, [sp], #0x30
b #0x237eac0'''),
 'impl_construct': (0x237a17c, '''mov x0, #0xf8
bl #0x426570
mov w1, w20
str x21, [sp, #0x20]
mov x21, x0
bl #0x237fa50
add x0, x19, #0xe0
str x21, [x19, #0xe8]'''),
 'impl_async_vptr': (0x237fb24, '''str xzr, [x21, #0x18]
adrp x0, #0x321a000
add x0, x0, #0xfd8
str x0, [x19, #0x20]
stp xzr, xzr, [x19, #0x40]'''),
 'base_submission': (0x237eb14, '''cbz x20, #0x237eb88
ldr x2, [x19]
mov x0, x19
mov x1, x20
ldr x2, [x2, #0x30]
blr x2
mov x19, x0
cbz x0, #0x237eb50'''),
 'base_zero_cleanup': (0x237eb50, '''mov x1, x20
mov x0, x21
bl #0x237e9f0'''),
 'async_submission': (0x2379cd0, '''cbz x1, #0x2379da0
mov x20, x0
add x0, x0, #0x38
ldr x19, [x1, #0x10]
add x1, sp, #0x28
bl #0x237f770
tst w0, #0xff
b.eq #0x2379d00
mov x0, x19'''),
 'async_failure_cleanup': (0x2379d94, '''ldr x1, [sp, #0x28]
mov x0, x20
bl #0x237e9f0
mov x19, #0
mov x0, x19'''),
 'queue_pointer_copy': (0x237f7e4, '''mov x0, x2
ldr x1, [x1]
str x1, [x0, #8]'''),
 'queue_link_publication': (0x237f858, '''ldaxr x3, [x0]
cmp x3, x5
b.ne #0x237f86c
stlxr w6, x4, [x0]
cmp w6, #0
cset w0, eq
cbnz w0, #0x237f878'''),
 'queue_payload_remove': (0x237f958, '''ldr x3, [sp, #8]
ldr x2, [x2, #8]
str x2, [x1]'''),
 'timer_cancel_forward': (0x237fdc4, '''mov w0, #0
mov x29, sp
str x1, [sp, #0x18]
bl #0x237a140
ldr x1, [sp, #0x18]
add x0, x0, #0x20
ldp x29, x30, [sp], #0x20
b #0x2376130'''),
 'cancel_auxiliary': (0x237613c, '''ldr x19, [x0, #0xc8]
cbz x19, #0x2376238'''),
 'cancel_token_copy': (0x23761a8, '''mov x0, x2
str x1, [x0, #8]'''),
 'timer_drain': (0x2380a2c, '''mov x19, x0
add x0, x0, #0x20
bl #0x237d4f0'''),
 'drain_queue_remove': (0x237d670, '''add x1, sp, #0xa0
mov x0, x27
bl #0x237f900
tst w0, #0xff
b.ne #0x237d584'''),
 'drain_cancel_taskno': (0x237d6c8, '''ldr x3, [x19, #0x10]'''),
 'drain_cancel_match': (0x237d780, '''ldr x1, [x0]
cmp x3, x1
b.eq #0x237da84
add x0, x0, #8
cmp x2, x0
b.ne #0x237d780'''),
 'drain_execute': (0x237d7e4, '''ldr x1, [sp, #0xa0]
mov x0, x1
ldr x2, [x1]
ldr x1, [x1, #0x10]
str x1, [x28, #0xc0]
ldr x1, [x2, #0x10]
blr x1'''),
 'drain_free': (0x237d810, '''ldr w2, [sp, #0x74]
mov x0, x28
ldr x1, [sp, #0xa0]
add w2, w2, #1
str w2, [sp, #0x74]
bl #0x237e9f0'''),
 'drain_cancel_bypass': (0x237da84, '''ldr d8, [sp, #0x60]
b #0x237d810'''),
 'free_task_destructor': (0x237ea34, '''ldr x1, [x19]
str xzr, [sp, #0x50]
mov x0, x19
str d0, [sp, #0x38]
ldr x1, [x1]
blr x1
ldr x0, [x20, #8]'''),
 'free_pool_dispatch': (0x237ea58, '''ldr x2, [x0]
ldr x2, [x2, #0x18]
cmp x2, x1
b.ne #0x237ea84
mov x0, x19
bl #0x2375810'''),
 'free_pool_virtual': (0x237ea84, '''mov x1, x19
blr x2'''),
 'shutdown_free_without_execute': (0x237a6fc, '''str xzr, [sp, #0x38]
b #0x237a710
ldr x1, [sp, #0x38]
mov x0, x19
bl #0x237e9f0
add x1, sp, #0x38
mov x0, x20
bl #0x237f900
tst w0, #0xff
b.ne #0x237a704'''),
 'timer_owner_predicate': (0x1e61ce4, '''bl #0x249b0f0
mov x19, x0
bl #0x2503090
cbz x0, #0x1e61cf8
ldr x0, [x0, #0x10]
cmp x0, x19
cset w0, ne'''),
 'owner_getter': (0x2503090, '''adrp x0, #0x4044000
ldr x0, [x0, #0x3a8]
ret'''),
 'pthread_wrapper': (0x249b0f0, '''b #0x425670'''),
 'async_owner_predicate': (0x99ca48, '''bl #0x249b0f0
ldr x1, [x19, #0x30]
ldr x19, [sp, #0x10]
cmp x1, x0
cset w0, ne'''),
 'timer_pool_release': (0x1ade33c, '''ldr x19, [x0, #0x10]
str x1, [x0]
cbz x19, #0x1ade370'''),
 'pool_strong_decrement': (0x1ade354, '''add x1, x19, #8
ldaxr w0, [x1]
sub w2, w0, #1
stlxr w3, w2, [x1]
cbnz w3, #0x1ade358
cmp w0, #1
b.eq #0x1ade390'''),
 'pool_dispose': (0x1ade390, '''ldr x1, [x19]
mov x0, x19
ldr x1, [x1, #0x10]
blr x1'''),
}
WORDS = {
    0x321b100: 0x321add8, 0x321b130: 0x1e61cd8,
    0x321b138: 0x237a440, 0x321b118: 0x237fdc0,
    0x321afd0: 0x321abc0, 0x321b008: 0x2379cc0,
    0x321afe8: 0x2376130, 0x321b000: 0x99ca38,
    0x321b0d0: 0x2380a20, 0x2900d30: 0x2900848,
    0x321af68: 0x2375570, 0x321af70: 0x2375300,
}
NAMES = {
    0x321add8: 'N4meow12TimerTaskBoxE',
    0x321abc0: 'N4meow12AsyncTaskBoxE',
    0x321ae10: 'N4meow12TimerTaskBox4ImplE',
    0x2900848: ('St23_Sp_counted_ptr_inplaceIN4meow16AsyncTaskBoxBase14DefaultMemPoolE'
                'SaIS2_ELN9__gnu_cxx12_Lock_policyE2EE'),
}
STRINGS = {
    0x321be28: 'virtual meow::TaskNo meow::AsyncTaskBox::postTaskInternal(meow::AsyncTaskBoxBase::IAsyncTask*)',
    0x321b970: 'meow::TaskNo meow::AsyncTaskBoxBase::postTaskInternal(meow::AsyncTaskBoxBase&, meow::AsyncTaskBoxBase::IAsyncTask*)',
    0x321b848: 'virtual void meow::AsyncTaskBox::cancelTask(meow::TaskNo::Type)',
    0x321ca10: 'int meow::AsyncTaskBox::executeTasks()',
    0x3219e38: 'AsyncTaskBoxBase::freeTask',
    0x290cc80: 'AsyncTaskBoxBase::doAsync - onTaskPosted',
}
CONTROLS = {
    'predicate_in_place_of_post': ('entry_calls_post_slot', 0x1b01d2c, ('ldr', 'x23, [x0, #0x28]')),
    'wrong_impl_subobject': ('timer_post_forward', 0x237a464, ('add', 'x1, x0, #0x18')),
    'truncated_taskno': ('async_submission', 0x2379cdc, ('ldr', 'w19, [x1, #0x10]')),
    'execute_in_place_of_enqueue': ('async_submission', 0x2379ce4, ('bl', '#0x1b029c0')),
    'execute_on_queue_failure': ('async_submission', 0x2379cec, ('b.ne', '#0x2379d00')),
    'cancel_wrong_task_field': ('drain_cancel_taskno', 0x237d6c8, ('ldr', 'x3, [x19, #8]')),
    'cancel_match_executes': ('drain_cancel_bypass', 0x237da88, ('b', '#0x237d7e4')),
    'wrong_task_virtual_slot': ('drain_execute', 0x237d7f8, ('ldr', 'x1, [x2, #0x18]')),
    'shutdown_executes': ('shutdown_free_without_execute', 0x237a70c, ('bl', '#0x1b029c0')),
    'pool_count_as_dispatcher': ('timer_pool_release', 0x1ade33c, ('ldr', 'x19, [x0, #0x18]')),
    'reversed_owner_predicate': ('timer_owner_predicate', 0x1e61cfc, ('cset', 'w0, eq')),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf', type=Path, required=True)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    raw = args.elf.read_bytes()
    sha = lambda b: hashlib.sha256(b).hexdigest()
    assert sha(raw) == FIRMWARE, 'Different firmware; no fallback'
    elf = ELFFile(io.BytesIO(raw))
    assert elf.elfclass == 64 and elf.little_endian
    assert elf['e_machine'] == 'EM_AARCH64' and elf['e_type'] == 'ET_EXEC'
    segments = list(elf.iter_segments())
    def read(a, n):
        for s in segments:
            if (s['p_type'] == 'PT_LOAD' and s['p_vaddr'] <= a
                    and a + n <= s['p_vaddr'] + s['p_filesz']):
                off = s['p_offset'] + a - s['p_vaddr']
                return raw[off:off + n]
        raise AssertionError('Address is not file-backed')
    def word(a): return struct.unpack('<Q', read(a, 8))[0]
    def string(a): return read(a, 512).split(b'\0', 1)[0].decode()
    fdes = {x['initial_location']: x['address_range']
            for x in elf.get_dwarf_info().EH_CFI_entries() if isinstance(x, FDE)}
    md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    ins = {}
    for a, n in FUNCTIONS.items():
        assert fdes[a] == n, 'FDE extent ' + hex(a)
        assert any(s['p_type'] == 'PT_LOAD' and s['p_flags'] & 1
                   and s['p_vaddr'] <= a and a+n <= s['p_vaddr']+s['p_filesz']
                   for s in segments)
        code = list(md.disasm(read(a, n), a))
        assert sum(i.size for i in code) == n
        ins.update({i.address: (i.mnemonic, i.op_str) for i in code})
    def expected_lines(text):
        return [(line.split(' ', 1)[0], line.split(' ', 1)[1] if ' ' in line else '')
                for line in text.splitlines()]
    def check_block(name, actual):
        start, text = BLOCKS[name]
        for k, expected in enumerate(expected_lines(text)):
            assert actual[start+4*k] == expected, name + ' ' + hex(start+4*k)
    for name in BLOCKS: check_block(name, ins)
    for a, value in WORDS.items(): assert word(a) == value, 'data ' + hex(a)
    for a, value in NAMES.items(): assert string(word(a+8)) == value, 'RTTI ' + hex(a)
    for a, value in STRINGS.items(): assert string(a) == value, 'diagnostic ' + hex(a)
    # Bind pthread_self by the real PLT relocation, not by a guessed wrapper name.
    plt = [(i.mnemonic, i.op_str) for i in md.disasm(read(0x425670, 16), 0x425670)]
    assert plt == [('adrp','x16, #0x3f84000'),('ldr','x17, [x16, #0x38]'),
                   ('add','x16, x16, #0x38'),('br','x17')]
    rel = elf.get_section_by_name('.rela.plt')
    symtab = elf.get_section(rel['sh_link'])
    rr = [r for r in rel.iter_relocations() if r['r_offset'] == 0x3f84038]
    assert len(rr) == 1 and rr[0]['r_info_type'] == 1026 and rr[0]['r_addend'] == 0
    sym = symtab.get_symbol(rr[0]['r_info_sym'])
    assert sym.name == 'pthread_self' and sym['st_shndx'] == 'SHN_UNDEF'
    # This only excludes direct literal B/BL to these three targets in the
    # selected bodies. It does not resolve indirect/transitive callees or the
    # route's later optional notification callback.
    forbidden = {0x1b029c0, 0x1e61cd8, 0x99ca38}
    for a in (0x237a440, 0x237eac0, 0x2379cc0):
        for pc in range(a, a+FUNCTIONS[a], 4):
            mnemonic, operand = ins[pc]
            if mnemonic in ('bl', 'b') and operand.startswith('#'):
                assert int(operand[1:], 16) not in forbidden
    rejected = []
    for label, (block, a, wrong) in CONTROLS.items():
        mutated = dict(ins); mutated[a] = wrong
        try: check_block(block, mutated)
        except AssertionError as failure:
            assert str(failure).startswith(block + ' ')
            rejected.append(label)
        else: raise AssertionError('Control accepted: ' + label)
    report = {
        'result': 'PASS', 'scope': 'Static exact-ELF relationships only; no runtime or firmware execution',
        'firmware_sha256': FIRMWARE,
        'instruction_anchors': sum(len(expected_lines(t)) for _, t in BLOCKS.values()) + 4,
        'blocks': len(BLOCKS), 'data_words': len(WORDS), 'RTTI_names': len(NAMES),
        'diagnostic_names': len(STRINGS), 'pthread_self_relocation': 'R_AARCH64_JUMP_SLOT',
        'strict_rejection_controls': rejected,
        'functions': {hex(a): {'FDE_size': n, 'sha256': sha(read(a,n))} for a,n in FUNCTIONS.items()},
        'relationships': {
            'timer_post_slot': '0x321b108 + 0x30 -> 0x237a440',
            'concrete_async_post_slot': '0x321afd8 + 0x30 -> 0x2379cc0',
            'queue_payload': 'copied task pointer; TaskNo returned from task + 0x10 on enqueue success',
            'drain': 'task execute slot +0x10, then freeTask; matching cancel TaskNo skips execute',
            'pool_control_RTTI': NAMES[0x2900848],
            'thread_predicate': 'selected submission uses +0x30, not +0x28; no direct B/BL to the two predicates in the three checked bodies; indirect/transitive behavior unproved',
        },
        'not_proven': ['live task-box identity or submission result', 'live queue execution/cancellation',
                       'dispatcher or child retained lifetime', 'post-return task validity',
                       'navigation, focus handback or native pause', 'whole-queue race/lifetime proof'],
    }
    text = json.dumps(report, indent=2) + '\n'
    if args.output: args.output.write_text(text)
    else: print(text, end='')

if __name__ == '__main__': main()

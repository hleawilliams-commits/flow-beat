#!/usr/bin/env python3
"""Verify bounded native-path/ABI anchors against the user's exact private EP147.
Emits hashes, addresses and conclusions only. Does not connect to any device.
Requires pyelftools and capstone. No source activation or firmware export.
"""
import argparse
import hashlib
import json
from pathlib import Path
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from elftools.elf.elffile import ELFFile

HASH = '6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
START, SIZE = 0x1ffb6c8, 0x20dc
HELPER, HELPER_SIZE = 0x1fd3a18, 0x2c8
CALL, RETURN, STORE, EMPTY_EDGE = 0x1ffbecc, 0x1ffbed0, 0x1ffbb0c, 0x1ffbea0
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('elf', type=Path)
ap.add_argument('--output', type=Path)
a = ap.parse_args()
raw = a.elf.read_bytes()
assert hashlib.sha256(raw).hexdigest() == HASH, 'Wrong firmware; no fallback'
with a.elf.open('rb') as f:
    elf = ELFFile(f)
    segments = [dict(s.header) for s in elf.iter_segments()]
    plt = elf.get_section_by_name('.plt')
    rel = list(elf.get_section_by_name('.rela.plt').iter_relocations())
    dyn = elf.get_section_by_name('.dynsym')
    nonreturning = {0x4271c0: '_ZSt25__throw_bad_function_callv',
                    0x426540: '_Unwind_Resume', 0x428930: '__cxa_rethrow'}
    for address, name in nonreturning.items():
        idx = (address - plt['sh_addr'] - 32) // 16
        assert dyn.get_symbol(rel[idx]['r_info_sym']).name == name

def read(address, size):
    s = next(s for s in segments if s['p_type'] == 'PT_LOAD' and
             s['p_vaddr'] <= address and address + size <= s['p_vaddr'] + s['p_filesz'])
    off = s['p_offset'] + address - s['p_vaddr']
    return raw[off:off + size]

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True
ins = {i.address: i for i in md.disasm(read(START, SIZE), START)}

def successors(i):
    if i.mnemonic in ('ret', 'brk', 'udf'):
        return []
    if i.mnemonic == 'br':
        raise AssertionError('Unresolved branch: ' + hex(i.address))
    if i.mnemonic == 'b':
        return [int(i.op_str[1:], 16)]
    if i.mnemonic == 'bl' and int(i.op_str[1:], 16) in nonreturning:
        return []
    if i.mnemonic.startswith('b.') or i.mnemonic in ('cbz', 'cbnz', 'tbz', 'tbnz'):
        return [int(i.op_str.split('#')[-1], 16), i.address + 4]
    return [i.address + 4]

def reach(start, removed=None):
    seen, todo = set(), [start]
    while todo:
        at = todo.pop()
        if at == removed or at in seen or at not in ins:
            continue
        seen.add(at)
        todo.extend(successors(ins[at]))
    return seen

anchors = {
    0x1ffbc90: ('ldp', 'x2, x0, [x1, #8]'),
    0x1ffbc94: ('cmp', 'x2, x0'),
    0x1ffbc98: ('b.eq', '#0x1ffbea0'),
    EMPTY_EDGE: ('ldr', 'x0, [x19, #0x28]'),
    0x1ffbea4: ('cbz', 'x0, #0x1ffbcf8'),
    0x1ffbea8: ('ldr', 'w4, [x19, #0x98]'),
    0x1ffbeb4: ('ldrh', 'w3, [x19, #0x9c]'),
    0x1ffbeb8: ('bfxil', 'x1, x4, #0, #0x20'),
    0x1ffbec0: ('bfi', 'x1, x3, #0x20, #0x10'),
    0x1ffbec4: ('bfi', 'x1, x2, #0x30, #8'),
    0x1ffbec8: ('and', 'x1, x1, #0xffffffffffffff'),
    CALL: ('bl', '#0x1fd3a18'),
    RETURN: ('cbz', 'w0, #0x1ffbcac'),
    0x1ffbf68: ('b', '#0x1ffbcf8'),
    0x1ffbe7c: ('mov', 'w0, #0'),
    0x1ffc0ac: ('mov', 'w0, #0'),
    0x1ffbfe4: ('mov', 'w0, #1'),
}
for at, pair in anchors.items():
    assert (ins[at].mnemonic, ins[at].op_str) == pair, hex(at)
assert CALL in reach(START)
assert CALL not in reach(START, STORE)
assert CALL not in reach(START, EMPTY_EDGE)
assert CALL not in reach(START, 0x1ffbc98)
assert 0x1ffbe7c in reach(RETURN + 4)
assert 0x1ffbe9c in reach(0x1ffbe7c)
helper = {i.address: i for i in md.disasm(read(HELPER, HELPER_SIZE), HELPER)}
for at, pair in {
    0x1fd3a24: ('mov', 'x19, x0'),
    0x1fd3a28: ('ldr', 'x0, [x0, #0x18]'),
    0x1fd3af4: ('add', 'x8, sp, #0x50'),
    0x1fd3af8: ('bl', '#0x1fc5590'),
    0x1fd3b04: ('ldrb', 'w0, [sp, #0x5c]'),
    0x1fd3b08: ('cbnz', 'w0, #0x1fd3b38'),
    0x1fd3b28: ('strb', 'w3, [x19, #0x38]'),
    0x1fd3ad8: ('mov', 'w0, #-1'),
    0x1fd3bc8: ('mov', 'w0, #-1'),
}.items():
    assert (helper[at].mnemonic, helper[at].op_str) == pair, hex(at)

# Extra incoming argument registers must not be consumed by the entry wrapper's
# native target. First callees are separately described in README.md.
def reg(register):
    name = md.reg_name(register)
    if len(name) > 1 and name[1:].isdigit():
        if name[0] == 'w':
            return 'x' + name[1:]
        if name[0] in 'qdsbh':
            return 'v' + name[1:]
    return name

extra = {'x' + str(n) for n in range(2, 9)} | {'v' + str(n) for n in range(8)}
incoming, todo, consumed, first_calls = {}, [(HELPER, extra)], set(), set()
while todo:
    at, taint = todo.pop()
    if at not in helper or at in incoming and taint <= incoming[at]:
        continue
    incoming.setdefault(at, set()).update(taint)
    i = helper[at]
    reads, writes = i.regs_access()
    consumed.update((at, x) for x in map(reg, reads) if x in taint)
    out = taint - set(map(reg, writes))
    if i.mnemonic in ('bl', 'blr'):
        if out:
            first_calls.add((at, i.op_str.removeprefix('#')))
        out = set()
    todo.extend((x, out) for x in successors(i))
assert not consumed
assert first_calls == {(0x1fd3a58, '0x24a1180'), (0x1fd3af8, '0x1fc5590'), (0x1fd3bf0, '0x24a1180')}
# Sanity-check exact construction/replacement anchors, without publishing code.
external = {
    0x1ffb68c: ('mov', 'w1, #0'),
    0x200f0b8: ('strh', 'w20, [x19, #0x78]'),
    0x1ffb514: ('cmp', 'w1, w2'),
    0x1ffb518: ('cset', 'w2, ls'),
    0x1ffb524: ('cbnz', 'w2, #0x1ffb540'),
    0x201fd60: ('str', 'xzr, [x24]'),
    0x201fd68: ('str', 'x21, [x20, #0x10]'),
    0x201fd78: ('blr', 'x1'),
    0x200f148: ('stp', 'xzr, xzr, [x19, #0x28]'),
}
for at, pair in external.items():
    i = next(md.disasm(read(at, 4), at))
    assert (i.mnemonic, i.op_str) == pair, hex(at)
result = {
    'firmware_sha256': HASH,
    'event_handler': {'address': hex(START), 'fde_extent': hex(SIZE)},
    'candidate': {'address': hex(HELPER), 'fde_extent': hex(HELPER_SIZE),
                  'call': hex(CALL), 'native_return': hex(RETURN),
                  'prologue_sha256_16': hashlib.sha256(read(HELPER, 16)).hexdigest(),
                  'abi': 'int (void *request_agent, uint64_t packed_request)',
                  'agent_source': 'live MediaChangeEvent +0x28',
                  'packed_fields': {'0:31': 'MediaID type', '32:47': 'MediaID identity',
                                    '48:55': 'opaque native per-media category byte; no expected value compared', '56:63': 'zero'}},
    'normal_cfg': {'commit_store_dominates_candidate': True, 'empty_list_equality_edge_dominates_candidate': True,
                   'nonzero_helper_result_can_rejoin_outer_return_0': True},
    'helper_abi': {'instruction_count': len(helper), 'normal_reachable_count': len(incoming),
                   'incoming_extra_register_reads_before_write_or_call': [],
                   'first_calls': [{'site': hex(x), 'target': y} for x, y in sorted(first_calls)]},
    'cancellation': {'media_event_type': 0, 'replacement_predicate_permits_new_type_0': True,
                     'executor_can_replace_and_delete_prior_event': True,
                     'final_async_cancellation_completion_proven': False},
    'limits': ['Scalar branch observation is not committed identity or final completion',
               'Static branch dominance covers ordinary native control flow only',
               'Additional native callbacks can re-enter, fail or throw',
               'No captured pointer may be dereferenced after original return',
               'First-call helper ABI annotations are in README; not a whole-program proof']
}
text = json.dumps(result, indent=2) + '\n'
if a.output:
    a.output.write_text(text)
print(text, end='')

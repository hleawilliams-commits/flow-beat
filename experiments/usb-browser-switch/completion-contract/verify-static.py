#!/usr/bin/env python3
"""Check narrow normal-flow anchors in the user's own exact firmware ELF.
Requires pyelftools and capstone. Emits only hashes, addresses and conclusions.
No runtime connection, code patch, disassembly export or activation operation.
"""
import argparse
import hashlib
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

EXPECTED_SHA = '6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
START, EXTENT = 0x1ffb6c8, 0x20dc  # exact ELF FDE, independently inspected
STORE, CALL, RETURN, HELPER = 0x1ffbb0c, 0x1ffbca4, 0x1ffbca8, 0x2014a88
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('elf', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
raw = args.elf.read_bytes()
assert hashlib.sha256(raw).hexdigest() == EXPECTED_SHA, 'Wrong firmware; no fallback'
with args.elf.open('rb') as stream:
    elf = ELFFile(stream)
    segments = [dict(s.header) for s in elf.iter_segments()]
    plt = elf.get_section_by_name('.plt')
    relocations = list(elf.get_section_by_name('.rela.plt').iter_relocations())
    symbols = elf.get_section_by_name('.dynsym')
    nonreturning = {}
    for address, expected_name in {
        0x4271c0: '_ZSt25__throw_bad_function_callv',
        0x426540: '_Unwind_Resume',
        0x428930: '__cxa_rethrow',
    }.items():
        index = (address - plt['sh_addr'] - 32) // 16
        actual_name = symbols.get_symbol(relocations[index]['r_info_sym']).name
        assert actual_name == expected_name
        nonreturning[address] = actual_name

def read_at(address, length):
    segment = next(s for s in segments if s['p_type'] == 'PT_LOAD' and
                   s['p_vaddr'] <= address and address + length <= s['p_vaddr'] + s['p_filesz'])
    offset = segment['p_offset'] + address - segment['p_vaddr']
    return raw[offset:offset + length]

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True
ins = {i.address: i for i in md.disasm(read_at(START, EXTENT), START)}
assert len(ins) == EXTENT // 4
expected = {
    STORE: ('strb', 'w0, [x21, #0x1e5]'),
    0x1ffbc9c: ('mov', 'w1, #0'),
    0x1ffbca0: ('mov', 'x0, x19'),
    CALL: ('bl', '#0x2014a88'),
    RETURN: ('cbnz', 'w0, #0x1ffbcf8'),
}
for address, pair in expected.items():
    assert (ins[address].mnemonic, ins[address].op_str) == pair, hex(address)

def successors(i):
    if i.mnemonic in ('ret', 'brk', 'udf'):
        return []
    if i.mnemonic == 'br':
        raise AssertionError('Unresolved indirect branch: ' + hex(i.address))
    if i.mnemonic == 'b':
        return [int(i.op_str[1:], 16)]
    if i.mnemonic == 'bl' and int(i.op_str[1:], 16) in nonreturning:
        return []
    if i.mnemonic.startswith('b.') or i.mnemonic in ('cbz', 'cbnz', 'tbz', 'tbnz'):
        return [int(i.op_str.split('#')[-1], 16), i.address + 4]
    # Calls may throw/re-enter: this analysis concerns ordinary returned control only.
    return [i.address + 4]

def reachable(start, removed=None, stop=None):
    seen, pending = set(), [start]
    while pending:
        address = pending.pop()
        if address == removed or address in seen or address not in ins:
            continue
        seen.add(address)
        if address != stop:
            pending.extend(successors(ins[address]))
    return seen

full = reachable(START)
assert CALL in full
assert CALL not in reachable(START, STORE)
revisits_commit = STORE in reachable(RETURN)
reverse = {a: [] for a in ins}
for a in full:
    for b in successors(ins[a]):
        if b in reverse:
            reverse[b].append(a)
can_reach_call, pending = set(), [CALL]
while pending:
    address = pending.pop()
    if address in can_reach_call:
        continue
    can_reach_call.add(address)
    pending.extend(reverse[address])
region = reachable(STORE + 4, stop=CALL) & can_reach_call
calls = [{'site': hex(a), 'target': i.op_str.removeprefix('#')} for a, i in ins.items()
         if a in region and a != CALL and i.mnemonic in ('bl', 'blr')]
# All direct visible identity writes in this handler use this fixed field layout.
identity_stores = [hex(a) for a, i in ins.items() if i.mnemonic.startswith('st') and
                   any(field in i.op_str for field in ('#0x1e0]', '#0x1e4]', '#0x1e5]'))]

# Check the exact helper's explicit argument-register dataflow. Known ordinary
# calls clobber these caller-saved values; first callees are separately described
# in README.md, rather than claiming a whole-program ABI/lifetime proof here.
helper_ins = {i.address: i for i in md.disasm(read_at(HELPER, 0x844), HELPER)}
def normal_reg(register):
    name = md.reg_name(register)
    if len(name) > 1 and name[1:].isdigit():
        if name[0] == 'w':
            return 'x' + name[1:]
        if name[0] in 'qdsbh':
            return 'v' + name[1:]
    return name

extra = {'x' + str(n) for n in range(2, 9)} | {'v' + str(n) for n in range(8)}
incoming, pending, explicit_reads, first_calls = {}, [(HELPER, extra)], set(), set()
while pending:
    address, tainted = pending.pop()
    if address not in helper_ins:
        continue
    if address in incoming and tainted <= incoming[address]:
        continue
    incoming.setdefault(address, set()).update(tainted)
    i = helper_ins[address]
    reads, writes = i.regs_access()
    explicit_reads.update((address, r) for r in map(normal_reg, reads) if r in tainted)
    outgoing = tainted - set(map(normal_reg, writes))
    if i.mnemonic in ('bl', 'blr'):
        if outgoing:
            first_calls.add((address, i.op_str))
        outgoing = set()
    pending.extend((a, outgoing) for a in successors(i))
assert not explicit_reads, 'Additional helper argument is consumed'
assert first_calls == {(0x2014ab0, '#0x2018870'), (0x201504c, '#0x24a1180')}
assert (helper_ins[0x2014aac].mnemonic, helper_ins[0x2014aac].op_str) == ('and', 'w21, w1, #0xff')
assert (helper_ins[0x2014c60].mnemonic, helper_ins[0x2014c60].op_str) == ('mov', 'w0, w24')
result = {
    'firmware_sha256': EXPECTED_SHA,
    'handler': {'start': hex(START), 'fde_extent': hex(EXTENT),
                'instruction_count': len(ins), 'normal_reachable_count': len(full)},
    'candidate': {'callee': hex(HELPER), 'native_call': hex(CALL),
                  'native_return': hex(RETURN), 'argument0': 'current native event (x19)',
                  'argument1': 0,
                  'prologue_sha256_16': hashlib.sha256(read_at(HELPER, 16)).hexdigest()},
    'final_identity_store_dominates_call_on_normal_paths': True,
    'normal_return_path_can_revisit_commit': revisits_commit,
    'verified_nonreturning_plt': {hex(a): name for a, name in nonreturning.items()},
    'intervening_calls': calls,
    'direct_identity_stores': identity_stores,
    'helper_abi': {
        'fde_extent': '0x844', 'instruction_count': len(helper_ins),
        'normal_reachable_count': len(incoming),
        'wrapper_type': 'int (void *native_event, unsigned mode)',
        'mode_consumption': 'w1 masked to byte at 0x2014aac',
        'native_caller_result': 'w0 consumed by cbnz at 0x1ffbca8',
        'additional_argument_registers_checked': sorted(extra),
        'explicit_read_before_write_or_call_clobber': [],
        'first_calls_while_extra_registers_unused': [
            {'site': hex(a), 'target': t.removeprefix('#')}
            for a, t in sorted(first_calls)
        ],
        'limits': 'Explicit function-register dataflow; first callee ABI anchors are documented separately'
    },
    'proof_limits': [
        'Not a whole-program ownership or final transaction-completion proof',
        'Intervening native calls may re-enter; live argument/context must be revalidated',
        'Exception landing-pad entry is outside normal CFG dominance',
        'Native helper callees may modify identity later; no post-return snapshot authorized',
        'Empty-list/already-current paths may produce no candidate callback'
    ]
}
text = json.dumps(result, indent=2) + '\n'
if args.output:
    args.output.write_text(text)
print(text, end='')

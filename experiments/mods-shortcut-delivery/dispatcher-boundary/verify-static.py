#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only dispatcher/queued executor anchors and actual installer policy.

Uses the owner's exact private firmware and git objects from pinned upstream.
Emits semantic anchors and hashes, not firmware dumps. Does not call native
functions, allocate an island, install a hook, or access any emulator/device.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import tarfile
import tempfile

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from elftools.dwarf.callframe import FDE
from elftools.elf.elffile import ELFFile

PIN = "3e0c5cb010454228f4ee63226b60d060494eeaae"
FIRMWARE = "6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925"
CANDIDATES = {0x1adea38: 0x4a8, 0x1adeee0: 0x588, 0x1add3b8: 0x1c}
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--elf", type=Path, required=True)
ap.add_argument("--upstream", type=Path, required=True)
ap.add_argument("--cc", default="cc")
args = ap.parse_args()
digest = lambda b: hashlib.sha256(b).hexdigest()
raw = args.elf.read_bytes()
assert digest(raw) == FIRMWARE, "Different firmware; no fallback"
elf = ELFFile(io.BytesIO(raw))
assert elf.elfclass == 64 and elf.little_endian
assert elf['e_machine'] == 'EM_AARCH64' and elf['e_type'] == 'ET_EXEC'


def read(address, size):
    for seg in elf.iter_segments():
        if (seg['p_type'] == 'PT_LOAD' and seg['p_vaddr'] <= address
                and address + size <= seg['p_vaddr'] + seg['p_filesz']):
            off = seg['p_offset'] + address - seg['p_vaddr']
            return raw[off:off + size]
    raise AssertionError('Address is not file-backed')


def word(address):
    return struct.unpack('<Q', read(address, 8))[0]


decoder = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
fdes = {entry['initial_location']: entry['address_range']
        for entry in elf.get_dwarf_info().EH_CFI_entries()
        if isinstance(entry, FDE)}
for start, size in CANDIDATES.items():
    assert fdes[start] == size
    assert any(seg['p_type'] == 'PT_LOAD' and seg['p_flags'] & 1
               and seg['p_vaddr'] <= start
               and start + size <= seg['p_vaddr'] + seg['p_filesz']
               for seg in elf.iter_segments())
ins = {}
for start, size in CANDIDATES.items():
    ins.update({i.address: (i.mnemonic, i.op_str)
                for i in decoder.disasm(read(start, size), start)})

# Normal and adjusted-this entries share the same scalar forwarding and queue.
# x1 at entry is overwritten without being consumed by either dispatcher.
anchors = {
    0x1adea50: ('mov', 'x22, x0'),
    0x1adea54: ('mov', 'x21, x2'),
    0x1adea5c: ('mov', 'w23, w3'),
    0x1adea60: ('mov', 'w24, w4'),
    0x1adea7c: ('str', 'x21, [sp, #0xa0]'),
    0x1adea88: ('blr', 'x1'),
    0x1adea8c: ('tst', 'w0, #0xff'),
    0x1adea90: ('b.ne', '#0x1adeba0'),
    0x1adea94: ('ldr', 'x0, [x22]'),
    0x1adea98: ('mov', 'w3, w24'),
    0x1adea9c: ('ldr', 'x1, [sp, #0xa0]'),
    0x1adeaa0: ('mov', 'w2, w23'),
    0x1adeaa4: ('ldr', 'x4, [x0, #0x198]'),
    0x1adeaa8: ('mov', 'x0, x22'),
    0x1adeaac: ('blr', 'x4'),
    0x1adeac4: ('ret', ''),
    0x1adec24: ('stp', 'w23, w24, [sp, #0xa8]'),
    0x1adec28: ('adrp', 'x1, #0x2d3d000'),
    0x1adec30: ('add', 'x1, x1, #0xee0'),
    0x1adec34: ('str', 'x26, [x19, #0x10]'),
    0x1adec3c: ('str', 'x1, [x0], #0x18'),
    0x1adec44: ('ldp', 'x2, x3, [sp, #0xa0]'),
    0x1adec48: ('stp', 'x2, x3, [x19, #0x18]'),
    0x1adec4c: ('str', 'x22, [x0, #0x10]'),
    0x1adec7c: ('mov', 'x1, x19'),
    0x1adec80: ('mov', 'x0, x25'),
    0x1adec84: ('bl', '#0x237a440'),
    0x1adeef8: ('mov', 'x21, x2'),
    0x1adeefc: ('sub', 'x22, x0, #0x118'),
    0x1adef04: ('mov', 'w23, w3'),
    0x1adef08: ('mov', 'w24, w4'),
    0x1adef2c: ('blr', 'x1'),
    0x1adef30: ('tst', 'w0, #0xff'),
    0x1adef34: ('b.ne', '#0x1adf048'),
    0x1adef38: ('ldr', 'x4, [x22]'),
    0x1adef3c: ('mov', 'w3, w24'),
    0x1adef40: ('mov', 'w2, w23'),
    0x1adef44: ('mov', 'x1, x21'),
    0x1adef48: ('mov', 'x0, x22'),
    0x1adef4c: ('ldr', 'x4, [x4, #0x198]'),
    0x1adef50: ('str', 'x21, [sp, #0x98]'),
    0x1adef54: ('blr', 'x4'),
    0x1adef6c: ('ret', ''),
    0x1adf0c8: ('adrp', 'x0, #0x2d3d000'),
    0x1adf0cc: ('add', 'x0, x0, #0xee0'),
    0x1adf0dc: ('str', 'x0, [x19]'),
    0x1adf0e4: ('stp', 'x27, x21, [x19, #0x10]'),
    0x1adf0e8: ('stp', 'w23, w24, [x19, #0x20]'),
    0x1adf0ec: ('str', 'x22, [x19, #0x28]'),
    0x1adf11c: ('mov', 'x1, x19'),
    0x1adf120: ('mov', 'x0, x25'),
    0x1adf124: ('bl', '#0x237a440'),
    0x1add3b8: ('ldr', 'x4, [x0, #0x28]'),
    0x1add3bc: ('ldp', 'w2, w3, [x0, #0x20]'),
    0x1add3c0: ('ldr', 'x1, [x0, #0x18]'),
    0x1add3c4: ('mov', 'x0, x4'),
    0x1add3c8: ('ldr', 'x4, [x4]'),
    0x1add3cc: ('ldr', 'x4, [x4, #0x198]'),
    0x1add3d0: ('br', 'x4'),
}
for address, expected in anchors.items():
    assert ins[address] == expected, hex(address)
for start, first_write in [(0x1adea38, 0x1adea78), (0x1adeee0, 0x1adef20)]:
    assert ins[first_write][0] == 'ldr' and ins[first_write][1].startswith('x1,')
    assert all(not re.search(r'\bx1\b', ins[address][1])
               for address in range(start, first_write, 4))

# The synchronous paths restore callee-saved registers/SP and return. Their
# downstream result is untouched, but the queued path has a different return;
# this is not evidence for a C return type or an accepted/completed status.
for start, end in [(0x1adeab0, 0x1adeac4), (0x1adef58, 0x1adef6c)]:
    for address in range(start, end, 4):
        mnemonic, operands = ins[address]
        assert mnemonic == 'ldp' and not re.match(r'[xw][01],', operands)

assert word(0x2d61ae0 + 0x198) == 0x1b3ab60
assert word(0x2d61ae0 + 0x1b0) == 0x1adea38
assert struct.unpack('<q', read(0x2d61d70, 8))[0] == -0x118
assert word(0x2d61d78) == 0x2d616d0
assert word(0x2d61d80 + 0x18) == 0x1adeee0
assert word(0x2d3ded8) == 0x2d3de58
assert word(0x2d3dee0 + 0x10) == 0x1add3b8
task_name_at = word(0x2d3de58 + 8)
task_name = read(task_name_at, 200).split(b'\0', 1)[0].decode()
assert task_name == ('N4meow16AsyncTaskBoxBase9AsyncTaskIZNS_14HuiAdaptedView'
                     'INS_21DefaultGuiTaskBoxConfEE16didRotateEncoderERKNS_11'
                     'IHuiEncoderENS_9EasyID_ExILb0EEEiiEUlvE_EE')

# Pin all source reads; never inspect dirty worktree files for policy evidence.
archive = subprocess.check_output(['git', 'archive', PIN, 'src/azmod'], cwd=args.upstream)
with tarfile.open(fileobj=io.BytesIO(archive)) as tf:
    sources = {m.name: tf.extractfile(m).read() for m in tf.getmembers()
               if m.isfile() and m.name.endswith(('.c', '.h'))}
h = sources['src/azmod/inline_hooks.c'].decode()
cm = sources['src/azmod/community.c'].decode()


def function(name, source=h):
    start = source.index('static int ' + name + '(')
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


policy = '\n\n'.join(function(n) for n in
                     ['azm_pcrel_kind', 'azm_ends_fn', 'azm_patch_ok'])
install = function('azm_install_hook_at')
assert install.index('if (!azm_patch_ok(func, 1))') < install.index('azm_island_slot(FUNC)')
assert 'if (!isl && __atomic_load_n(&g_azm_late, __ATOMIC_ACQUIRE))' in install
assert 'return azm_rec(FUNC, name, AZM_REFUSED_LATE);' in install
claim = function('cm_claim_hook', cm)
overlap = 'addr < g_hooked[i].addr + 16 && g_hooked[i].addr < addr + 16'
assert overlap in claim
assert claim.index(overlap) < claim.index('azm_install_hook(addr, hook, orig)')
assert 'addr < g_cm_text_lo || addr >= g_cm_text_hi' in claim
assert '!g_sdk_in_init || (c - g_cm) != g_cm_loading' in claim

prologues = {a: struct.unpack('<4I', read(a, 16)) for a in CANDIDATES}
rows = ',\n'.join('{' + ','.join(hex(w) + 'u' for w in words) + '}'
                  for words in prologues.values())
fixture = '''
#include <stdint.h>
#include <stdio.h>
''' + policy + '''
static struct { uintptr_t addr; } g_hooked[1];
static int conflict(uintptr_t addr) { int i = 0; return ''' + overlap + '''; }
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) return checks; } while (0)
int main(void) {
    const uint32_t words[3][4] = {''' + rows + '''};
    for (int n = 0; n < 3; ++n) {
        for (int i = 0; i < 4; ++i)
            CHECK(azm_pcrel_kind(words[n][i]) == (n < 2 && i == 3 ? 2 : 0));
        CHECK(azm_patch_ok(words[n], 1) == 1);
        CHECK(azm_patch_ok(words[n], 4) == 1);
        for (int i = 0; i < 4; ++i) {
            uint32_t changed[4];
            for (int j = 0; j < 4; ++j) changed[j] = words[n][j];
            changed[i] = 0x36080064u; /* rejected TBZ fixture, never image write */
            CHECK(azm_patch_ok(changed, 1) == 0);
            CHECK(azm_patch_ok(changed, 4) == 0);
        }
        uint32_t ended[4] = {0xd65f03c0u,0xd503201fu,0xd503201fu,0xd503201fu};
        CHECK(azm_patch_ok(ended, 1) == 1);
        CHECK(azm_patch_ok(ended, 4) == 0);
    }
    const uintptr_t candidates[] = {0x1adea38u,0x1adeee0u,0x1add3b8u};
    for (int n = 0; n < 3; ++n) {
        g_hooked[0].addr = 0x1b10860u; /* preserved Browse owner */
        CHECK(!conflict(candidates[n]));
        g_hooked[0].addr = candidates[n];
        CHECK(conflict(candidates[n]));
        CHECK(conflict(candidates[n]-15));
        CHECK(conflict(candidates[n]+15));
        CHECK(!conflict(candidates[n]-16));
        CHECK(!conflict(candidates[n]+16));
    }
    printf("%d\\n", checks);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='az-dispatcher-policy-') as tmp:
    c, binary = Path(tmp) / 'policy.c', Path(tmp) / 'policy'
    c.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(c), '-o', str(binary)], check=True)
    checks = int(subprocess.check_output([str(binary)], text=True).strip())
assert checks == 66

# Conservative static audit: no numeric literal in pinned azmod C/headers is
# within the forbidden 16-byte overlap of these entries, including v200 address
# table values. This cannot inspect computed runtime hook addresses/other mods.
literal_collisions = []
for path, data in sources.items():
    for match in re.finditer(rb'\b0[xX]([0-9a-fA-F]+)[uUlL]*\b', data):
        value = int(match.group(1), 16)
        for candidate in CANDIDATES:
            if candidate < value + 16 and value < candidate + 16:
                literal_collisions.append([path, hex(value), hex(candidate)])
assert literal_collisions == []

result = {
    'upstream_commit': PIN,
    'firmware_sha256': FIRMWARE,
    'source_sha256': {n: digest(sources['src/azmod/' + n])
                      for n in ['inline_hooks.c', 'community.c', 'ep147addr.h']},
    'actual_policy_extraction_sha256': digest(policy.encode()),
    'actual_overlap_predicate_sha256': digest(overlap.encode()),
    'host_policy_assertions': checks,
    'instruction_anchors_checked': len(anchors),
    'candidates': {
        hex(a): {'fde_extent': hex(size), 'body_sha256': digest(read(a, size)),
                 'prologue_sha256': digest(read(a, 16)),
                 'pcrel_kinds': [0, 0, 0, 0 if a == 0x1add3b8 else 2],
                 'actual_patch_ok_one_word': True, 'actual_patch_ok_four_words': True,
                 'file_backed_executable_segment': True}
        for a, size in CANDIDATES.items()},
    'negative_controls': {
        'tbz_in_each_of_four_positions_rejected_in_both_modes': True,
        'return_in_overwritten_prefix_rejects_four_word_fallback': True,
        'overlap_same_and_plus_minus_15_refused_plus_minus_16_clear': True},
    'pinned_source_literal_audit': {'source_file_count': len(sources),
                                    'collisions': literal_collisions,
                                    'runtime_hook_ownership_proven': False},
    'waveform_primary_slot_0x1b0': '0x1adea38',
    'waveform_secondary_at_plus_0x118_slot_0x18': '0x1adeee0',
    'native_virtual_target_slot': '0x198',
    'synchronous_virtual_call_sites': ['0x1adeaac', '0x1adef54'],
    'async_task_vtable': '0x2d3dee0',
    'async_task_execute_slot_0x10': '0x1add3b8',
    'async_task_rtti_sha256': digest(task_name.encode()),
    'async_task_method_name': 'HuiAdaptedView<DefaultGuiTaskBoxConf>::didRotateEncoder',
    'copied_payload': {'0x18': 'entry x2 (opaque 64 bits)',
                       '0x20': 'entry w3', '0x24': 'entry w4',
                       '0x28': 'primary view address (adjusted by -0x118 for secondary entry)'},
    'enqueue_call_sites': ['0x1adec84', '0x1adf124'],
    'enqueue_target': '0x237a440',
    'dispatcher_entry_observation_does_not_imply_immediate_delivery': True,
    'native_return_type_proven': False,
    'native_counter_ordering_proven': False,
    'task_or_view_lifetime_after_return_proven': False,
    'physical_knob_or_main_thread_provenance_observed': False,
    'runtime_claim_attempted': False,
    'successful_installation_proven': False,
    'limits': [
        'Instruction policy acceptance is conditional eligibility, not SDK claim success',
        'Late claim still needs a reachable executable island and unoccupied runtime address',
        'These generic dispatchers serve multiple views; live Waveform provenance remains unproven',
        'A dispatcher has five input integer registers; the four-register downstream wrapper is insufficient',
        'Queued task executor accepts a task object, not the downstream four-argument signature',
        'No event consumption, activation, focus handback, cancellation or runtime pointer reads are authorized by this static result'
    ]
}
print(json.dumps(result, indent=2))

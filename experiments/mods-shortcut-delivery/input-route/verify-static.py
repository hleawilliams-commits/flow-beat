#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only native encoder route anchors and actual installer policy.

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
FUNCTIONS = {0x1b01cf8: 0x2e8, 0x1b029c0: 0x104, 0x2360f80: 0x134,
             0x235c270: 0x1b8, 0x1afe1f0: 0x3a4}
CANDIDATES = {0x1b01cf8: 0x2e8}
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
for start, size in FUNCTIONS.items():
    assert fdes[start] == size
    assert any(seg['p_type'] == 'PT_LOAD' and seg['p_flags'] & 1
               and seg['p_vaddr'] <= start
               and start + size <= seg['p_vaddr'] + seg['p_filesz']
               for seg in elf.iter_segments())
ins = {}
for start, size in FUNCTIONS.items():
    ins.update({i.address: (i.mnemonic, i.op_str)
                for i in decoder.disasm(read(start, size), start)})

# These are file-backed static anchors, not live pointer dereferences.
anchors = {
  0x1b01cf8: ('stp', 'x29, x30, [sp, #-0xc0]!'),
  0x1b01cfc: ('mov', 'x29, sp'),
  0x1b01d00: ('stp', 'x19, x20, [sp, #0x10]'),
  0x1b01d04: ('stp', 'x21, x22, [sp, #0x20]'),
  0x1b01d08: ('mov', 'w21, w4'),
  0x1b01d10: ('mov', 'w24, w3'),
  0x1b01d18: ('mov', 'x26, x2'),
  0x1b01d1c: ('sub', 'x25, x0, #0x118'),
  0x1b01d20: ('bl', '#0x1b00e10'),
  0x1b01dac: ('adrp', 'x1, #0x2d44000'),
  0x1b01db4: ('add', 'x1, x1, #0x4d8'),
  0x1b01db8: ('str', 'x1, [x19]'),
  0x1b01dc0: ('stp', 'x27, x25, [x19, #0x10]'),
  0x1b01dc4: ('str', 'x26, [x19, #0x20]'),
  0x1b01dc8: ('stp', 'w24, w21, [x19, #0x28]'),
  0x1b01d24: ('mov', 'x20, x0'),
  0x1b01d28: ('ldr', 'x0, [x0]'),
  0x1b01d2c: ('ldr', 'x23, [x0, #0x30]'),
  0x1b01df8: ('mov', 'x1, x19'),
  0x1b01dfc: ('mov', 'x0, x20'),
  0x1b01e00: ('blr', 'x23'),
  0x1b029c0: ('ldr', 'x3, [x0, #0x18]'),
  0x1b029cc: ('add', 'x2, x3, #0x19c'),
  0x1b029ec: ('ldr', 'w21, [x3, #0x19c]'),
  0x1b029f4: ('add', 'x20, x0, #0x18'),
  0x1b029f8: ('mov', 'x19, x0'),
  0x1b029fc: ('bl', '#0x1b02858'),
  0x1b02a00: ('cmp', 'w21, w0'),
  0x1b02a04: ('b.eq', '#0x1b02a1c'),
  0x1b02a1c: ('ldr', 'x4, [x19, #0x18]'),
  0x1b02a24: ('ldp', 'w2, w3, [x20, #0x10]'),
  0x1b02a2c: ('ldr', 'x0, [x4, #0x1a0]'),
  0x1b02a30: ('cbz', 'x0, #0x1b02a4c'),
  0x1b02a34: ('ldr', 'x1, [x0]'),
  0x1b02a40: ('ldr', 'x5, [x1, #0x198]'),
  0x1b02a44: ('cmp', 'x5, x21'),
  0x1b02a48: ('b.ne', '#0x1b02a8c'),
  0x1b02a4c: ('ldr', 'x0, [x4, #0x150]'),
  0x1b02a50: ('cbz', 'x0, #0x1b02a64'),
  0x1b02a54: ('ldr', 'x1, [x0]'),
  0x1b02a58: ('ldr', 'x5, [x1, #0x198]'),
  0x1b02a5c: ('cmp', 'x5, x21'),
  0x1b02a60: ('b.ne', '#0x1b02aa8'),
  0x1b02a64: ('ldr', 'x0, [x4]'),
  0x1b02a68: ('ldr', 'x5, [x0, #0x198]'),
  0x1b02a6c: ('cmp', 'x5, x21'),
  0x1b02a70: ('b.eq', '#0x1b02a08'),
  0x1b02a74: ('ldr', 'x1, [x20, #8]'),
  0x1b02a78: ('mov', 'x0, x4'),
  0x1b02a88: ('br', 'x5'),
  0x1b02a8c: ('ldr', 'x1, [x20, #8]'),
  0x1b02a90: ('blr', 'x5'),
  0x1b02a94: ('tst', 'w0, #0xff'),
  0x1b02a98: ('b.ne', '#0x1b02a08'),
  0x1b02a9c: ('ldp', 'w2, w3, [x20, #0x10]'),
  0x1b02aa0: ('ldr', 'x4, [x19, #0x18]'),
  0x1b02aa8: ('ldr', 'x1, [x20, #8]'),
  0x1b02aac: ('blr', 'x5'),
  0x1b02ab0: ('tst', 'w0, #0xff'),
  0x1b02ab4: ('b.ne', '#0x1b02a08'),
  0x1b02ab8: ('ldp', 'w2, w3, [x20, #0x10]'),
  0x1b02abc: ('ldr', 'x4, [x19, #0x18]'),
  0x1afe24c: ('adrp', 'x3, #0x2d45000'),
  0x1afe254: ('add', 'x3, x3, #0x3d0'),
  0x1afe270: ('str', 'x3, [x19, #0x118]'),
  0x1afe32c: ('adrp', 'x13, #0x2d45000'),
  0x1afe334: ('add', 'x13, x13, #0x740'),
  0x1afe370: ('str', 'x13, [x19, #0x118]'),
  0x2360fa8: ('mov', 'x21, x2'),
  0x2360fb4: ('mov', 'x20, x0'),
  0x2360fb8: ('mov', 'x19, x1'),
  0x2361018: ('ldr', 'w3, [x21]'),
  0x236101c: ('cbz', 'w3, #0x2360fc8'),
  0x236102c: ('ldr', 'x2, [x19]'),
  0x2361034: ('ldr', 'x22, [x2, #0x18]'),
  0x2361040: ('ldr', 'x0, [x20, #0xd0]'),
  0x2361068: ('ldr', 'w4, [x21, #4]'),
  0x236106c: ('mov', 'x2, x0'),
  0x2361070: ('add', 'x1, x20, #0xc0'),
  0x2361074: ('mov', 'x0, x19'),
  0x2361078: ('mov', 'x5, x22'),
  0x2361088: ('br', 'x5'),
  0x235c304: ('ldr', 'w20, [x20]'),
  0x235c330: ('ldr', 'w24, [x21, #0xd8]'),
  0x235c338: ('add', 'w24, w20, w24'),
  0x235c33c: ('str', 'w24, [x21, #0xd8]'),
  0x235c3ac: ('str', 'w20, [x0, x23]'),
  0x235c3b0: ('str', 'w24, [x1, #4]'),
}
for address, expected in anchors.items():
    assert ins[address] == expected, hex(address)
words = {
  0x2d61c90: 0x1adea38, 0x2d61d98: 0x1adeee0,
  0x2d61c78: 0x1b3ab60,
  0x2d453c0: (1 << 64) - 0x118, 0x2d453c8: 0x2d44890,
  0x2d453e8: 0x1b01cf8,
  0x2d45730: (1 << 64) - 0x118, 0x2d45738: 0x2d448e8,
  0x2d45758: 0x1b01cf8,
  0x2d444d0: 0x2d44450, 0x2d444e8: 0x1b029c0,
  0x3216948: 0x2360f80, 0x3216958: 0x235c270,
}
for address, expected in words.items():
    assert word(address) == expected, hex(address)
names = {
  0x2d44890: 'N4meow24HuiAdaptedViewDispatcherINS_21DefaultGuiTaskBoxConfEEE',
  0x2d448e8: 'N3gui21ViewTransitionManagerE',
  0x2d44450: ('N4meow16AsyncTaskBoxBase9AsyncTaskIZNS_24HuiAdaptedViewDispatcher'
                'INS_21DefaultGuiTaskBoxConfEE16didRotateEncoderERKNS_11IHuiEncoder'
                'ENS_9EasyID_ExILb0EEEiiEUlvE_EE'),
}
for address, expected in names.items():
    assert read(word(address + 8), 240).split(b'\0', 1)[0].decode() == expected
# No incoming x1 use before the task-box helper call: encoder reference need not
# be dereferenced by a passive observer. Record it only as an opaque scalar.
assert all(not re.search(r'\b[xw]1\b', ins[a][1])
           for a in range(0x1b01cf8, 0x1b01d20, 4))
# Discriminating controls use wrong expected semantics, never mutate firmware.
negative_anchors = {
  0x1b01dc8: ('stp', 'w21, w24, [x19, #0x28]'),
  0x1b02a24: ('ldp', 'w2, w3, [x20, #8]'),
  0x1b02a40: ('ldr', 'x5, [x1, #0x1b0]'),
  0x1b02a98: ('b.eq', '#0x1b02a08'),
  0x2361068: ('ldr', 'w4, [x21]'),
}
for address, wrong in negative_anchors.items():
    assert ins[address] != wrong, hex(address)
assert word(0x2d453e8) != word(0x2d61d98)
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

prologue = struct.unpack('<4I', read(0x1b01cf8, 16))
row = ','.join(hex(w) + 'u' for w in prologue)
fixture = '#include <stdint.h>\n#include <stdio.h>\n' + policy + """
static struct { uintptr_t addr; } g_hooked[1];
static int conflict(uintptr_t addr) { int i = 0; return """ + overlap + """; }
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) return checks; } while (0)
int main(void) {
    const uint32_t words[4] = {""" + row + """};
    for (int i=0;i<4;++i) CHECK(azm_pcrel_kind(words[i]) == 0);
    CHECK(azm_patch_ok(words,1) == 1);
    CHECK(azm_patch_ok(words,4) == 1);
    for (int i=0;i<4;++i) {
        uint32_t changed[4];
        for (int j=0;j<4;++j) changed[j]=words[j];
        changed[i]=0x36080064u;
        CHECK(azm_patch_ok(changed,1)==0);
        CHECK(azm_patch_ok(changed,4)==0);
    }
    uint32_t ended[4]={0xd65f03c0u,0xd503201fu,0xd503201fu,0xd503201fu};
    CHECK(azm_patch_ok(ended,1)==1);
    CHECK(azm_patch_ok(ended,4)==0);
    const uintptr_t addr=0x1b01cf8u;
    g_hooked[0].addr=0x1b10860u; CHECK(!conflict(addr));
    g_hooked[0].addr=addr;
    CHECK(conflict(addr)); CHECK(conflict(addr-15)); CHECK(conflict(addr+15));
    CHECK(!conflict(addr-16)); CHECK(!conflict(addr+16));
    printf("%d\\n",checks);
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='az-input-route-policy-') as tmp:
    c, binary = Path(tmp)/'policy.c', Path(tmp)/'policy'
    c.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(c), '-o', str(binary)], check=True)
    checks = int(subprocess.check_output([str(binary)], text=True).strip())
assert checks == 22
literal_collisions = []
for path, data in sources.items():
    for match in re.finditer(rb'\b0[xX]([0-9a-fA-F]+)[uUlL]*\b', data):
        value = int(match.group(1), 16)
        for candidate in CANDIDATES:
            if candidate < value+16 and value < candidate+16:
                literal_collisions.append([path, hex(value), hex(candidate)])
assert literal_collisions == []
result = {
    'firmware_sha256': FIRMWARE, 'upstream_commit': PIN,
    'source_sha256': {n: digest(sources['src/azmod/'+n]) for n in
                      ['inline_hooks.c','community.c','ep147addr.h']},
    'actual_policy_extraction_sha256': digest(policy.encode()),
    'actual_overlap_predicate_sha256': digest(overlap.encode()),
    'host_policy_assertions': checks, 'instruction_anchors_checked': len(anchors),
    'data_word_anchors_checked': len(words), 'rtti_anchors_checked': len(names),
    'semantic_negative_controls': len(negative_anchors)+1,
    'functions': {hex(a): {'fde_extent':hex(n),'body_sha256':digest(read(a,n))}
                  for a,n in FUNCTIONS.items()},
    'recommended_passive_boundary': {
        'address': '0x1b01cf8', 'fde_extent': '0x2e8',
        'class': 'HuiAdaptedViewDispatcher<DefaultGuiTaskBoxConf>',
        'also_used_by_derived_class': 'gui::ViewTransitionManager',
        'observer_vptr': '0x2d453d0', 'derived_observer_vptr': '0x2d45740',
        'observer_slot': '0x18', 'primary_this_adjustment': '-0x118',
        'first_four': [ins[a] for a in range(0x1b01cf8,0x1b01d08,4)],
        'prologue_sha256': digest(read(0x1b01cf8,16)),
        'pcrel_kinds': [0,0,0,0],
        'actual_patch_ok_one_word': True, 'actual_patch_ok_four_words': True,
        'capture': ['x0 opaque observer','x1 opaque encoder reference',
                    'x2 opaque full-width ID','w3 signed delta bits',
                    'w4 cumulative counter bits','original LR'],
        'native_pointer_reads_required_for_initial_observer': False,
        'return_type_interpretation_proven': False,
    },
    'native_notification': {'entry':'0x2360f80','rotate_slot':'0x18',
        'record_delta_offset':'0x0','record_cumulative_offset':'0x4',
        'encoder_input_ingestion':'0x235c270','accumulator_offset':'0xd8',
        'zero_delta_skips_rotation_callback':True,
        'record_live_lifetime_for_external_reader_proven':False},
    'queued_task': {'vptr':'0x2d444d8','executor':'0x1b029c0',
        'capture':{'0x18':'primary dispatcher','0x20':'ID','0x28':'w3 delta',
                   '0x2c':'w4 cumulative'},
        'dispatch_order':['dispatcher+0x1a0 child','dispatcher+0x150 child','dispatcher'],
        'target_slot':'0x198','child_low_byte_nonzero_stops_chain':True,
        'bypasses_generic_wrapper_entries':['0x1adea38','0x1adeee0'],
        'dispatcher_field_0x19c_equals_helper_return_before_dispatch':True,
        'generation_semantics_proven':False,
        'cancellation_or_live_lifetime_contract_proven':False},
    'pinned_source_literal_audit':{'source_file_count':len(sources),
        'collisions':literal_collisions,'runtime_ownership_proven':False},
    'policy_negative_controls': ['TBZ in every first-four position refused in both modes',
        'RET permits one-word but refuses four-word overwrite',
        'same/plus-minus15 overlap refused; plus-minus16 disjoint'],
    'limits': [
        'Static alternative route does not explain previous zero-entry runtime result',
        'Live rotarySelector registration recipient and active dispatcher/child identity are unproven',
        'HUI delta and cumulative fields are not proof of physical byte-34 counter ordering',
        'Actual claim requires runtime ownership and reachable island checks',
        'No new hook, runtime observation, emulator contact or physical access occurred',
        'Entry observation would prove neither queued execution nor accepted/completed handling',
        'Raw SDK input-stage evidence is independently needed to distinguish mutation or eating before native routing',
    ],
}
print(json.dumps(result, indent=2, sort_keys=True))

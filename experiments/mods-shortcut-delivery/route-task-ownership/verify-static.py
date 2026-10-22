#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact-ELF task capture/execution audit; runs only host policy fixtures.

No native function execution, live pointer read, hook claim, emulator or network.
Output contains selected semantic facts and hashes, never firmware body bytes.
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

FIRMWARE = '6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
PIN = '3e0c5cb010454228f4ee63226b60d060494eeaae'
FUNCTIONS = {0x1b01cf8:0x2e8, 0x1b029c0:0x104, 0x1b02858:0x60,
             0x1aff780:0xcc, 0x1aff270:0xdc, 0x1afeb68:0x3c,
             0x1afe1f0:0x3a4, 0x1b038a0:0x1bc, 0x6f2398:0x94,
             0x1add3a0:0x8, 0x237a440:0x38, 0x237eac0:0x1e0}
CANDIDATES = (0x1b029c0, 0x237a440, 0x237eac0)

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--elf', type=Path, required=True)
ap.add_argument('--upstream', type=Path, required=True)
ap.add_argument('--cc', default='cc')
a = ap.parse_args()
sha = lambda data: hashlib.sha256(data).hexdigest()
raw = a.elf.read_bytes()
assert sha(raw) == FIRMWARE, 'different firmware; no fallback'
elf = ELFFile(io.BytesIO(raw))
assert elf.elfclass == 64 and elf.little_endian
assert elf['e_machine'] == 'EM_AARCH64' and elf['e_type'] == 'ET_EXEC'
segments = list(elf.iter_segments())

def read(addr, size):
    matches = [s for s in segments if s['p_type'] == 'PT_LOAD'
               and s['p_vaddr'] <= addr
               and addr + size <= s['p_vaddr'] + s['p_filesz']]
    assert len(matches) == 1, 'not uniquely file-backed'
    s = matches[0]; off = s['p_offset'] + addr - s['p_vaddr']
    return raw[off:off+size]

def word(addr):
    return struct.unpack('<Q', read(addr, 8))[0]

fde_objects = {f['initial_location']:f for f in elf.get_dwarf_info().EH_CFI_entries()
               if isinstance(f, FDE)}
fdes = {addr:f['address_range'] for addr,f in fde_objects.items()}
md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
ins = {}
for start, size in FUNCTIONS.items():
    assert fdes[start] == size, 'unwind extent mismatch'
    assert any(s['p_type'] == 'PT_LOAD' and s['p_flags'] & 1
               and s['p_vaddr'] <= start
               and start+size <= s['p_vaddr']+s['p_filesz'] for s in segments)
    decoded = list(md.disasm(read(start, size), start))
    assert sum(i.size for i in decoded) == size
    ins.update({i.address:(i.mnemonic,i.op_str) for i in decoded})

# Narrow semantic anchors selected after offline full-function review.
ANCHORS = {
  0x1b01d08:('mov','w21, w4'),
  0x1b01d10:('mov','w24, w3'),
  0x1b01d18:('mov','x26, x2'),
  0x1b01d1c:('sub','x25, x0, #0x118'),
  0x1b01d20:('bl','#0x1b00e10'),
  0x1b01d24:('mov','x20, x0'),
  0x1b01d28:('ldr','x0, [x0]'),
  0x1b01d2c:('ldr','x23, [x0, #0x30]'),
  0x1b01d64:('mov','x2, #8'),
  0x1b01d68:('mov','x1, #0x38'),
  0x1b01d70:('ldr','x3, [x20, #8]'),
  0x1b01d74:('mov','x0, x3'),
  0x1b01d78:('ldr','x3, [x3]'),
  0x1b01d7c:('ldr','x3, [x3, #0x10]'),
  0x1b01d80:('blr','x3'),
  0x1b01d84:('mov','x19, x0'),
  0x1b01d88:('cbz','x0, #0x1b01ec8'),
  0x1b01d8c:('add','x0, x20, #0x20'),
  0x1b01d94:('ldaxr','x27, [x0]'),
  0x1b01d98:('add','x27, x27, #1'),
  0x1b01d9c:('stlxr','w1, x27, [x0]'),
  0x1b01da0:('cbnz','w1, #0x1b01d94'),
  0x1b01da4:('bl','#0x7305f8'),
  0x1b01dac:('adrp','x1, #0x2d44000'),
  0x1b01db4:('add','x1, x1, #0x4d8'),
  0x1b01db8:('str','x1, [x19]'),
  0x1b01dbc:('str','w0, [x19, #8]'),
  0x1b01dc0:('stp','x27, x25, [x19, #0x10]'),
  0x1b01dc4:('str','x26, [x19, #0x20]'),
  0x1b01dc8:('stp','w24, w21, [x19, #0x28]'),
  0x1b01dd8:('add','x21, x21, #0x588'),
  0x1b01ddc:('ldaxr','w0, [x21]'),
  0x1b01de0:('add','w0, w0, #1'),
  0x1b01de4:('stlxr','w1, w0, [x21]'),
  0x1b01df8:('mov','x1, x19'),
  0x1b01dfc:('mov','x0, x20'),
  0x1b01e00:('blr','x23'),
  0x1b01e04:('cbz','x0, #0x1b01e58'),
  0x1b01e08:('ldr','x0, [x20, #0x18]'),
  0x1b01e3c:('ldr','x1, [x20, #0x18]'),
  0x1b01e48:('ldr','x1, [x1, #0x10]'),
  0x1b01e4c:('blr','x1'),
  0x1b01f5c:('b','#0x1b01df0'),
  0x1b029c0:('ldr','x3, [x0, #0x18]'),
  0x1b029c4:('adrp','x1, #0x28fa000'),
  0x1b029c8:('add','x1, x1, #0xb4'),
  0x1b029cc:('add','x2, x3, #0x19c'),
  0x1b029d0:('cmp','x2, x1'),
  0x1b029d4:('b.lo','#0x1b02a18'),
  0x1b029d8:('tst','x2, #3'),
  0x1b029dc:('b.ne','#0x1b02a18'),
  0x1b029e0:('stp','x29, x30, [sp, #-0x30]!'),
  0x1b029ec:('ldr','w21, [x3, #0x19c]'),
  0x1b029f4:('add','x20, x0, #0x18'),
  0x1b029f8:('mov','x19, x0'),
  0x1b029fc:('bl','#0x1b02858'),
  0x1b02a00:('cmp','w21, w0'),
  0x1b02a04:('b.eq','#0x1b02a1c'),
  0x1b02a14:('ret',''),
  0x1b02a18:('ret',''),
  0x1b02a24:('ldp','w2, w3, [x20, #0x10]'),
  0x1b02a28:('add','x21, x21, #0x3a0'),
  0x1b02a2c:('ldr','x0, [x4, #0x1a0]'),
  0x1b02a30:('cbz','x0, #0x1b02a4c'),
  0x1b02a40:('ldr','x5, [x1, #0x198]'),
  0x1b02a44:('cmp','x5, x21'),
  0x1b02a48:('b.ne','#0x1b02a8c'),
  0x1b02a4c:('ldr','x0, [x4, #0x150]'),
  0x1b02a50:('cbz','x0, #0x1b02a64'),
  0x1b02a58:('ldr','x5, [x1, #0x198]'),
  0x1b02a60:('b.ne','#0x1b02aa8'),
  0x1b02a68:('ldr','x5, [x0, #0x198]'),
  0x1b02a70:('b.eq','#0x1b02a08'),
  0x1b02a74:('ldr','x1, [x20, #8]'),
  0x1b02a78:('mov','x0, x4'),
  0x1b02a84:('ldp','x29, x30, [sp], #0x30'),
  0x1b02a88:('br','x5'),
  0x1b02a90:('blr','x5'),
  0x1b02a94:('tst','w0, #0xff'),
  0x1b02a98:('b.ne','#0x1b02a08'),
  0x1b02a9c:('ldp','w2, w3, [x20, #0x10]'),
  0x1b02aa0:('ldr','x4, [x19, #0x18]'),
  0x1b02aac:('blr','x5'),
  0x1b02ab0:('tst','w0, #0xff'),
  0x1b02ab4:('b.ne','#0x1b02a08'),
  0x1b02ab8:('ldp','w2, w3, [x20, #0x10]'),
  0x1b02abc:('ldr','x4, [x19, #0x18]'),
  0x1b02868:('add','x19, x19, #0x5f0'),
  0x1b0286c:('ldarb','w0, [x19]'),
  0x1b02874:('tbz','w0, #0, #0x1b02888'),
  0x1b02878:('ldr','w0, [x20, #0x5f8]'),
  0x1b02890:('bl','#0x427c50'),
  0x1b02898:('bl','#0x6f2398'),
  0x1b0289c:('str','w0, [x20, #0x5f8]'),
  0x1b028a4:('bl','#0x428270'),
  0x6f23b8:('add','x19, x19, #0x810'),
  0x6f23bc:('ldaxr','w0, [x19]'),
  0x6f23c0:('add','w0, w0, #1'),
  0x6f23c4:('stlxr','w1, w0, [x19]'),
  0x6f23c8:('cbnz','w1, #0x6f23bc'),
  0x1afe214:('bl','#0x1b02858'),
  0x1afe218:('str','w0, [x19, #0x19c]'),
  0x1afe4f8:('str','wzr, [x19, #0x19c]'),
  0x1b039b4:('str','wzr, [x19, #0x19c]'),
  0x1aff78c:('mov','x21, x0'),
  0x1aff7a0:('add','x20, x20, #0x580'),
  0x1aff7a4:('str','x0, [x21]'),
  0x1aff7b4:('add','x19, x19, #0x588'),
  0x1aff7b8:('ldaxr','w0, [x19]'),
  0x1aff7bc:('sub','w0, w0, #1'),
  0x1aff7c0:('stlxr','w1, w0, [x19]'),
  0x1aff7d0:('str','wzr, [x21, #8]'),
  0x1aff2a4:('add','x19, x19, #0x588'),
  0x1aff2ac:('sub','w0, w0, #1'),
  0x1aff2c0:('str','wzr, [x21, #8]'),
  0x1aff2c4:('mov','x0, x21'),
  0x1aff2c8:('mov','x1, #0x38'),
  0x1aff2d4:('b','#0x4280a0'),
  0x1afeb78:('add','x0, sp, #0x28'),
  0x1afeb80:('mov','x19, x8'),
  0x1afeb84:('bl','#0x24876f0'),
  0x1afeb88:('mov','x8, x19'),
  0x1afeb90:('bl','#0x236dce0'),
  0x1add3a0:('mov','w0, #0'),
  0x1add3a4:('ret',''),
  0x237a440:('stp','x29, x30, [sp, #-0x30]!'),
  0x237a444:('mov','x29, sp'),
  0x237a448:('str','x19, [sp, #0x10]'),
  0x237a44c:('mov','x19, x0'),
  0x237eac0:('stp','x29, x30, [sp, #-0x90]!'),
  0x237eac4:('mov','x29, sp'),
  0x237eac8:('stp','x19, x20, [sp, #0x10]'),
  0x237eacc:('mov','x20, x2'),
}
WORDS = {0x2d444c8:0, 0x2d444d0:0x2d44450,
         0x2d444d8:0x1aff780, 0x2d444e0:0x1aff270,
         0x2d444e8:0x1b029c0, 0x2d444f0:0x1afeb68,
         0x2d453e8:0x1b01cf8, 0x2d45758:0x1b01cf8}
TASK_NAME = ('N4meow16AsyncTaskBoxBase9AsyncTaskIZNS_24HuiAdaptedViewDispatcher'
             'INS_21DefaultGuiTaskBoxConfEE16didRotateEncoderERKNS_11IHuiEncoder'
             'ENS_9EasyID_ExILb0EEEiiEUlvE_EE')
assert read(word(0x2d44458), 240).split(b'\0',1)[0].decode() == TASK_NAME

def verify(actual, expected):
    assert all(actual.get(k) == v for k,v in expected.items()), 'anchor mismatch'

verify(ins, ANCHORS)
verify({p:word(p) for p in WORDS}, WORDS)
# Entry CFI starts before any frame allocation, then follows both guarded and tail paths.
CFI = {0x1b029c0:0, 0x1b029e4:48, 0x1b029ec:48, 0x1b029f4:48,
       0x1b02a14:0, 0x1b02a1c:48, 0x1b02a88:0, 0x1b02a8c:48}
rows = fde_objects[0x1b029c0].get_decoded().table
assert {r['pc']:r['cfa'].offset for r in rows} == CFI
assert all(r['cfa'].reg == 31 and r['cfa'].expr is None for r in rows)
for r in rows:
    if r['cfa'].offset:
        assert r[29].type == 'OFFSET' and r[29].arg == -48
        assert r[30].type == 'OFFSET' and r[30].arg == -40
# Task-specific destructors never read lambda fields through their preserved task x21.
# This is a bounded body check, not whole-program proof of absent lifetime management.
for start in (0x1aff780,0x1aff270):
    body = [ins[p] for p in range(start,start+FUNCTIONS[start],4)]
    assert not any(re.search(r'\[x21(?:,|\])',op) and mn.startswith('ld')
                   for mn,op in body)

controls = [
 ('capture-adjustment',0x1b01d1c,('sub','x25, x0, #0x120')),
 ('allocation-size',0x1b01d68,('mov','x1, #0x30')),
 ('taskno-is-64-bit',0x1b01d94,('ldaxr','w27, [x0]')),
 ('dispatcher-capture',0x1b01dc0,('stp','x25, x27, [x19, #0x10]')),
 ('full-id-capture',0x1b01dc4,('str','w26, [x19, #0x20]')),
 ('delta-cumulative-order',0x1b01dc8,('stp','w21, w24, [x19, #0x28]')),
 ('submission-slot',0x1b01d2c,('ldr','x23, [x0, #0x28]')),
 ('entry-first-read',0x1b029c0,('ldr','x3, [x0, #0x20]')),
 ('executor-lower-bound',0x1b029d4,('b.hs','#0x1b02a18')),
 ('guard-equality',0x1b02a04,('b.ne','#0x1b02a1c')),
 ('guard-is-static',0x1b02878,('ldr','w0, [x0, #0x19c]')),
 ('constructor-sentinel',0x1afe218,('str','w0, [x19, #0x198]')),
 ('destructor-clear',0x1b039b4,('str','w0, [x19, #0x19c]')),
 ('first-child-offset',0x1b02a2c,('ldr','x0, [x4, #0x150]')),
 ('native-slot',0x1b02a40,('ldr','x5, [x1, #0x1b0]')),
 ('child-low-byte-only',0x1b02a94,('cmp','x0, #0')),
 ('final-tail-return',0x1b02a88,('blr','x5')),
 ('destructor-global-not-capture',0x1aff7b4,('add','x19, x21, #0x18')),
]
for name,addr,wrong in controls:
    changed = dict(ANCHORS); changed[addr] = wrong
    try: verify(ins, changed)
    except AssertionError: pass
    else: raise AssertionError('accepted wrong '+name)
for name,actual,expected in [
    ('wrong-executor-vslot',word(0x2d444e8),0x1afeb68),
    ('mid-function-as-entry',fdes.get(0x1b029c4),0x100),
    ('wrong-executor-extent',fdes[0x1b029c0],0x100),
    ('frame-before-guard',CFI[0x1b029c0],48),
]:
    try: assert actual == expected
    except AssertionError: pass
    else: raise AssertionError('accepted wrong '+name)

archive = subprocess.check_output(['git','archive',PIN,'src/azmod'],cwd=a.upstream)
with tarfile.open(fileobj=io.BytesIO(archive)) as tf:
    sources = {m.name:tf.extractfile(m).read() for m in tf.getmembers()
               if m.isfile() and m.name.endswith(('.c','.h'))}
h = sources['src/azmod/inline_hooks.c'].decode()
cm = sources['src/azmod/community.c'].decode()
def function(name, source=h):
    start=source.index('static int '+name+'('); b=source.index('{',start)
    depth,end=1,b+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
policy = '\n\n'.join(function(n) for n in ('azm_pcrel_kind','azm_ends_fn','azm_patch_ok'))
install = function('azm_install_hook_at')
assert install.index('if (!azm_patch_ok(func, 1))') < install.index('azm_island_slot(FUNC)')
assert 'if (!isl && __atomic_load_n(&g_azm_late, __ATOMIC_ACQUIRE))' in install
assert 'if (azm_pcrel_kind(in) == 2)' in install
assert 'azm_emit_abs_addr(&tramp[out]' in install
claim = function('cm_claim_hook',cm)
overlap = 'addr < g_hooked[i].addr + 16 && g_hooked[i].addr < addr + 16'
assert overlap in claim and claim.index(overlap) < claim.index('azm_install_hook(addr, hook, orig)')
assert 'addr < g_cm_text_lo || addr >= g_cm_text_hi' in claim
assert '!g_sdk_in_init || (c - g_cm) != g_cm_loading' in claim
prologues = [struct.unpack('<4I',read(p,16)) for p in CANDIDATES]
array = ',\n'.join('{'+','.join(hex(w)+'u' for w in row)+'}' for row in prologues)
addresses = ','.join(hex(p)+'u' for p in CANDIDATES)
fixture = '#include <stdint.h>\n#include <stdio.h>\n'+policy+'''
static struct { uintptr_t addr; } g_hooked[1];
static int conflict(uintptr_t addr) { int i=0; return '''+overlap+'''; }
static int checks;
#define CHECK(v) do { ++checks; if (!(v)) return checks; } while(0)
int main(void) {
    const uint32_t original[3][4]={'''+array+'''};
    const uintptr_t addresses[3]={'''+addresses+'''};
    for (int c=0;c<3;++c) {
        for (int i=0;i<4;++i) CHECK(azm_pcrel_kind(original[c][i])==((c==0 && i==1)?2:0));
        CHECK(azm_patch_ok(original[c],1)==1); CHECK(azm_patch_ok(original[c],4)==1);
        for (int i=0;i<4;++i) {
            uint32_t w[4]; for (int j=0;j<4;++j) w[j]=original[c][j];
            w[i]=0x36080064u;
            CHECK(!azm_patch_ok(w,1)); CHECK(!azm_patch_ok(w,4));
        }
        g_hooked[0].addr=addresses[c];
        CHECK(conflict(addresses[c])); CHECK(conflict(addresses[c]-15));
        CHECK(conflict(addresses[c]+15)); CHECK(!conflict(addresses[c]-16));
        CHECK(!conflict(addresses[c]+16));
    }
    uint32_t adr[4]; for (int i=0;i<4;++i) adr[i]=original[0][i];
    adr[1]=0x10000001u;
    CHECK(!azm_patch_ok(adr,1)); CHECK(!azm_patch_ok(adr,4));
    uint32_t ended[4]={0xd65f03c0u,0xd503201fu,0xd503201fu,0xd503201fu};
    CHECK(azm_patch_ok(ended,1)); CHECK(!azm_patch_ok(ended,4));
    printf("%d\\n",checks); return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='az-route-task-policy-') as tmp:
    c=Path(tmp)/'policy.c'; binary=Path(tmp)/'policy';c.write_text(fixture)
    subprocess.run([a.cc,'-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(binary)],check=True)
    checks=int(subprocess.check_output([str(binary)],text=True).strip())
assert checks == 61
collisions=[]
for path,data in sources.items():
    for match in re.finditer(rb'\b0[xX]([0-9a-fA-F]+)[uUlL]*\b',data):
        v=int(match.group(1),16)
        for p in CANDIDATES:
            if p < v+16 and v < p+16:collisions.append([path,hex(v),hex(p)])
assert collisions == [], 'pinned literal overlaps; no automatic fallback'

result = {
 'firmware_sha256':FIRMWARE, 'upstream_commit':PIN,
 'instruction_anchors_checked':len(ANCHORS), 'data_word_anchors_checked':len(WORDS),
 'rtti_anchors_checked':1, 'semantic_rejection_controls':len(controls)+4,
 'host_actual_policy_assertions':checks, 'executor_cfi_rows_checked':len(rows),
 'functions':{hex(p):{'fde_extent':hex(n),'body_sha256':sha(read(p,n))} for p,n in FUNCTIONS.items()},
 'source_sha256':{n:sha(sources['src/azmod/'+n]) for n in ('inline_hooks.c','community.c','ep147addr.h')},
 'actual_policy_extraction_sha256':sha(policy.encode()),
 'actual_overlap_predicate_sha256':sha(overlap.encode()),
 'pinned_literal_audit':{'files':len(sources),'collisions':collisions,'runtime_ownership_proven':False},
 'candidate_policy':{hex(p):{'first_four':[ins[p+i*4] for i in range(4)],
      'prologue_sha256':sha(read(p,16)), 'pcrel_kinds':[0,2,0,0] if p==0x1b029c0 else [0,0,0,0],
      'patch_ok_1':True,'patch_ok_4':True,'actual_claim_attempted':False}
      for p in CANDIDATES},
 'task':{'vptr':'0x2d444d8','allocation_bytes':56,'allocation_alignment':8,
   'fields':{'0x8':'32-bit helper result, cleared by task destructor',
     '0x10':'64-bit incremented task-box counter; TaskNo interpretation in TASKBOX.md',
     '0x18':'raw adjusted dispatcher pointer (entry x0 minus 0x118)',
     '0x20':'copied full-width entry x2', '0x28':'copied entry w3','0x2c':'copied entry w4'},
   'direct_construction_without_copy_call_in_reviewed_success_path':True,
   'capture_fields_read_by_reviewed_task_destructors':False,
   'task_destructor_specialization_counter':'0x3fea588',
   'dispatcher_retention_by_these_capture_destructor_paths_proven':False},
 'executor':{'entry':'0x1b029c0','known_incoming_argument':'x0 task pointer',
   'first_native_read':'task+0x18 before address guards',
   'guard_order':['dispatcher+0x19c address >= 0x28fa0b4 (unsigned)',
      '(dispatcher+0x19c address)&3 == 0','load dispatcher+0x19c equals cached helper result'],
   'task_pointer_mapping_size_or_lifetime_check':False,
   'child_order':['dispatcher+0x1a0','dispatcher+0x150','dispatcher'],
   'handler_slot':'0x198','handler_arguments':['x0 target','x1 copied ID','w2 copied delta','w3 copied cumulative'],
   'default_handler':'0x1add3a0 (returns w0 zero)',
   'child_result_test':'low byte nonzero stops fallback',
   'final_dispatch':'tail br x5 after restoring incoming LR/frame',
   'unified_return_or_completion_meaning_proven':False,
   'cfi':{hex(p):{'cfa_register':31,'cfa_offset':off} for p,off in CFI.items()}},
 'guard':{'helper':'0x1b02858','static_guard':'0x3fea5f0','cached_value':'0x3fea5f8',
   'initial_value_function':'0x6f2398','constructor_store':'0x1afe218',
   'destructor_zero':'0x1b039b4','constructor_failure_zero':'0x1afe4f8',
   'per_task_epoch_proven':False,'retained_storage_or_cancellation_fence_proven':False},
 'limits':['Static observations only; no new runtime/hook/ABI-wrapper test',
   'Native owner thread, active dispatcher/children and live task mappings remain unobserved',
   'Allocation/queue ownership and dispatcher lifetime are separate; see TASKBOX.md',
   'Task-specific destructor evidence does not exclude outside lifetime rules',
   'Numeric-literal audit is not installed-hook ownership or reachable-island evidence',
   'Successful prologue policy does not establish full claim eligibility',
   'No submitted return value is a child-execution or completion observation',
   'Do not retain native pointers or read task/dispatcher fields after native return'],
}
print(json.dumps(result,indent=2,sort_keys=True))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify immutable firmware and the pinned installer's refusal, without native calls.

The executed C is extracted instruction policy plus the vslot segment validator
with host phdr fixtures. No native function, image write or hook is executed.
"""
import argparse, hashlib, io, json, struct, subprocess, tempfile
from pathlib import Path
from elftools.elf.elffile import ELFFile
ap=argparse.ArgumentParser();ap.add_argument('--elf',type=Path,required=True);ap.add_argument('--upstream',type=Path,required=True);ap.add_argument('--cc',default='cc')
a=ap.parse_args();pin='3e0c5cb010454228f4ee63226b60d060494eeaae'
def digest(b):return hashlib.sha256(b).hexdigest()
raw=a.elf.read_bytes();assert digest(raw)=='6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
elf=ELFFile(io.BytesIO(raw))
loads=[{'start':s['p_vaddr'],'memory_end':s['p_vaddr']+s['p_memsz'],
        'file_end':s['p_vaddr']+s['p_filesz'],'flags':s['p_flags']}
       for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
slot_segment=next(s for s in loads if s['start']<=0x2d61c78 and 0x2d61c80<=s['memory_end'])
assert elf['e_type']=='ET_EXEC' and slot_segment['flags']==5
assert slot_segment['start']==0x400000 and slot_segment['memory_end']==0x3f5f714
assert not any(s['start']<=0x2d61c78 and 0x2d61c80<=s['memory_end']
               for s in loads if not s['flags'] & 1)
def read(addr,n):
 for s in elf.iter_segments():
  if s['p_type']=='PT_LOAD' and s['p_vaddr']<=addr and addr+n<=s['p_vaddr']+s['p_filesz']:
   off=s['p_offset']+addr-s['p_vaddr'];return raw[off:off+n]
 raise AssertionError('not file-backed')
prologue=struct.unpack('<4I',read(0x1b3ab60,16))
assert prologue==(0xf9421c04,0x39430084,0x36080064,0x52800000)
assert struct.unpack('<Q',read(0x2d61ae0+0x198,8))[0]==0x1b3ab60
files={n:subprocess.check_output(['git','show',pin+':src/azmod/'+n],cwd=a.upstream) for n in ['inline_hooks.c','community.c','sdk_vslot.c']}
h=files['inline_hooks.c'].decode();cm=files['community.c'].decode()
vs=files['sdk_vslot.c'].decode()
def function(name,source=h):
 start=source.index('static int '+name+'(');brace=source.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
policy='\n\n'.join(function(n) for n in ['azm_pcrel_kind','azm_ends_fn','azm_patch_ok'])
gate=function('azm_patch_ok')
assert 'for (int i = 0; i < 4; i++)\n        if (azm_pcrel_kind(f[i]) == 1)\n            return 0;' in gate
install=function('azm_install_hook_at')
precheck='if (!azm_patch_ok(func, 1))\n        return azm_rec(FUNC, name, AZM_DECLINED);'
assert precheck in install
assert install.index(precheck)<install.index('uint32_t *isl = azm_island_slot(FUNC);')
assert install.index(precheck)<install.index('__atomic_store_n(orig, (void *) tramp, __ATOMIC_RELAXED);')
assert install.index(precheck)<install.index('__atomic_store_n(func, b, __ATOMIC_RELEASE);')

# Exercise the exact extracted predicates, including the one-word island gate.
# The NOP substitution is a fixture control; it never modifies the firmware.
fixture='''
#include <stdint.h>
#include <stdio.h>
'''+policy+'''
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) return checks; } while (0)
int main(void) {
    const uint32_t exact[4] = {'''+','.join(hex(v)+'u' for v in prologue)+'''};
    const int kinds[4] = {0, 0, 1, 0};
    for (int i = 0; i < 4; ++i) CHECK(azm_pcrel_kind(exact[i]) == kinds[i]);
    CHECK(azm_patch_ok(exact, 1) == 0);
    CHECK(azm_patch_ok(exact, 4) == 0);
    uint32_t control[4] = {exact[0], exact[1], 0xd503201fu, exact[3]};
    CHECK(azm_patch_ok(control, 1) == 1);
    CHECK(azm_patch_ok(control, 4) == 1);
    for (int pos = 0; pos < 4; ++pos) {
        uint32_t words[4] = {0xd503201fu, 0xd503201fu, 0xd503201fu, 0xd503201fu};
        words[pos] = exact[2];
        CHECK(azm_patch_ok(words, 1) == 0);
        CHECK(azm_patch_ok(words, 4) == 0);
    }
    control[0] = 0xd65f03c0u;
    CHECK(azm_patch_ok(control, 1) == 1);
    CHECK(azm_patch_ok(control, 4) == 0);
    printf("%d\\n", checks);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='az-knob-policy-') as tmp:
 source=Path(tmp)/'policy.c';binary=Path(tmp)/'policy'
 source.write_text(fixture)
 subprocess.run([a.cc,'-std=c11','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
 checks=int(subprocess.check_output([str(binary)],text=True).strip())
assert checks==18

# SDK claim_vslot is narrower than the core's raw vtable primitive: it rejects
# every executable PT_LOAD, even when a valid .rodata vtable shares that segment.
vs_cb=function('sdk_ep_data_cb',vs);vs_contains=function('sdk_in_ep_data',vs)
assert 'if (ph->p_type != PT_LOAD || (ph->p_flags & PF_X))\n            continue;' in vs_cb
assert 'if (info->dlpi_name && info->dlpi_name[0])\n        return 0;' in vs_cb
vs_claim=function('sdk_claim_vslot',vs)
assert 'else if (!sdk_in_ep_data(slot))\n        why = "not inside EP147\'s data";' in vs_claim
assert vs_claim.index('if (why) {')<vs_claim.index('azm_patch_vtable(slot, fn, orig)')
vs_decl=vs[vs.index('static struct {',vs.index("/* EP147's non-executable")):vs.index('static int sdk_ep_data_cb(')]
vs_policy=vs_decl+vs_cb+'\n'+vs_contains
phdrs=','.join('{1u,%du,0x%xu,0x%xu}' % (s['flags'],s['start'],s['memory_end']-s['start']) for s in loads)
vs_fixture='''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define PT_LOAD 1u
#define PF_X 1u
typedef struct { unsigned p_type, p_flags; uintptr_t p_vaddr, p_memsz; } HostPhdr;
#define ElfW(type) HostPhdr
struct dl_phdr_info { uintptr_t dlpi_addr; const char *dlpi_name; const HostPhdr *dlpi_phdr; uint16_t dlpi_phnum; };
static int dl_iterate_phdr(int (*fn)(struct dl_phdr_info *,size_t,void *),void *data);
'''+vs_policy+'''
static HostPhdr phdrs[] = {'''+phdrs+'''};
static struct dl_phdr_info info = {0, "", phdrs, sizeof phdrs/sizeof phdrs[0]};
static int dl_iterate_phdr(int (*fn)(struct dl_phdr_info *,size_t,void *),void *data) {
    return fn(&info, sizeof info, data);
}
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) return checks; } while (0)
int main(void) {
    CHECK(sdk_in_ep_data(0x2d61c78u) == 0);
    CHECK(sdk_in_ep_data(0x1b3ab60u) == 0);
    CHECK(g_sdk_ep_data_n == 1);
    CHECK(sdk_in_ep_data(g_sdk_ep_data[0].lo) == 1);
    CHECK(sdk_in_ep_data(g_sdk_ep_data[0].hi - sizeof(void *)) == 1);
    CHECK(sdk_in_ep_data(g_sdk_ep_data[0].hi - sizeof(void *) + 1) == 0);
    CHECK(sdk_in_ep_data(g_sdk_ep_data[0].lo - 1) == 0);
    info.dlpi_name = "fixture-library.so";
    CHECK(sdk_ep_data_cb(&info, sizeof info, NULL) == 0);
    CHECK(g_sdk_ep_data_n == 1);
    info.dlpi_name = "";
    /* Discriminating fixture control only: no ELF/program-header mutation. */
    phdrs[0].p_flags &= ~PF_X;
    g_sdk_ep_data_n = -1;
    CHECK(sdk_in_ep_data(0x2d61c78u) == 1);
    phdrs[0].p_flags |= PF_X;
    g_sdk_ep_data_n = -1;
    CHECK(sdk_in_ep_data(0x2d61c78u) == 0);
    printf("%d\\n", checks);
    return 0;
}
'''
assert len(loads)==2 and loads[0] is slot_segment
with tempfile.TemporaryDirectory(prefix='az-knob-vslot-policy-') as tmp:
 source=Path(tmp)/'policy.c';binary=Path(tmp)/'policy'
 source.write_text(vs_fixture)
 subprocess.run([a.cc,'-std=c11','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
 vslot_checks=int(subprocess.check_output([str(binary)],text=True).strip())
assert vslot_checks==11
assert 'int ncopy = isl ? 1 : 4' in h
assert 'if (!isl && __atomic_load_n(&g_azm_late, __ATOMIC_ACQUIRE))' in h
assert 'return azm_rec(FUNC, name, AZM_REFUSED_LATE);' in h
assert h.index('__atomic_store_n(orig, (void *) tramp, __ATOMIC_RELAXED);') < h.index('__atomic_store_n(func, b, __ATOMIC_RELEASE);')
assert 'int rc = azm_install_hook(addr, hook, orig);' in cm
assert 'if (!g_sdk_in_init || (c - g_cm) != g_cm_loading)' in cm
assert 'addr < g_hooked[i].addr + 16' in cm
print(json.dumps({'upstream_commit':pin,'firmware_sha256':digest(raw),
 'source_sha256':{n:digest(v) for n,v in files.items()},
 'entry':'0x1b3ab60','vtable_slot':'0x2d61c78','prologue_matches':True,
 'policy_extraction_sha256':digest(policy.encode()),
 'policy_host_assertions':checks,
 'prologue_pcrel_kinds':[0,0,1,0],
 'first_instruction_is_position_independent_load':True,
 'policy_scans_first_four_instructions_for_both_patch_widths':True,
 'tbz_at_entry_plus_8_declines_both_patch_widths':True,
 'decline_precedes_island_lookup_and_code_writes':True,
 'expected_install_result':'AZM_DECLINED (-1)',
 'function_entry_hook_eligible':False,
 'late_island_path_would_copy_one_instruction_only_after_policy_acceptance':True,
 'late_no_island_refuses':True,'original_published_before_atomic_redirect':True,
 'vslot_policy_extraction_sha256':digest(vs_policy.encode()),
 'vslot_segment_policy_host_assertions':vslot_checks,
 'slot_pt_load':{k:(hex(v) if k!='flags' else v) for k,v in slot_segment.items()},
 'slot_pt_load_flags_description':'PF_R | PF_X',
 'slot_in_non_executable_pt_load':False,
 'sdk_claim_vslot_eligible':False,
 'sdk_claim_vslot_expected_refusal':"not inside EP147's data",
 'vslot_control_without_fixture_pf_x_accepts':True,
 'vslot_control_restore_fixture_pf_x_refuses':True,
 'vslot_refusal_precedes_patch_primitive':True,
 'vslot_runtime_claim_attempted':False,
 'runtime_install_success_proven':False,'caller_and_return_semantics_proven':False},indent=2))

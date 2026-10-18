#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only exact-firmware and pinned late-hook checks. No firmware dump."""
import argparse, hashlib, io, json, struct, subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile
ap=argparse.ArgumentParser();ap.add_argument('--elf',type=Path,required=True);ap.add_argument('--upstream',type=Path,required=True)
a=ap.parse_args();pin='3e0c5cb010454228f4ee63226b60d060494eeaae'
def digest(b):return hashlib.sha256(b).hexdigest()
raw=a.elf.read_bytes();assert digest(raw)=='6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
elf=ELFFile(io.BytesIO(raw))
def read(addr,n):
 for s in elf.iter_segments():
  if s['p_type']=='PT_LOAD' and s['p_vaddr']<=addr and addr+n<=s['p_vaddr']+s['p_filesz']:
   off=s['p_offset']+addr-s['p_vaddr'];return raw[off:off+n]
 raise AssertionError('not file-backed')
assert struct.unpack('<4I',read(0x1b3ab60,16))==(0xf9421c04,0x39430084,0x36080064,0x52800000)
assert struct.unpack('<Q',read(0x2d61ae0+0x198,8))[0]==0x1b3ab60
files={n:subprocess.check_output(['git','show',pin+':src/azmod/'+n],cwd=a.upstream) for n in ['inline_hooks.c','community.c']}
h=files['inline_hooks.c'].decode();cm=files['community.c'].decode()
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
 'first_instruction_is_position_independent_load':True,
 'late_island_path_copies_one_instruction':True,
 'tbz_at_entry_plus_8_is_not_relocated_in_late_path':True,
 'late_no_island_refuses':True,'original_published_before_atomic_redirect':True,
 'runtime_install_success_proven':False,'caller_and_return_semantics_proven':False},indent=2))

#!/usr/bin/env python3
"""Check minimal exact-firmware activation-boundary anchors, without device access.
Only sanitized hashes/addresses/claims are emitted. Requires capstone/pyelftools.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
from elftools.elf.elffile import ELFFile
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('elf',type=Path);ap.add_argument('--output',type=Path);a=ap.parse_args()
b=a.elf.read_bytes();expected='6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
assert hashlib.sha256(b).hexdigest()==expected
with a.elf.open('rb') as f: segments=[dict(s.header) for s in ELFFile(f).iter_segments()]
def read(at,n):
 s=next(s for s in segments if s['p_type']=='PT_LOAD' and s['p_vaddr']<=at and at+n<=s['p_vaddr']+s['p_filesz'])
 off=s['p_offset']+at-s['p_vaddr'];return b[off:off+n]
def q(at):return struct.unpack('<Q',read(at,8))[0]
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
anchors={
 0x1b2d3cc:('add','x0, x19, #0x348'),
 0x1b2d3f8:('bl','#0x1ae9380'),
 0x1b2de00:('add','x22, x19, #0x1b8'),
 0x1b2de98:('str','x22, [x3]'),
 0x1ae8cb0:('bl','#0x249b0f0'),
 0x1ae8cb8:('bl','#0x2503090'),
 0x1ae8cc0:('ldr','x0, [x0, #0x10]'),
 0x1ae8cc8:('b.ne','#0x1ae8d40'),
 0x1ae8cd4:('b.eq','#0x1ae9070'),
 0x1ae9098:('ldr','x1, [x0, w20, sxtw #3]'),
 0x1ae909c:('mov','x0, x1'),
 0x1ae90a4:('ldr','x1, [x1, #0x18]'),
 0x1ae90a8:('blr','x1'),
 0x1ae90b4:('ldr','w0, [x19, #0x38]'),
 0x1b2e538:('ret',''),
 0x1b2cb30:('add','x4, x19, #0x1b8'),
 0x1b2cbd0:('add','x0, x19, #0x348'),
 0x1b2cbd4:('bl','#0x1ae8958'),
 0x1c32f64:('cmp','w20, w0'),
 0x1c32f68:('b.eq','#0x1c32fcc'),
 0x1c32f80:('bl','#0x2355a30'),
 0x1c32fd4:('bl','#0x1c34a50'),
 0x1c33028:('blr','x3'),
 0x1c3304c:('blr','x1'),
 0x1b29480:('str','w21, [x20, #0x1e8]'),
 0x1b294b0:('str','wzr, [x20, #0x1e8]'),
 0x1b29734:('ldr','w0, [x19, #0x1e8]'),
 0x1b29738:('cbnz','w0, #0x1b29750'),
 0x1b29740:('bl','#0x1c324e0'),
 0x1b29774:('bl','#0x1be8a20'),
 0x1be8a80:('ldr','w1, [x1, #0x14]'),
 0x1be8a88:('bl','#0x1be5798'),
 0x1be8a9c:('bl','#0x1be7e38'),
 0x1af1ec8:('bl','#0x1b2d248'),
 0x1af1ed8:('add','x0, x23, #0xd8'),
 0x1af1ef0:('str','x23, [x19, #0x90]'),
}
for at,want in anchors.items():
 ins=next(md.disasm(read(at,4),at));assert (ins.mnemonic,ins.op_str)==want,hex(at)
assert q(0x2d5b008)==0xfffffffffffffe48 and q(0x2d5b010)==0x2d5a3a0
assert q(0x2d5b030)==0x1b2e538
assert q(0x2d5a3a0+24+10*16)==0x2d59f90
assert q(0x2d5a3a0+32+10*16)==0x1b802
assert b'StateRefreshCycleTimer9IListenerE' in read(q(0x2d59f90+8),100)
# The no-op consumes no arguments and the shown native caller ignores its return.
assert read(0x1b2e538,4).hex()=='c0035fd6'
r={
 'firmware_sha256':expected,'firmware_tag':'ep147-9e9454e357389ece',
 'verified_anchor_count':len(anchors),
 'candidate':{'source_view_timer_listener_offset':'0x1b8','listener_vtable':'0x2d5b018','slot_address':'0x2d5b030','slot_offset':'0x18','original':'0x1b2e538','original_body':'RET only','caller':'0x1ae90a8','caller_uses_result':False,'entry_argument':'live native IListener subobject; parent=argument-0x1b8','new_hook_installed':False},
 'thread_routing':{'direct_callback_after_juce_thread_compare':True,'off_thread_path_queues_native_task':True,'runtime_tid_not_observed_for_this_slot':True},
 'lifetime':{'timer_embedded_in_view_at':'0x348','dispatcher_reads_timer_after_listener_return':True,'source_view_has_native_reference_owner_at_factory_offset':'0x90','complete_activation_retention_proven':False,'reference_counts_modified':False},
 'actual_activation':{'source_row_listener':'0x1c32f40','different_row_selects_then_previews':True,'same_row_publishes_then_changes_focus':True,'parent_select_mode0':'0x1c324e0','parent_select_mode1_normal_library':'0x1be8a20','final_library_method':'0x1be7e38','row_to_library_resolution':'0x1be5798','full_final_argument_contract_proven':False},
 'limits':['Read-only callback entry candidate only; activation adapter gated','No retained native pointer, async reference or reference-count mutation','Source-row focus is not browser-source activation','Timer caller lifetime must survive any new operation','Category/library choice and missing-media semantics require further proof']
}
s=json.dumps(r,indent=2)+'\n';print(s,end='')
if a.output:a.output.write_text(s)

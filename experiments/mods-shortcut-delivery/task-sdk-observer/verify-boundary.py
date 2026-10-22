#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the passive task SDK adapter to exact firmware and tested ABI sources.
No firmware execution, native call, hook claim, device contact or prior-suite rerun.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from elftools.dwarf.callframe import FDE
from elftools.elf.elffile import ELFFile

HERE=Path(__file__).resolve().parent
FIRMWARE='6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
REFERENCE_COMMIT='5b0f5f3ee1b92682c3a3db51fc97f58050187ff2'
FW_TAG='ep147-9e9454e357389ece'
ENTRIES={'submission':0x237a440,'execution':0x1b029c0}
SLOTS={'submission':0x321b138,'execution':0x2d444e8}
CALLERS={'submission':0x1b01e04,'execution':0x237d800}
PROLOGUES={'submission':(0xa9bd7bfd,0x910003fd,0xf9000bf3,0xaa0003f3),
           'execution':(0xf9400c03,0x90006fc1,0x9102d021,0x91067062)}
TASK_VPTR=0x2d444d8
FUNCTIONS={
 0x237a440:(0x38,'d5861b114788f1d459c3104bedfeaaf3fc0ce0be5e80aa38c7cd909bf77d2af0'),
 0x1b029c0:(0x104,'482b8337cf92d2d8e43baee7f0476d662653cba984cfe0f206c49b1bcce2fddb'),
 0x1b01cf8:(0x2e8,'e91f820c8b58b342d8d8fc95edb6e71fb4836f73c1b809da2ac0c73cfba8427a'),
 0x237d4f0:(0x75c,'c032eff2da78670d19e203ea05ab1a1cd8a4e09eb9a2c5f36fa800e89568e8f3'),
 0x1b00d20:(0xec,'fed976c453fdcc74df5bd1542cd630a42b599aaf9447bd3a189dee800f979052'),
}
BLOCKS={
 'timer_vptr':(0x1b00d90,[('adrp','x0, #0x321b000'),('add','x0, x0, #0x108'),('stp','x0, x21, [x19]')]),
 'submission_first_four':(0x237a440,[('stp','x29, x30, [sp, #-0x30]!'),('mov','x29, sp'),('str','x19, [sp, #0x10]'),('mov','x19, x0')]),
 'submission_forward':(0x237a450,[('mov','w0, #0'),('str','x1, [sp, #0x28]'),('bl','#0x237a140'),('ldr','x1, [sp, #0x28]'),('mov','x2, x1'),('add','x1, x0, #0x20'),('mov','x0, x19'),('ldr','x19, [sp, #0x10]'),('ldp','x29, x30, [sp], #0x30'),('b','#0x237eac0')]),
 'submission_selects_slot':(0x1b01d20,[('bl','#0x1b00e10'),('mov','x20, x0'),('ldr','x0, [x0]'),('ldr','x23, [x0, #0x30]')]),
 'task_vptr_page':(0x1b01dac,[('adrp','x1, #0x2d44000')]),
 'task_constructs_vptr_and_fields':(0x1b01db4,[('add','x1, x1, #0x4d8'),('str','x1, [x19]'),('str','w0, [x19, #8]'),('stp','x27, x25, [x19, #0x10]'),('str','x26, [x19, #0x20]'),('stp','w24, w21, [x19, #0x28]')]),
 'submission_caller':(0x1b01df8,[('mov','x1, x19'),('mov','x0, x20'),('blr','x23'),('cbz','x0, #0x1b01e58')]),
 'execution_first_four':(0x1b029c0,[('ldr','x3, [x0, #0x18]'),('adrp','x1, #0x28fa000'),('add','x1, x1, #0xb4'),('add','x2, x3, #0x19c')]),
 'execution_caller':(0x237d7e4,[('ldr','x1, [sp, #0xa0]'),('mov','x0, x1'),('ldr','x2, [x1]'),('ldr','x1, [x1, #0x10]'),('str','x1, [x28, #0xc0]'),('ldr','x1, [x2, #0x10]'),('blr','x1'),('ldr','w0, [sp, #0x7c]')]),
}
WORDS={0x321b100:0x321add8,0x321b138:0x237a440,0x2d444d0:0x2d44450,
       0x2d444d8:0x1aff780,0x2d444e8:0x1b029c0}
TASK_NAME=('N4meow16AsyncTaskBoxBase9AsyncTaskIZNS_24HuiAdaptedViewDispatcher'
           'INS_21DefaultGuiTaskBoxConfEE16didRotateEncoderERKNS_11IHuiEncoder'
           'ENS_9EasyID_ExILb0EEEiiEUlvE_EE')
REUSED={
 'abi/submission-entry.S':('task-observer-integration/submission-entry.S','16c7df29fef3c80fcd5f6259d4073abf92fd57471bc203c39f3853a0709d06c8'),
 'abi/executor-entry.S':('task-observer-integration/executor-entry.S','31aef2512c9b0ba35223e640eba0a962fd2fd493e7a9988afee51d387f58ea62'),
 'deps/task-capture-contract/capture.h':('task-capture-contract/capture.h','aa9b82311065fa3481cda29883f6dae40d1685c213d520271e1f4530039291ea'),
 'deps/task-value-transport/transport.h':('task-value-transport/transport.h','3f2a0b902a6fae0ab425be570bda3904ac5e99203415457aa7ced059d5061d55'),
 'deps/task-call-return-abi/context.h':('task-call-return-abi/context.h','c8ebf02fcaa1eb986ab59ee6516dfbaa8b9f6a6c13db211661c658e3cc6acc07'),
}
REFERENCES={
 'route-task-ownership/static-result.json':'836ee5ee84b9c6ef856ced03007167ce86a2176e6bd8fafdbb64a441aeb0abcf',
 'route-task-ownership/taskbox-result.json':'8fb7bce8fa2881a860365b0d8a90c00c2f5a8a276422048035968ab0d079cc46',
 'task-observer-integration/test-result.json':'79a7957cf9fd99296ac62d73b0758c0bba8bbe07c916c17c8fdd10d457b0f036',
 'task-observer-integration/independent-result.json':'bfc4d8391273684e3277ed3134c960304f741fabbcf6558663a0c1a2c97ea054',
}
def sha(data):return hashlib.sha256(data).hexdigest()
def require(ok,label):
 if not ok:raise AssertionError(label)
def compact(source):return re.sub(r'\s+','',re.sub(r'/\*.*?\*/|//[^\n]*','',source,flags=re.S))
def body(source,marker):
 start=source.index(marker);start=source.index('{',start);end=start+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start+1:end-1]
def boundary(entries,slots,callers,prologues,vptr,read,fdes):
 require(entries==ENTRIES,'entry identity')
 require(slots==SLOTS,'slot identity')
 require(callers==CALLERS,'caller LR identity')
 require(vptr==TASK_VPTR,'task vptr identity')
 for name,entry in ENTRIES.items():
  require(tuple(prologues[name])==PROLOGUES[name],'expected prologue words')
  require(fdes.get(entry)==FUNCTIONS[entry][0],'FDE entry/extent')
  require(read(entry,16)==struct.pack('<4I',*prologues[name]),'actual prologue words')
  require(struct.unpack('<Q',read(slots[name],8))[0]==entry,'actual slot target')
 require(read(CALLERS['submission']-4,4)==struct.pack('<I',0xd63f02e0),'submission caller BLR')
 require(read(CALLERS['execution']-4,4)==struct.pack('<I',0xd63f0020),'execution caller BLR')
def wrapper_shape(submission,executor):
 s=compact(submission);e=compact(executor)
 require('ldpx1,x2,[sp,#0]ldrx3,[sp,#152]addx0,sp,#352blaz_task_call_before' in s,'submission helper ABI setup')
 require('movx1,x0addx0,sp,#352blaz_task_call_after' in s,'full-width value-only after ABI setup')
 require('ldarx16,[x16]blrx16' in s and s.count('blraz_task_call')==0,'submission original call')
 require('movx1,x30blaz_task_exec_observe' in e,'one-input executor plus LR ABI setup')
 require('ldpx18,x30,[sp,#144].cfi_restore30addsp,sp,#320.cfi_def_cfa_offset0' in e,'executor original LR/SP restoration')
 require('ldarx16,[x16]brx16' in e,'executor original tail forwarding')
 require('.cfi_offset30,-264' in s and '.cfi_def_cfa_offset416' in s,'submission CFI setup')
 require('.cfi_offset30,-168' in e and '.cfi_def_cfa_offset320' in e,'executor CFI setup')
 require(sha(submission.encode())==REUSED['abi/submission-entry.S'][1],'immutable submission wrapper')
 require(sha(executor.encode())==REUSED['abi/executor-entry.S'][1],'immutable executor wrapper')

def source_values(source):
 values={}
 for name in ('SUBMIT_ENTRY','EXECUTE_ENTRY','SUBMIT_SLOT','EXECUTE_SLOT'):
  found=re.findall(r'\b'+name+r'\s*=\s*(0x[0-9a-fA-F]+)\b',source)
  require(len(found)==1,'one '+name+' constant');values[name]=int(found[0],16)
 words={}
 for name in ('SUBMIT_WORDS','EXECUTE_WORDS'):
  found=re.findall(r'\b'+name+r'\s*\[\]\s*=\s*\{([^}]+)\}',source)
  require(len(found)==1,'one '+name+' array');words[name]=tuple(int(v.strip(),16) for v in found[0].split(','))
 return ({'submission':values['SUBMIT_ENTRY'],'execution':values['EXECUTE_ENTRY']},
         {'submission':values['SUBMIT_SLOT'],'execution':values['EXECUTE_SLOT']},
         {'submission':words['SUBMIT_WORDS'],'execution':words['EXECUTE_WORDS']})

def source_contract(source):
 # Named source shape and ordering checks, not a parser or whole-program proof.
 code=compact(source);labels=[]
 def bound(label,condition):require(condition,label);labels.append(label)
 guard=compact(body(source,'static bool check_static('))
 init=compact(body(source,'static int init('))
 capture=compact(body(source,'static void capture('))
 before=compact(body(source,'void az_task_call_before('))
 after=compact(body(source,'void az_task_call_after('))
 execute=compact(body(source,'void az_task_exec_observe('))
 bound('unique module and exact firmware names','namespaceeto{' in code and 'ID[]="encoder-task-observer";' in code and 'FIRMWARE[]="'+FW_TAG+'";' in code)
 bound('uses actual copied transport and context headers','#include"deps/task-value-transport/transport.h"' in code and '#include"deps/task-call-return-abi/context.h"' in code)
 bound('permanent aligned submission original slot','alignas(void*)void*az_task_call_original=nullptr;' in code and code.count('az_task_call_original=')==1)
 bound('permanent aligned execution original slot','alignas(void*)void*az_task_exec_original=nullptr;' in code and code.count('az_task_exec_original=')==1)
 bound('runtime compatibility reads both slots and four words per entry',all(x in guard for x in (
  'static_read(&submit,SUBMIT_SLOT,sizeofsubmit);','static_read(&execute,EXECUTE_SLOT,sizeofexecute);',
  'static_read(sw,SUBMIT_ENTRY,sizeofsw);','static_read(ew,EXECUTE_ENTRY,sizeofew);',
  'returnsubmit==SUBMIT_ENTRY&&execute==EXECUTE_ENTRY&&!std::memcmp(sw,SUBMIT_WORDS,sizeofsw)&&!std::memcmp(ew,EXECUTE_WORDS,sizeofew);')))
 bound('explicit emulator opt-in and externally supplied run scope',all(x in init for x in (
  'std::getenv("AZ_ENCODER_TASK_OBSERVER_EMULATOR")','std::strcmp(opt,"1")',
  'parse_run(std::getenv("AZ_ENCODER_TASK_OBSERVER_RUN_ID"),run)')))
 bound('exact firmware compatibility before static native reads','std::strcmp(tag,FIRMWARE)' in init and init.index('std::strcmp(tag,FIRMWARE)')<init.index('check_static()'))
 submit='core->claim_hook(ID,SUBMIT_ENTRY,reinterpret_cast<void*>(az_task_call_entry),&az_task_call_original)'
 execute_claim='core->claim_hook(ID,EXECUTE_ENTRY,reinterpret_cast<void*>(az_task_exec_entry),&az_task_exec_original)'
 bound('claims use each permanent wrapper original slot directly',submit in init and execute_claim in init and init.count('core->claim_hook(')==2)
 bound('static compatibility and fallible worker setup precede claims',init.index('check_static()')<init.index('core->thread_start(')<init.index(submit)<init.index(execute_claim))
 bound('transport is published before claims and starts after both','published.store(t,std::memory_order_release);' in init and init.index('published.store(')<init.index(submit) and init.index(execute_claim)<init.index('t->start()'))
 bound('Ready release transition cannot overwrite a failed installer','expected=Installing;if(!state.compare_exchange_strong(expected,Ready,std::memory_order_release,std::memory_order_relaxed))' in init and init.index('t->start()')<init.index('expected=Installing;') and 'state.store(Ready' not in code)
 bound('transport storage is static and constructed once before publication','storage[sizeof(Transport)];' in code and 'staticstd::atomic<Transport*>published{nullptr};' in code and init.count('new(storage)Transport(run)')==1 and init.index('new(storage)Transport(run)')<init.index('published.store(') and 'if(!state.compare_exchange_strong(expected,Installing,' in init)
 bound('capture readiness checked with acquire before caller and native reads','if(state.load(std::memory_order_acquire)!=Ready)' in capture and capture.index('state.load(')<capture.index('if(lr')<capture.index('t->begin('))
 bound('capture exact original caller LR is not remapped','if(lr!=(phase==TC_SUBMISSION?TC_SUBMIT_LR:TC_EXECUTE_LR))' in capture and 't->begin(&c,phase,lr,reinterpret_cast<constvoid*>(task),tid);' in capture and 'lr=' not in capture)
 bound('before helper has exact four-argument native-width ABI','extern"C"voidaz_task_call_before(TaskCallContext*raw,uintptr_t,uintptr_ttask,uintptr_tlr)noexcept{' in code)
 bound('after helper has pointer-free context and full-x0 ABI','extern"C"voidaz_task_call_after(constTaskCallContext*raw,uint64_tresult)noexcept{' in code)
 bound('executor helper has exact task-plus-original-LR ABI','extern"C"voidaz_task_exec_observe(uintptr_ttask,uintptr_tlr)noexcept{' in code)
 bound('submission before copies private 64-byte value context','tvt::ErrnoGuardsaved;tc_contextc{};eto::capture(c,TC_SUBMISSION,task,lr);std::memcpy(raw,&c,sizeofc);'==before)
 bound('executor observes only its one input and incoming LR','tvt::ErrnoGuardsaved;tc_contextc{};eto::capture(c,TC_EXECUTION,task,lr);'==execute)
 bound('after consumes private context and full native return only','tvt::ErrnoGuardsaved;tc_contextc{};std::memcpy(&c,raw,sizeofc);' in after and 'if(t)t->finish(c,result);' in after and 'task' not in after and 'current_tid' not in after and 'state' not in after and after.count('std::memcpy(')==1)
 bound('context and payload field offsets are asserted',all('offsetof(TaskCallContext,'+field+')==offsetof(tc_context,'+field+')' in code for field in ('values','run_id','invocation_id','thread_observation','phase','status','reserved')) and 'sizeof(TaskCallContext)==sizeof(tc_context)' in code and all('offsetof(TaskValues,'+old+')==offsetof(tc_values,'+new+')' in code for old,new in [('taskno','task_no'),('id','id'),('delta_bits','delta_bits'),('cumulative_bits','cumulative_bits')]))
 bound('no helper-side native forwarding or navigation calls',code.count('az_task_call_entry(')==1 and code.count('az_task_exec_entry(')==1 and not any(x in code for x in ('claim_vslot(','dlclose(','munmap(','deletepublished','deletecore','request_view(')))
 return labels

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--elf',type=Path,required=True);ap.add_argument('--result',type=Path);args=ap.parse_args()
 checks=[];controls=[]
 files=['observer.cpp','mod.c','Makefile','azmod.json','frozen-inputs.json','verify-boundary.py']+list(REUSED)
 snapshot={filename:(HERE/filename).read_bytes() for filename in files}
 def check(label,ok):require(ok,label);checks.append(label)
 def reject(name,fn,reason):
  try:fn()
  except AssertionError as ex:require(str(ex)==reason,name+': unexpected reason '+str(ex));controls.append({'name':name,'rejected_by':reason})
  else:raise AssertionError(name+': invalid binding accepted')
 raw=args.elf.read_bytes();check('exact firmware SHA256',sha(raw)==FIRMWARE)
 elf=ELFFile(io.BytesIO(raw));check('64-bit little-endian ARM64 executable',elf.elfclass==64 and elf.little_endian and elf['e_machine']=='EM_AARCH64' and elf['e_type']=='ET_EXEC')
 segments=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
 def read(address,size):
  matches=[s for s in segments if s['p_vaddr']<=address and address+size<=s['p_vaddr']+s['p_filesz']]
  require(len(matches)==1,'unique file-backed range');s=matches[0];offset=s['p_offset']+address-s['p_vaddr'];return raw[offset:offset+size]
 fdes={f['initial_location']:f['address_range'] for f in elf.get_dwarf_info().EH_CFI_entries() if isinstance(f,FDE)}
 md=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);instructions={}
 for start,(extent,digest) in FUNCTIONS.items():
  check('FDE '+hex(start),fdes.get(start)==extent)
  check('body SHA256 '+hex(start),sha(read(start,extent))==digest)
  check('executable file-backed body '+hex(start),any(s['p_flags']&1 and s['p_vaddr']<=start and start+extent<=s['p_vaddr']+s['p_filesz'] for s in segments))
  decoded=list(md.disasm(read(start,extent),start));check('complete instruction decoding '+hex(start),sum(i.size for i in decoded)==extent)
  instructions.update({i.address:(i.mnemonic,i.op_str) for i in decoded})
 for label,(address,expected) in BLOCKS.items():
  check('instruction block '+label,all(instructions.get(address+4*i)==value for i,value in enumerate(expected)))
 for address,expected in WORDS.items():check('data word '+hex(address),struct.unpack('<Q',read(address,8))[0]==expected)
 def cstring(address):return read(address,256).split(b'\0',1)[0].decode()
 for address,expected in [(0x321add8,'N4meow12TimerTaskBoxE'),(0x2d44450,TASK_NAME)]:
  check('RTTI '+hex(address),cstring(struct.unpack('<Q',read(address+8,8))[0])==expected)
 boundary(ENTRIES,SLOTS,CALLERS,PROLOGUES,TASK_VPTR,read,fdes);checks.append('complete two-boundary firmware binding')
 for label,baseline,changes,reason in [
  ('submission entry',ENTRIES,{'submission':ENTRIES['submission']+4},'entry identity'),
  ('execution entry',ENTRIES,{'execution':ENTRIES['execution']+4},'entry identity'),
  ('submission slot',SLOTS,{'submission':SLOTS['submission']-8},'slot identity'),
  ('execution slot',SLOTS,{'execution':SLOTS['execution']+8},'slot identity'),
  ('submission LR',CALLERS,{'submission':CALLERS['submission']-4},'caller LR identity'),
  ('execution LR',CALLERS,{'execution':CALLERS['execution']+4},'caller LR identity'),
 ]:
  altered={**baseline,**changes}
  reject('wrong '+label,lambda altered=altered,baseline=baseline:boundary(altered if baseline is ENTRIES else ENTRIES,altered if baseline is SLOTS else SLOTS,altered if baseline is CALLERS else CALLERS,PROLOGUES,TASK_VPTR,read,fdes),reason)
 reject('wrong task vptr',lambda:boundary(ENTRIES,SLOTS,CALLERS,PROLOGUES,TASK_VPTR+8,read,fdes),'task vptr identity')
 for name in ENTRIES:
  changed={**PROLOGUES,name:(PROLOGUES[name][0]^1,*PROLOGUES[name][1:])}
  reject('wrong expected '+name+' prologue',lambda changed=changed:boundary(ENTRIES,SLOTS,CALLERS,changed,TASK_VPTR,read,fdes),'expected prologue words')
  changed_fde={**fdes,ENTRIES[name]:FUNCTIONS[ENTRIES[name]][0]-4}
  reject('wrong '+name+' extent',lambda changed_fde=changed_fde:boundary(ENTRIES,SLOTS,CALLERS,PROLOGUES,TASK_VPTR,read,changed_fde),'FDE entry/extent')
  for position in range(4):
   def broken_read(address,size,name=name,position=position):
    value=read(address,size)
    if address==ENTRIES[name] and size==16:
     changed=bytearray(value);changed[position*4]^=1;return bytes(changed)
    return value
   reject(name+' actual prologue word '+str(position),lambda broken_read=broken_read:boundary(ENTRIES,SLOTS,CALLERS,PROLOGUES,TASK_VPTR,broken_read,fdes),'actual prologue words')
  def wrong_slot(address,size,name=name):return struct.pack('<Q',ENTRIES[name]+4) if address==SLOTS[name] and size==8 else read(address,size)
  reject(name+' actual slot target',lambda wrong_slot=wrong_slot:boundary(ENTRIES,SLOTS,CALLERS,PROLOGUES,TASK_VPTR,wrong_slot,fdes),'actual slot target')
  def wrong_call(address,size,name=name):return struct.pack('<I',0xd503201f) if address==CALLERS[name]-4 and size==4 else read(address,size)
  reject(name+' actual caller BLR',lambda wrong_call=wrong_call:boundary(ENTRIES,SLOTS,CALLERS,PROLOGUES,TASK_VPTR,wrong_call,fdes),name+' caller BLR')
 # Continue below with immutable source and actual adapter binding.
 refs={}
 for filename,digest in REFERENCES.items():
  payload=(HERE.parent/filename).read_bytes();check('immutable reference '+filename,sha(payload)==digest);refs[filename]=json.loads(payload)
 policy=refs['route-task-ownership/static-result.json']
 for name,entry in ENTRIES.items():
  previous=policy['candidate_policy'][hex(entry)]
  check('retained unchanged prologue policy '+name,previous['patch_ok_1'] and previous['patch_ok_4'] and previous['prologue_sha256']==sha(read(entry,16)))
 check('retained policy exact firmware/upstream',policy['firmware_sha256']==FIRMWARE and policy['upstream_commit']=='3e0c5cb010454228f4ee63226b60d060494eeaae')
 abi=refs['task-observer-integration/test-result.json'];independent=refs['task-observer-integration/independent-result.json']
 check('corrected prior ARM64 integration evidence',abi['candidate_summary'][:8]==[239,0,16,45,42,8,45,7] and len(abi['controls'])==4 and len(abi['cfi_rejection_controls'])==2 and abi['synthetic_only'] and not abi['firmware_or_device_access'] and 'source_review_correction' in abi)
 check('independent corrected ABI source and object review',independent['candidate_assertions']==239 and independent['fixture_counter_race_corrected_before_accepted_runs'] and independent['source_and_executor_object_equal'] and independent['all_allocated_code_data_and_unwind_sections_byte_identical'])
 frozen=json.loads(snapshot['frozen-inputs.json']);check('copied-source base commit',frozen['base_commit']==REFERENCE_COMMIT)
 copied={}
 for filename,(original,digest) in REUSED.items():
  payload=snapshot[filename];original_payload=(HERE.parent/original).read_bytes()
  check('copied source identity '+filename,sha(payload)==digest and payload==original_payload)
  check('frozen-input declaration '+filename,frozen['files'][filename]=={'source':'experiments/mods-shortcut-delivery/'+original,'sha256':digest})
  command=['git','show',REFERENCE_COMMIT+':experiments/mods-shortcut-delivery/'+original]
  committed=subprocess.run(command,cwd=HERE.parents[2],capture_output=True,check=True).stdout
  check('reference commit identity '+filename,payload==committed)
  copied[filename]=digest
 submission=snapshot['abi/submission-entry.S'].decode();executor=snapshot['abi/executor-entry.S'].decode()
 wrapper_shape(submission,executor);checks.append('complete reused wrapper ABI source identity and setup')
 for name,sub,exe,reason in [
  ('wrong submission original LR register',submission.replace('ldr x3,[sp,#152]','ldr x3,[sp,#0]'),executor,'submission helper ABI setup'),
  ('wrong submission native input transfer',submission.replace('ldp x1,x2,[sp,#0]','ldp x2,x3,[sp,#0]'),executor,'submission helper ABI setup'),
  ('truncated full-x0 after input',submission.replace('mov x1,x0','mov w1,w0'),executor,'full-width value-only after ABI setup'),
  ('wrong executor LR register',submission,executor.replace('mov x1, x30','mov x1, x0'),'one-input executor plus LR ABI setup'),
  ('wrong executor frame restoration',submission,executor.replace('add sp, sp, #320','add sp, sp, #304'),'executor original LR/SP restoration'),
  ('wrong executor original transfer',submission,executor.replace('br x16','blr x16'),'executor original tail forwarding'),
  ('missing submission LR CFI',submission.replace('.cfi_offset 30,-264',''),executor,'submission CFI setup'),
  ('missing executor LR CFI',submission,executor.replace('.cfi_offset 30, -168',''),'executor CFI setup'),
 ]:reject(name,lambda sub=sub,exe=exe:wrapper_shape(sub,exe),reason)
 header=snapshot['deps/task-capture-contract/capture.h'].decode()
 def header_values(candidate):
  result={}
  for macro in ('TC_SUBMIT_LR','TC_EXECUTE_LR','TC_TASK_VPTR'):
   found=re.findall(r'^#define\s+'+macro+r'\s+UINT64_C\((0x[0-9a-fA-F]+)\)',candidate,re.M)
   require(len(found)==1,'one capture macro '+macro);result[macro]=int(found[0],16)
  require(result=={'TC_SUBMIT_LR':CALLERS['submission'],'TC_EXECUTE_LR':CALLERS['execution'],'TC_TASK_VPTR':TASK_VPTR},'capture caller/vptr constants')
 header_values(header);checks.append('capture header exact caller and type constants')
 for old in ('0x1b01e04','0x237d800','0x2d444d8'):
  reject('wrong copied capture constant '+old,lambda old=old:header_values(header.replace(old,hex(int(old,16)+4))),'capture caller/vptr constants')
 source=snapshot['observer.cpp'].decode();mod=snapshot['mod.c'].decode();manifest=json.loads(snapshot['azmod.json']);makefile=snapshot['Makefile'].decode()
 def actual_source_binding(candidate):
  entries,slots,prologues=source_values(candidate)
  boundary(entries,slots,CALLERS,prologues,TASK_VPTR,read,fdes)
 actual_source_binding(source);checks.append('actual adapter constants match exact firmware binding')
 checks.extend(source_contract(source))
 for name,old,new,reason in [
  ('adapter submission entry','SUBMIT_ENTRY=0x237a440','SUBMIT_ENTRY=0x237a444','entry identity'),
  ('adapter executor entry','EXECUTE_ENTRY=0x1b029c0','EXECUTE_ENTRY=0x1b029c4','entry identity'),
  ('adapter submission slot','SUBMIT_SLOT=0x321b138','SUBMIT_SLOT=0x321b130','slot identity'),
  ('adapter executor slot','EXECUTE_SLOT=0x2d444e8','EXECUTE_SLOT=0x2d444f0','slot identity'),
  ('adapter submission prologue','0xa9bd7bfd','0xd503201f','expected prologue words'),
  ('adapter executor prologue','0xf9400c03','0xd503201f','expected prologue words'),
 ]:
  require(old in source,'control precondition '+name)
  reject(name,lambda old=old,new=new:actual_source_binding(source.replace(old,new,1)),reason)
 for name,old,new,reason in [
  ('temporary submission original','&az_task_call_original)','nullptr)','claims use each permanent wrapper original slot directly'),
  ('temporary execution original','&az_task_exec_original)','nullptr)','claims use each permanent wrapper original slot directly'),
  ('skip runtime prologue comparison','!std::memcmp(ew,EXECUTE_WORDS,sizeof ew)','true','runtime compatibility reads both slots and four words per entry'),
  ('invert caller filter','if(lr!=(phase==TC_SUBMISSION?TC_SUBMIT_LR:TC_EXECUTE_LR))','if(lr==(phase==TC_SUBMISSION?TC_SUBMIT_LR:TC_EXECUTE_LR))','capture exact original caller LR is not remapped'),
  ('narrow before helper caller','az_task_call_before(TaskCallContext* raw,uintptr_t,uintptr_t task,uintptr_t lr)','az_task_call_before(TaskCallContext* raw,uintptr_t,uintptr_t task,uint32_t lr)','before helper has exact four-argument native-width ABI'),
  ('narrow after helper result','az_task_call_after(const TaskCallContext* raw,uint64_t result)','az_task_call_after(const TaskCallContext* raw,uint32_t result)','after helper has pointer-free context and full-x0 ABI'),
  ('narrow executor helper caller','az_task_exec_observe(uintptr_t task,uintptr_t lr)','az_task_exec_observe(uintptr_t task,uint32_t lr)','executor helper has exact task-plus-original-LR ABI'),
  ('wrong before phase','eto::capture(c,TC_SUBMISSION,task,lr)','eto::capture(c,TC_EXECUTION,task,lr)','submission before copies private 64-byte value context'),
  ('wrong executor phase','eto::capture(c,TC_EXECUTION,task,lr)','eto::capture(c,TC_SUBMISSION,task,lr)','executor observes only its one input and incoming LR'),
  ('overwrite failed readiness','expected=Installing;','expected=Failed;','Ready release transition cannot overwrite a failed installer'),
  ('suppress reserved late return','std::memcpy(&c,raw,sizeof c);','std::memcpy(&c,raw,sizeof c);if(eto::state.load(std::memory_order_acquire)!=eto::Ready)return;','after consumes private context and full native return only'),
  ('truncate adapter after result','t->finish(c,result)','t->finish(c,static_cast<uint32_t>(result))','after consumes private context and full native return only'),
 ]:
  require(old in source,'control precondition '+name)
  reject(name,lambda old=old,new=new:source_contract(source.replace(old,new,1)),reason)
 check('manifest unique unpublished observer identity',manifest['id']=='encoder-task-observer' and manifest['version']=='0.0.1' and manifest['firmware']==[FW_TAG] and manifest['hooks'] is True)
 m=compact(mod)
 check('C module exact identity and firmware tag','.id="encoder-task-observer"' in m and 'tags[]={"'+FW_TAG+'",0}' in m)
 check('C module only init/tick adapter callbacks','.init=task_observer_init' in m and '.tick=task_observer_tick' in m and not any(x in m for x in ('.frame=','.draw=','.touch=','.fini=')))
 check('build includes both unchanged ABI files',all(x in makefile for x in ('observer.cpp','mod.c','abi/submission-entry.S','abi/executor-entry.S')))
 check('build retains exception/unwind metadata and hides exports',all(x in makefile for x in ('-fexceptions','-fasynchronous-unwind-tables','-fvisibility=hidden','--no-undefined','global: az_mod_v1_entry; local: *;')))
 check('production build does not enable test seams','AZTASK_TEST' not in makefile and 'TC_READ_BYTES' not in makefile and 'TVT_TEST_POINT' not in makefile)
 check('all bound module inputs unchanged during verification',all((HERE/filename).read_bytes()==payload for filename,payload in snapshot.items()))
 report={'result':'PASS','scope':'Exact-firmware/static adapter binding only; no claim or runtime execution',
  'firmware_sha256':FIRMWARE,'reference_commit':REFERENCE_COMMIT,'checks':len(checks),'check_labels':checks,
  'instruction_anchors':sum(len(v[1]) for v in BLOCKS.values()),'data_word_anchors':len(WORDS),'rtti_anchors':2,
  'strict_rejection_controls':controls,'strict_rejection_control_count':len(controls),
  'boundaries':{name:{'entry':hex(entry),'slot':hex(SLOTS[name]),'original_caller_lr':hex(CALLERS[name]),
   'fde_extent':hex(FUNCTIONS[entry][0]),'body_sha256':FUNCTIONS[entry][1],'prologue_sha256':sha(read(entry,16)),
   'retained_policy_allows_first_four':True,'actual_claim_attempted':False} for name,entry in ENTRIES.items()},
  'task_vptr':hex(TASK_VPTR),'source_sha256':{filename:sha(snapshot[filename]) for filename in files},
  'immutable_references':REFERENCES,'copied_source_sha256':copied,
  'submission_abi':'x0 box, x1 task; full x0 native result; private 64B before context; normal-return after only',
  'execution_abi':'one incoming x0 task; helper receives original LR in x1; original SP/LR tail-forwarding; no result meaning',
  'prior_abi_suite_rerun':False,'prior_hook_policy_suite_rerun':False,
  'limits':['Source-shape checks and exact hashes are not whole-program verification',
   'Caller/type matching is a rejection filter, not native memory validity or retained lifetime',
   'Static slot targets do not establish the current live receiver or installed hook ownership',
   'Prior policy acceptance does not establish island allocation, original publication or live claim success',
   'Synthetic ABI evidence does not prove installed trampoline/asynchronous unwind or teardown',
   'Normal submission return, executor entry and missing records do not prove admission/completion/cancellation',
   'No firmware, emulator, device, native call, hook claim, package publication or prior-suite execution']}
 output=json.dumps(report,indent=2,sort_keys=True)+'\n'
 if args.result:args.result.write_text(output)
 else:print(output,end='')

if __name__=='__main__':main()

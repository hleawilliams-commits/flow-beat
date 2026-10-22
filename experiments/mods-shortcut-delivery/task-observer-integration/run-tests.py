#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""New integrated ARM64 fixture only; uses a pinned local image without network."""
import argparse,hashlib,json,re,subprocess,uuid
from pathlib import Path
from collections import Counter
HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
IMAGE='azmod-build:18.04'
ID='sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90'
SUBMISSION='16c7df29fef3c80fcd5f6259d4073abf92fd57471bc203c39f3853a0709d06c8'
parser=argparse.ArgumentParser();parser.add_argument('--candidate-only',action='store_true');parser.add_argument('--explore-controls',action='store_true');args=parser.parse_args()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(cmd):return subprocess.run(cmd,text=True,capture_output=True,timeout=120)
x=run(['docker','image','inspect',IMAGE,'--format','{{.Id}} {{.Architecture}}'])
assert x.returncode==0 and x.stdout.strip()==ID+' arm64',(x.stdout,x.stderr)
assert sha(HERE/'submission-entry.S')==SUBMISSION==sha(ROOT/'task-call-return-abi/entry.S')
assert sha(ROOT/'task-capture-contract/capture.h')=='aa9b82311065fa3481cda29883f6dae40d1685c213d520271e1f4530039291ea'
assert sha(ROOT/'task-value-transport/transport.h')=='3f2a0b902a6fae0ab425be570bda3904ac5e99203415457aa7ced059d5061d55'
paths=['task-observer-integration/'+n for n in ('submission-entry.S','executor-entry.S','fixture.S','test_integration.cpp','run-tests.py')]+[
 'task-capture-contract/capture.h','task-value-transport/transport.h','task-call-return-abi/context.h']
hashes={p:sha(ROOT/p) for p in paths}
out=HERE/'out-history'/str(uuid.uuid4());inputs=out/'inputs';inputs.mkdir(parents=True)
for p in paths:
 dst=inputs/p;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes((ROOT/p).read_bytes())
(out/'source-manifest.json').write_text(json.dumps(hashes,indent=2)+'\n')
def docker(command):return run(['docker','run','--rm','--pull=never','--network','none','--platform','linux/arm64','-v',str(inputs)+':/src:ro','-v',str(out)+':/out',IMAGE,'sh','-c',command])
def retain(r,name):
 text=r.stdout+r.stderr;(out/(name+'-process.txt')).write_text(text)
 (out/(name+'-exit.json')).write_text(json.dumps({'exit_code':r.returncode})+'\n');return text
BASE='/src/task-observer-integration/'
FLAGS='-std=c++17 -O2 -g -Wall -Wextra -Werror -fexceptions -fasynchronous-unwind-tables -pthread '
def build(submission,executor,test,output):return 'g++ '+FLAGS+submission+' '+executor+' '+BASE+'fixture.S '+test+' -o /out/'+output+' && /out/'+output
cmd='set -eu\nuname -m\ng++ --version | head -n1\n'+build(BASE+'submission-entry.S',BASE+'executor-entry.S',BASE+'test_integration.cpp','test_integration')+'\n'
cmd+='g++ -g -fPIC -c '+BASE+'executor-entry.S -o /out/executor-entry.o\n'
cmd+='objdump -d /out/executor-entry.o > /out/executor-disassembly.txt\nreadelf --debug-dump=frames /out/executor-entry.o > /out/executor-cfi.txt\n'
r=docker(cmd);text=retain(r,'candidate');print(text,end='',flush=True)
assert r.returncode==0,(r.returncode,str(out))
pattern=r'^ARM64 integration: (\d+) passed, (\d+) failed; scenarios=(\d+); before=(\d+); after=(\d+); executor=(\d+); native-submit=(\d+); native-execute=(\d+); concurrent-records=(\d+)$'
summary=re.findall(pattern,text,re.M);assert len(summary)==1 and int(summary[0][1])==0,text
numbers=list(map(int,summary[0]));assert numbers[:8]==[239,0,16,45,42,8,45,7] and 1<=numbers[8]<=8,text
assert not re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|runtime error:|DEADLYSIGNAL',text),text
print('Retained run '+out.name,flush=True)
report={'image':IMAGE,'image_id':ID,'platform':'linux/arm64','source_sha256':hashes,'candidate_summary':numbers,
 'private_run_id':out.name,'test_elf_sha256':sha(out/'test_integration'),'executor_object_sha256':sha(out/'executor-entry.o'),
 'unchanged_submission_assembly_sha256':SUBMISSION,'synthetic_only':True,'firmware_or_device_access':False}
(out/'candidate-result.json').write_text(json.dumps(report,indent=2)+'\n')
if args.candidate_only:raise SystemExit(0)

def once(s,a,b):assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
executor=(inputs/'task-observer-integration/executor-entry.S').read_text()
submission=(inputs/'task-observer-integration/submission-entry.S').read_text()
test=(inputs/'task-observer-integration/test_integration.cpp').read_text()
controls=[
 ('executor-wrong-lr','executor',once(executor,'    mov x1, x30\n','    mov x1, x0\n')),
 ('executor-omit-x0-x1-restore','executor',once(executor,'    ldp x0, x1, [sp, #0]\n','    nop\n')),
 ('truncate-observed-return','submission',once(submission,'    mov x1,x0\n','    mov w1,w0\n')),
 ('executor-clobber-errno','test',once(test,'trace(1);errno=93;errno=saved;','trace(1);errno=93;(void)saved;')),
]
expectations={
 'executor-wrong-lr':{k:1 for k in [
 'entry-execution-return/actual-caller-lrs','entry-execution-return/distinct-observer-invocations',
 'entry-execution-return/distinct-phases','entry-execution-return/execution-entry-visible-before-submission-return',
 'entry-execution-return/independent-records-present','entry-execution-return/only-five-reads-per-capture',
 'entry-execution-return/value-only-candidate','exception-bypasses-after/entries-remain-no-return',
 'exception-bypasses-after/usable-after-unwind','one-input-executor-abi/executor-original-lr-register',
 'unmap-before-return/no-post-task-read','wrong-caller/unmapped-executor-lr-retained']},
 'executor-omit-x0-x1-restore':{'null-task/null-executor-once':1,'one-input-executor-abi/native-input-register-preserved':2},
 'truncate-observed-return':{'close-with-outstanding/return-publishes-after-close':1,
 'entry-execution-return/full-qualified-bits':1,'mismatch-return/ambiguous-return-only':1,
 'nested-native/nested-result-per-invocation':3},
 'executor-clobber-errno':{'concurrent-submission/native-entry-errno':1,'one-input-executor-abi/executor-helper-errno-preserved':1},
}
verified=[];logs=[]
for name,kind,source in controls:
 ext='.cpp' if kind=='test' else '.S';p=out/(name+ext);p.write_text(source)
 if kind=='test':
  # Keep relative includes identical in a private mirror, never change shared input files.
  temp=out/'control-inputs'/name
  for rel in paths:
   dst=temp/rel;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes((inputs/rel).read_bytes())
  (temp/'task-observer-integration/test_integration.cpp').write_text(source)
  testpath='/out/control-inputs/'+name+'/task-observer-integration/test_integration.cpp'
 else:testpath=BASE+'test_integration.cpp'
 rr=docker(build('/out/'+name+ext if kind=='submission' else BASE+'submission-entry.S',
                 '/out/'+name+ext if kind=='executor' else BASE+'executor-entry.S',testpath,name))
 tt=retain(rr,name);failures=re.findall(r'^FAIL (.+)$',tt,re.M);sm=re.findall(pattern,tt,re.M)
 assert rr.returncode==1 and len(sm)==1 and len(failures)>0,(rr.returncode,tt)
 ns=list(map(int,sm[0]));assert ns[0]+ns[1]==numbers[0] and ns[1]==len(failures) and ns[2:8]==numbers[2:8] and 1<=ns[8]<=8,tt
 assert not re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|runtime error:|DEADLYSIGNAL|Segmentation fault',tt),tt
 expected=dict(expectations[name])
 if name=='truncate-observed-return':expected['concurrent-submission/concurrent-full-return']=ns[8]
 if not args.explore_controls:assert dict(Counter(failures))==expected,(name,expected,tt)
 item={'name':name,'summary':ns,'expected_failures':dict(Counter(failures)),'source_sha256':sha(p),'output_sha256':hashlib.sha256(tt.encode()).hexdigest()}
 verified.append(item);logs.append('\nControl: '+name+'\n'+tt);print(name+': '+json.dumps(item['expected_failures'],sort_keys=True),flush=True)
report['controls']=verified
(out/'explored-result.json').write_text(json.dumps(report,indent=2)+'\n')
if args.explore_controls:raise SystemExit(0)
dis=(out/'executor-disassembly.txt').read_text();cfi=(out/'executor-cfi.txt').read_text()
instructions=[(m.group(1),m.group(2).strip()) for m in re.finditer(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(\S+)\s*(.*)$',dis,re.M)]
assert len(instructions)==46,instructions
assert [i for i in instructions if i[0] in ('bl','blr','br','ret')]==[('bl','0 <az_task_exec_observe>'),('br','x16')]
assert instructions[0]==('sub','sp, sp, #0x140') and instructions[-1]==('br','x16')
assert instructions[20]==('mov','x1, x30')
assert instructions[-5]==('add','sp, sp, #0x140') and instructions[-2]==('ldar','x16, [x16]')
def valid_cfi(text):return all(x in text for x in (
 'pc=0000000000000000..00000000000000b8','DW_CFA_def_cfa_offset: 320',
 'r30 (x30) at cfa-168','DW_CFA_restore: r30 (x30)','DW_CFA_def_cfa_offset: 0'))
assert valid_cfi(cfi)
cfi_controls=[]
for name,source in [
 ('executor-missing-lr-rule',executor.replace('.cfi_offset 30, -168\n','')),
 ('executor-wrong-frame-rule',executor.replace('.cfi_def_cfa_offset 320\n','.cfi_def_cfa_offset 304\n')),
]:
 (out/(name+'.S')).write_text(source)
 rr=docker('g++ -g -fPIC -c /out/'+name+'.S -o /out/'+name+'.o && readelf --debug-dump=frames /out/'+name+'.o')
 tt=retain(rr,name);assert rr.returncode==0 and not valid_cfi(tt),(name,tt)
 cfi_controls.append({'name':name,'compiled':True,'executed':False,'metadata_rejected':True,
                      'source_sha256':sha(out/(name+'.S'))})
 print(name+': metadata audit rejected (not executed)',flush=True)
report.update({'executor_instruction_count':46,'executor_cfi_audit':True,'executor_stack_bytes':320,
 'executor_lr_cfa_offset':-168,'cfi_rejection_controls':cfi_controls,
 'context_bytes':64,'entry_and_return_publication_independent':True,'normal_return_after_close_published':True,
 'synthetic_task_unmapped_before_after':True,'one_native_exception':True,'submission_invocations_on_unwind':3,
 'exception_destructors':5,'synthetic_lr_mapping_only':True,'actual_firmware_caller_provenance':False,
 'copy_range_test':'TC_READ_BYTES requested-range seam; not proof of default memcpy machine load extents',
 'concurrent_native_calls':8,'concurrent_records':numbers[8],
 'concurrent_contention_allowed':True,'sanitizers_used':False,'runtime_hook_or_trampoline_unwind_proven':False,
 'source_review_correction':{
  'issue':'Earlier fixture destructor bookkeeping incremented one plain unsigned counter from four concurrent native-submit threads.',
  'correction':'Only that bookkeeping counter is now std::atomic<unsigned>; no production transport or wrapper source changed.',
  'prior_passing_runs_superseded':[
   '7d0899a9-d43d-4abb-9577-c256420b0b7c','6c1db50c-7979-4283-b957-05e185a10b0a',
   '3593f94a-d6be-4d26-9908-b0e1b4a73972','ede96c92-cc05-4a9e-a63a-36b94a42dfb9'],
  'detected_by':'Independent root source review, not a sanitizer report',
  'original_runs_and_pre_fix_files_retained':True}})
assert {p:sha(ROOT/p) for p in paths}==hashes,'input changed during test run'
(HERE/'test-result.json').write_text(json.dumps(report,indent=2)+'\n')
(HERE/'test-results.txt').write_text(text+''.join(logs))
(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
for name in ('executor-disassembly.txt','executor-cfi.txt'):
 (HERE/name).write_text((out/name).read_text().replace('/out','<OUTPUT>'))


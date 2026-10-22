#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic ARM64 call/return execution in pinned local image, network disabled."""
from collections import Counter
import hashlib,json,re,subprocess,uuid
from pathlib import Path
HERE=Path(__file__).resolve().parent
IMAGE='azmod-build:18.04'
ID='sha256:c7e3893c4f03e0ff30555c2120b3d9ec53834eaa2c8ce376a2da09b70fd23a90'
def run(cmd):return subprocess.run(cmd,text=True,capture_output=True,timeout=90)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
c=run(['docker','image','inspect',IMAGE,'--format','{{.Id}} {{.Architecture}}'])
assert c.returncode==0 and c.stdout.strip()==ID+' arm64',(c.stdout,c.stderr)
names=['entry.S','context.h','test_fixture.S','test_abi.cpp','run-tests.py']
hashes={n:sha(HERE/n) for n in names}
out=HERE/'out-history'/str(uuid.uuid4());inputs=out/'inputs';inputs.mkdir(parents=True)
for n in names:(inputs/n).write_bytes((HERE/n).read_bytes())
assert {n:sha(inputs/n) for n in names}==hashes
(out/'source-manifest.json').write_text(json.dumps(hashes,indent=2)+'\n')
def docker(command):
 return run(['docker','run','--rm','--pull=never','--network','none','--platform','linux/arm64',
             '-v',str(inputs)+':/src:ro','-v',str(out)+':/out',IMAGE,'sh','-c',command])
def retain(r,name):
 text=r.stdout+r.stderr;(out/(name+'-process.txt')).write_text(text)
 (out/(name+'-exit.json')).write_text(json.dumps({'exit_code':r.returncode})+'\n')
 return text
command='''set -eu
uname -m
g++ --version | head -n 1
g++ -O2 -g -Wall -Wextra -Werror -fexceptions -fasynchronous-unwind-tables -fPIC -c /src/entry.S -o /out/entry.o
g++ -std=c++17 -O2 -g -Wall -Wextra -Werror -fexceptions -fasynchronous-unwind-tables -pthread /out/entry.o /src/test_fixture.S /src/test_abi.cpp -o /out/test_abi
/out/test_abi
objdump -d /out/entry.o > /out/entry-disassembly.txt
readelf --debug-dump=frames /out/entry.o > /out/entry-cfi.txt
readelf --wide -r /out/entry.o > /out/entry-relocations.txt
'''
r=docker(command);text=retain(r,'candidate');print(text,end='',flush=True)
assert r.returncode==0,(r.returncode,str(out))
assert not re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|LeakSanitizer|runtime error:|DEADLYSIGNAL',text)
m=re.findall(r'^AArch64 call/return assertions: (\d+) passed, (\d+) failed; before=(\d+); after=(\d+); native=(\d+)$',text,re.M)
assert len(m)==1 and [int(x) for x in m[0]]==[319,0,83,80,83],text
report={'image':IMAGE,'image_id':ID,'platform':'linux/arm64','source_sha256':hashes,
        'candidate_summary':[int(x) for x in m[0]],'object_sha256':sha(out/'entry.o'),
        'test_elf_sha256':sha(out/'test_abi'),'private_run_id':out.name,
        'actual_arm64_assembly_executed':True,'no_firmware_or_device_access':True}
(out/'candidate-result.json').write_text(json.dumps(report,indent=2)+'\n')
print('Retained candidate run '+out.name,flush=True)


entry=(inputs/'entry.S').read_text()
def once(text,old,new,index=0):
    parts=text.split(old)
    assert len(parts)>index+1,(old,index)
    return old.join(parts[:index+1])+new+old.join(parts[index+1:])
def assert_run(result,expected):
    text=result.stdout+result.stderr
    failures=re.findall(r'^FAIL line \d+: (.+)$',text,re.M)
    summary=re.findall(r'^AArch64 call/return assertions: (\d+) passed, (\d+) failed; before=(\d+); after=(\d+); native=(\d+)$',text,re.M)
    assert len(summary)==1,text
    passed,failed,before,after,native=map(int,summary[0])
    assert passed+failed==319 and (before,after,native)==(83,80,83),text
    assert failed==len(expected) and Counter(failures)==Counter(expected),text
    assert result.returncode==bool(expected),(result.returncode,text)
    assert not re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|LeakSanitizer|runtime error:|DEADLYSIGNAL',text),text
    return {'assertions':319,'passed':passed,'failed':failed,'before_calls':before,
            'after_calls':after,'native_calls':native,'exit_code':result.returncode,
            'expected_failures':dict(Counter(expected))}
report['candidate']=assert_run(r,[])
post_expected=['events[0].result==0xfedcba9876543210ULL',
 'events[base].box==a&&events[base].task==b&&events[base].result==value(a,b)']+[
 'events[base+i].result==0x8000000000000013ULL-i']*4+[
 'observed==base+2&&events[base].result==value(a,b)&&events[base+1].result==value(31,41)',
 'observed==base+2&&events[base].result==value(a,b)&&events[base+1].result==value(51,61)']+[
 'events[i].result==value(events[i].box,events[i].task)']*64
return_expected=['fixture_invoke_registers()==0xfedcba9876543210ULL',
 'fixture_output.x[i]==(i==0?0xfedcba9876543210ULL:i==1?0x0123456789abcdefULL:0x700+i)']+[
 'fixture_entry_integer(a,b)==value(a,b)']*4+[
 'p.a==(a^0xfedcba9876543210ULL)&&p.b==(b^0x123456789abcdef0ULL)',
 'fixture_entry_nested(3,0x8000000000000010ULL)==0x8000000000000013ULL']+[
 'events[base+i].result==0x8000000000000013ULL-i']*3+[
 'nested_before_result==value(31,41)','nested_after_result==value(51,61)',
 'fixture_entry_integer(0x1234,reinterpret_cast<U>(page))==0x8000000000000001ULL','thread_errors==0']
controls=[
 ('wrong-original-lr',once(entry,'    ldr x3,[sp,#152]\n','    ldr x3,[sp,#0]\n'),
  ['events[0].lr==fixture_output.lr&&events[0].lr!=0']),
 ('truncate-post-capture',once(entry,'    mov x1,x0\n','    mov w1,w0\n'),post_expected),
 ('truncate-return',once(entry,'    ldp x0,x1,[sp,#0]\n','    ldp x0,x1,[sp,#0]\n    mov w0,w0\n',1),return_expected),
 ('omit-post-errno-restore',once(entry,'    str w10,[x9]\n','    nop\n',1),
  ['errno==73']*2+['errno==73&&native_seen_errno==38','unmap_result==0&&errno==75','thread_errors==0']),
 ('nonzero-initial-context',once(entry,'    stp xzr,xzr,[sp,#400]\n','    stp xzr,xzr,[sp,#400]\n    mov x9,#1\n    str x9,[sp,#408]\n'),
  ['zero_errors==0']),
]
verified=[];logs=[]
for name,source,expected in controls:
    (out/(name+'.S')).write_text(source)
    rr=docker('g++ -std=c++17 -O2 -g -Wall -Wextra -Werror -fexceptions -fasynchronous-unwind-tables -pthread /out/'+name+'.S /src/test_fixture.S /src/test_abi.cpp -o /out/'+name+' && /out/'+name)
    tt=retain(rr,name);v=assert_run(rr,expected)
    v.update({'name':name,'source_sha256':sha(out/(name+'.S')),
              'output_sha256':hashlib.sha256(tt.encode()).hexdigest()})
    verified.append(v);logs.append('\nControl: '+name+'\n'+tt)
    print(name+': exact '+str(len(expected))+' failures',flush=True)

dis=(out/'entry-disassembly.txt').read_text()
cfi=(out/'entry-cfi.txt').read_text()
rel=(out/'entry-relocations.txt').read_text()
instructions=[(m.group(1),m.group(2).strip()) for m in re.finditer(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(\S+)\s*(.*)$',dis,re.M)]
assert len(instructions)==107,instructions
assert [i for i in instructions if i[0] in ('bl','blr','br','ret')]==[
 ('bl','0 <__errno_location>'),('bl','0 <az_task_call_before>'),('blr','x16'),
 ('bl','0 <az_task_call_after>'),('ret','')]
assert instructions[0]==('sub','sp, sp, #0x1a0')
assert instructions[-2]==('add','sp, sp, #0x1a0')
assert 'az_task_call_native_return' in dis
assert all(s in rel for s in ('__errno_location','az_task_call_before','az_task_call_after','az_task_call_original'))
# After native return, reads are confined to the wrapper stack and saved errno address.
return_body=entry.split('az_task_call_native_return:\n',1)[1]
for line in return_body.splitlines():
    if re.match(r'\s*(?:ldp|ldr|ldar)\s',line):
        assert re.search(r'\[(?:sp|x9)(?:,|\])',line),line
assert 'add x0,sp,#352\n    bl az_task_call_after' in return_body

def valid_cfi(text):
    return all(s in text for s in ('pc=0000000000000000..00000000000001ac',
      'DW_CFA_def_cfa_offset: 416','r30 (x30) at cfa-264',
      'DW_CFA_restore: r30 (x30)','DW_CFA_def_cfa_offset: 0'))
assert valid_cfi(cfi)
cfi_controls=[]
for name,source in [
 ('missing-lr-rule',entry.replace('.cfi_offset 30,-264\n','')),
 ('wrong-frame-rule',entry.replace('.cfi_def_cfa_offset 416\n','.cfi_def_cfa_offset 400\n')),
]:
    (out/(name+'.S')).write_text(source)
    rr=docker('g++ -g -fPIC -c /out/'+name+'.S -o /out/'+name+'.o && readelf --debug-dump=frames /out/'+name+'.o')
    tt=retain(rr,name)
    assert rr.returncode==0,tt
    assert not valid_cfi(tt),'wrong CFI accepted'
    cfi_controls.append({'name':name,'object_compiled':True,'object_executed':False,
                         'metadata_audit_rejected':True,'source_sha256':sha(out/(name+'.S'))})
    print(name+': metadata audit rejected (not executed)',flush=True)
report.update({'negative_controls':verified,'cfi_rejection_controls':cfi_controls,
 'instruction_count':107,'instruction_audit_passed':True,'cfi_audit_passed':True,
 'context_bytes':64,'private_stack_frame_bytes':416,'normal_return_only_after':True,
 'native_exception_calls':3,'native_exception_after_calls':0,'exception_destructors':4,
 'concurrent_calls':64,'concurrent_threads':4,
 'synthetic_task_unmapped_before_return':True,
 'known_target_abi':'two integer registers in, full x0 out; no stack argument forwarding',
 'firmware_entry_intended':'0x237a440 (not called or hooked by this test)',
 'native_owner_lifetime_or_installation_proven':False,
 'hardware_timing_measured':False})
(HERE/'test-result.json').write_text(json.dumps(report,indent=2)+'\n')
(HERE/'test-results.txt').write_text(text+''.join(logs))
(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
for n in ('entry-disassembly.txt','entry-cfi.txt','entry-relocations.txt'):
    (HERE/n).write_text((out/n).read_text().replace('/out','<OUTPUT>'))
assert {n:sha(HERE/n) for n in names}==hashes,'source changed during run'

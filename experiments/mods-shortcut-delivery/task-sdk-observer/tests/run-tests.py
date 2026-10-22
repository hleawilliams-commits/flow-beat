#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fresh-process actual adapter tests; no firmware instructions or device access."""
import argparse,hashlib,json,os,re,subprocess,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
PIN='5b0f5f3'
UPSTREAM='3e0c5cb010454228f4ee63226b60d060494eeaae'
SCENARIOS=['env-missing','env-wrong','run-zero','run-sign','run-space','run-tail','run-overflow','run-long',
 'sdk-null-core','sdk-abi','sdk-null-fw','sdk-null-claim','sdk-null-thread','sdk-negative','sdk-short','sdk-null-log',
 'firmware-null','firmware-wrong','static-1','static-2','static-3','static-4','thread-failure','thread-fatal',
 'first-failure','second-failure','first-empty','second-empty','first-worker-fatal','second-worker-fatal',
 'gates','worker','suppressed-log','budget','fatal-worker','logging-limits','reentry','hole','concurrent','unmap','run-max-valid']
def sha(b):return hashlib.sha256(b.encode() if isinstance(b,str) else b).hexdigest()
def once(s,a,b):assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sdk',type=Path,default=ROOT.parents[3]/'az-mods-sdk');ap.add_argument('--result',type=Path,default=ROOT/'host-result.json');ap.add_argument('--explore-controls',action='store_true');a=ap.parse_args()
 repo=ROOT.parents[2];pin=subprocess.check_output(['git','rev-parse',PIN],cwd=repo,text=True).strip()
 upstream=ROOT.parents[5]/'work/xdj-az-mods'
 sdk=subprocess.check_output(['git','show',UPSTREAM+':src/azmod/azmod-mod.h'],cwd=upstream)
 assert sdk==(a.sdk/'include/azmod-mod.h').read_bytes()
 paths=['observer.cpp','mod.c','tests/test_observer.cpp','tests/run-tests.py']
 bindings={'deps/task-capture-contract/capture.h':'task-capture-contract/capture.h',
  'deps/task-value-transport/transport.h':'task-value-transport/transport.h',
  'deps/task-call-return-abi/context.h':'task-call-return-abi/context.h',
  'abi/submission-entry.S':'task-observer-integration/submission-entry.S','abi/executor-entry.S':'task-observer-integration/executor-entry.S'}
 inputs={p:(ROOT/p).read_bytes() for p in paths}
 for local,original in bindings.items():
  old=subprocess.check_output(['git','show',pin+':experiments/mods-shortcut-delivery/'+original],cwd=repo)
  assert old==(ROOT/local).read_bytes(),local
  inputs[local]=old
 source=inputs['observer.cpp'].decode()
 second='!__atomic_load_n(&az_task_exec_original,__ATOMIC_ACQUIRE))return fail(-21);'
 controls={
  'clear-retained-original':(once(source,second,'!__atomic_load_n(&az_task_exec_original,__ATOMIC_ACQUIRE)){az_task_call_original=nullptr;return fail(-21); }'),'second-failure'),
  'gate-late-after':(once(source,'auto* t=eto::published.load(std::memory_order_acquire);if(t)t->finish(c,result);','if(eto::state.load()!=eto::Ready)return;\n    auto* t=eto::published.load(std::memory_order_acquire);if(t)t->finish(c,result);'),'gates'),
  'consume-unpublished-hole':(once(source,'if(!w.entry_copied[i]&&t->read_entry(i,c))','if(!w.entry_copied[i]&&(w.entry_copied[i]=true,t->read_entry(i,c)))'),'hole'),
  'retry-copied-entry-log':(once(source,'w.entry_copied[i]=true;++w.entry_count;++w.log_attempts;','w.entry_copied[i]=false;++w.entry_count;++w.log_attempts;'),'suppressed-log'),
  'tid-before-caller-gate':(once(once(source,'if(lr!=(phase==TC_SUBMISSION?TC_SUBMIT_LR:TC_EXECUTE_LR))','const unsigned early_tid=current_tid();\n    if(lr!=(phase==TC_SUBMISSION?TC_SUBMIT_LR:TC_EXECUTE_LR))'),'const unsigned tid=current_tid();','const unsigned tid=early_tid;'),'gates')}
 expected={
  'clear-retained-original':['failure/first-original-retained'],
  'gate-late-after':['helper/late-value-only-after-close','helper/duplicate-no-rewrite'],
  'consume-unpublished-hole':['worker/skips-unpublished-hole','worker/revisits-hole-after-close'],
  'retry-copied-entry-log':['worker/copied-records-not-retried'],
  'tid-before-caller-gate':['helper/wrong-caller-no-reads']}
 out=ROOT/'out-history'/str(uuid.uuid4());out.mkdir(parents=True)
 (out/'sdk').mkdir();(out/'sdk/azmod-mod.h').write_bytes(sdk)
 manifest={p:sha(b) for p,b in inputs.items()};(out/'inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
 report={'scope':'Actual adapter C++ source, mocked SDK/static memory/TID/sleep/log and direct helper entry, one process per init scenario. No native wrapper execution, firmware/device access, timing or installed-hook/lifetime proof.',
  'base_commit':pin,'upstream_commit':UPSTREAM,'sdk_header_sha256':sha(sdk),'source_sha256':manifest,'modes':{},'controls':{}}
 for variant in ['candidate',*controls]:
  d=out/variant
  for p,b in inputs.items():q=d/p;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(b)
  if variant!='candidate':(d/'observer.cpp').write_text(controls[variant][0])
  for mode,flags in [('plain',[]),('asan-ubsan',['-fsanitize=address,undefined','-fno-sanitize-recover=all']),('tsan',['-fsanitize=thread'])]:
   exe=d/mode
   command=[os.environ.get('CXX','clang++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fno-omit-frame-pointer','-pthread',*flags,'-I'+str(out/'sdk'),str(d/'tests/test_observer.cpp'),'-o',str(exe)]
   built=subprocess.run(command,capture_output=True,text=True);(d/(mode+'-compile.log')).write_text(built.stdout+built.stderr)
   if built.returncode:print(built.stdout+built.stderr,flush=True)
   built.check_returncode();results={}
   for scenario in (SCENARIOS if variant=='candidate' else [controls[variant][1]]):
    r=subprocess.run([str(exe),scenario],capture_output=True,text=True,timeout=60);output=r.stdout+r.stderr;(d/(mode+'-'+scenario+'.log')).write_text(output)
    assert not re.search(r'Sanitizer|runtime error:|DEADLYSIGNAL',output),(str(d),output)
    found=re.findall(r'^FAIL (.*)$',r.stderr,re.M);m=re.search(r'^scenario=(.+) checks=(\d+) failures=(\d+)$',r.stdout,re.M)
    assert m and m[1]==scenario and int(m[3])==len(found),(str(d),output,r.returncode)
    if variant=='candidate':assert r.returncode==0 and not found,(str(d),output)
    else:
     assert r.returncode==1 and found,(str(d),output)
     if not a.explore_controls:assert found==expected[variant],(variant,found,output)
    results[scenario]={'checks':int(m[2]),'failures':found,'output_sha256':sha(output)}
   if variant=='candidate':report['modes'][mode]={'assertions':sum(v['checks'] for v in results.values()),'scenarios':results};print(mode+': '+str(report['modes'][mode]['assertions'])+' checks / '+str(len(results))+' scenarios',flush=True)
   else:report['controls'].setdefault(variant,{})[mode]=results;print(variant+'-'+mode+': '+str(next(iter(results.values()))['failures']),flush=True)
 assert {p:sha((ROOT/p).read_bytes()) for p in inputs}==manifest,'source drift during run'
 payload=json.dumps(report,indent=2)+'\n';(out/'result.json').write_text(payload)
 if not a.explore_controls:a.result.write_text(payload)
 print('snapshot='+str(out),flush=True)
if __name__=='__main__':main()

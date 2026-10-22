#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host-only value capture/lifecycle fixture; no native device dependency."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid

HERE=Path(__file__).resolve().parent
PIN='70fb9d4d3aaffe04ceda5164d5bb2c4a93b759d9'
def sha(v): return hashlib.sha256(v.encode() if isinstance(v,str) else v).hexdigest()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--result',type=Path,required=True)
    a=ap.parse_args()
    repo=HERE.parents[2]
    binding_path='experiments/mods-shortcut-delivery/route-task-ownership/taskbox-verifier.py'
    binding=subprocess.check_output(['git','show',PIN+':'+binding_path],cwd=repo,text=True)
    blocks=next(ast.literal_eval(n.value) for n in ast.parse(binding).body
                if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='BLOCKS' for t in n.targets))
    entry_start,entry_body=blocks['entry_calls_post']
    execute_start,execute_body=blocks['drain_execute']
    submit_lr=entry_start+4*(entry_body.splitlines().index('blr x23')+1)
    execute_lr=execute_start+4*(execute_body.splitlines().index('blr x1')+1)
    header=(HERE/'capture.h').read_text(); test=(HERE/'test-capture.c').read_text()
    assert submit_lr==0x1b01e04 and execute_lr==0x237d800
    assert '#define TC_SUBMIT_LR UINT64_C(0x1b01e04)' in header
    assert '#define TC_EXECUTE_LR UINT64_C(0x237d800)' in header
    prior=json.loads(subprocess.check_output(['git','show',PIN+':experiments/mods-shortcut-delivery/route-task-ownership/static-result.json'],cwd=repo,text=True))
    assert prior['task']['vptr']=='0x2d444d8' and '#define TC_TASK_VPTR UINT64_C(0x2d444d8)' in header
    out=HERE/'out-history'/str(uuid.uuid4());out.mkdir(parents=True)
    (out/'taskbox-binding.py').write_text(binding)
    variants={
        'candidate':header,
        'truncated-return':header.replace('out.native_result=native_result;','out.native_result=(uint32_t)native_result;'),
        'dispatcher-overread':header.replace('c->status=TC_COPIED;',
            'TC_READ_BYTES(&vptr,p+0x18,8); /* strict control */\n    c->status=TC_COPIED;')}
    assert len(set(variants.values()))==3
    expected={'candidate':[], 'truncated-return':['exact-values/full-width-return'],
              'dispatcher-overread':['exact-values/exact-five-read-ranges','all-reads/no-dispatcher-tag-or-padding-read']}
    report={'scope':'Host-only owned synthetic storage; serial scripted invocation order, mock read accounting, real unmap before return; no native lifetime/concurrency/ARM64/runtime proof',
            'prior_static_binding_commit':PIN,'prior_verifier_sha256':sha(binding),
            'reused_caller_LRs':[hex(submit_lr),hex(execute_lr)],
            'header_sha256':sha(header),'test_sha256':sha(test),
            'runner_sha256':sha(Path(__file__).read_bytes()),'runs':[]}
    for name,body in variants.items():
        directory=out/name;directory.mkdir(); (directory/'capture.h').write_text(body)
        c=directory/'test-capture.c';c.write_text(test)
        for mode,flags in [('plain',[]),('asan-ubsan',['-fsanitize=address,undefined']),('tsan',['-fsanitize=thread'])]:
            exe=directory/mode
            built=subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fno-omit-frame-pointer',*flags,str(c),'-o',str(exe)],capture_output=True,text=True)
            (directory/(mode+'-compile.txt')).write_text(built.stdout+built.stderr)
            if built.returncode: print(built.stderr,flush=True)
            built.check_returncode()
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            output=r.stdout+r.stderr; (directory/(mode+'.txt')).write_text(output)
            print(name+'-'+mode+': '+output,end='',flush=True)
            assert not re.search(r'Sanitizer|runtime error:|DEADLYSIGNAL',output),str(out)
            actual=re.findall(r'^FAIL (.+)$',r.stderr,re.M)
            result=re.search(r'^checks=(\d+) failures=(\d+) scenarios=(\d+)$',r.stdout,re.M)
            assert result and int(result[1])==67 and int(result[3])==6 and actual==expected[name] and int(result[2])==len(actual) and r.returncode==bool(actual),str(out)
            report['runs'].append({'variant':name,'mode':mode,'checks':67,'scenarios':6,
                                  'expected_failures':actual,'output_sha256':sha(output)})
    payload=json.dumps(report,indent=2)+'\n';(out/'result.json').write_text(payload);a.result.write_text(payload)

if __name__=='__main__': main()

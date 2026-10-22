#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline immutable transport fixture, actual pinned capture header, no native runtime."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid

HERE=Path(__file__).resolve().parent
PIN='5213910'
CAPTURE_PATH='experiments/mods-shortcut-delivery/task-capture-contract/capture.h'
CAPTURE_SHA='aa9b82311065fa3481cda29883f6dae40d1685c213d520271e1f4530039291ea'
def sha(v): return hashlib.sha256(v.encode() if isinstance(v,str) else v).hexdigest()
def replace_once(text,old,new):
    assert text.count(old)==1,old
    return text.replace(old,new)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--result',type=Path,required=True);a=ap.parse_args()
    repo=HERE.parents[2]
    pin=subprocess.check_output(['git','rev-parse',PIN],cwd=repo,text=True).strip()
    capture=subprocess.check_output(['git','show',pin+':'+CAPTURE_PATH],cwd=repo)
    assert capture==(HERE.parent/'task-capture-contract/capture.h').read_bytes()
    assert sha(capture)==CAPTURE_SHA
    header=(HERE/'transport.h').read_text();test=(HERE/'test-transport.cpp').read_text()
    assert header.count('claimed_.compare_exchange_strong(')==1
    variants={
        'candidate':header,
        'entry-published-before-copy':replace_once(header,'TVT_TEST_POINT(HookPoint::BeforeEntryCopy,this,index);',
            'slot.entry_ready.store(1,std::memory_order_release); /* strict control */\n        TVT_TEST_POINT(HookPoint::BeforeEntryCopy,this,index);'),
        'return-published-before-copy':replace_once(header,'TVT_TEST_POINT(HookPoint::BeforeReturnCopy,this,index);',
            'slot.return_state.store(ReturnReady,std::memory_order_release); /* strict control */\n        TVT_TEST_POINT(HookPoint::BeforeReturnCopy,this,index);'),
        'duplicate-return-rewrite':replace_once(header,'if (!slot.return_state.compare_exchange_strong(',
            'if (false && !slot.return_state.compare_exchange_strong('),
        'truncated-return':replace_once(header,'slot.returned=tc_return(&entry,native_result);',
            'slot.returned=tc_return(&entry,static_cast<std::uint32_t>(native_result));')}
    expected={
        'candidate':[],
        'entry-published-before-copy':['control/entry-must-wait-for-copy'],
        'return-published-before-copy':['control/return-must-wait-for-copy'],
        'duplicate-return-rewrite':['control/duplicate-rejected','control/duplicate-cannot-rewrite'],
        'truncated-return':['control/full-result','control/duplicate-cannot-rewrite']}
    out=HERE/'out-history'/str(uuid.uuid4());out.mkdir(parents=True)
    inputs={'base_commit':pin,'capture_sha256':sha(capture),'transport_sha256':sha(header),'test_sha256':sha(test),'runner_sha256':sha(Path(__file__).read_bytes())}
    (out/'inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
    (out/'run-tests.py').write_bytes(Path(__file__).read_bytes())
    report={'scope':'Offline C++17 immutable pointer-free transport with actual pinned capture header; candidate concurrent schedules and serial strict controls. No native provenance, firmware execution, hook integration, unload, wall-clock, whole-process race or lifetime proof.',
            **inputs,'runs':[]}
    for name,body in variants.items():
        directory=out/name;directory.mkdir();src=directory/'task-value-transport';src.mkdir()
        cap=directory/'task-capture-contract';cap.mkdir();(cap/'capture.h').write_bytes(capture)
        (src/'transport.h').write_text(body);(src/'test-transport.cpp').write_text(test)
        for mode,flags in [('plain',[]),('asan-ubsan',['-fsanitize=address,undefined']),('tsan',['-fsanitize=thread'])]:
            exe=directory/mode
            cmd=[os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fno-omit-frame-pointer','-pthread',*flags,str(src/'test-transport.cpp'),'-o',str(exe)]
            built=subprocess.run(cmd,capture_output=True,text=True)
            (directory/(mode+'-compile.txt')).write_text(built.stdout+built.stderr)
            if built.returncode: print(built.stderr,flush=True)
            built.check_returncode()
            r=subprocess.run([str(exe)]+([] if name=='candidate' else ['--serial-controls']),capture_output=True,text=True,timeout=60)
            output=r.stdout+r.stderr;(directory/(mode+'.txt')).write_text(output)
            print(name+'-'+mode+': '+output,end='',flush=True)
            actual=re.findall(r'^FAIL (.+)$',r.stderr,re.M)
            result=re.search(r'^checks=(\d+) failures=(\d+) scenarios=(\d+)$',r.stdout,re.M)
            assert not re.search(r'Sanitizer|runtime error:|DEADLYSIGNAL',output),str(out)
            assert result and actual==expected[name] and int(result[2])==len(actual) and r.returncode==bool(actual),str(out)
            assert int(result[3])==(9 if name=='candidate' else 1)
            report['runs'].append({'variant':name,'mode':mode,'checks':int(result[1]),'scenarios':int(result[3]),'expected_failures':actual,'output_sha256':sha(output),'compiled_header_sha256':sha(body)})
    candidate_counts={r['checks'] for r in report['runs'] if r['variant']=='candidate'}
    assert len(candidate_counts)==1
    payload=json.dumps(report,indent=2)+'\n';(out/'result.json').write_text(payload);a.result.write_text(payload)
    print('snapshot='+str(out),flush=True)
if __name__=='__main__':main()

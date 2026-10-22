#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unchanged MODS 0.1.3 with actual pinned SDK registration/dispatch/strikes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid

UPSTREAM = '3e0c5cb010454228f4ee63226b60d060494eeaae'
MOD = '321b955b8eefb5470f0e0e636e3bb08ebc520c56'
HERE = Path(__file__).resolve().parent

def sha(s):
    return hashlib.sha256(s.encode() if isinstance(s,str) else s).hexdigest()

def git(repo, pin, path):
    return subprocess.check_output(['git','show',pin+':'+path],cwd=repo,text=True)

def function(source, name):
    m=re.search(r'^static [^\n]*\b'+name+r'\([^;]*?\)\s*\{',source,re.M)
    assert m, name
    start=source.index('{',m.start()); depth=0
    for t in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]',source[start:],re.S):
        if t.group()=='{': depth+=1
        elif t.group()=='}':
            depth-=1
            if not depth: return source[m.start():start+t.end()]+'\n'
    raise RuntimeError(name)

SHIM='''#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "azmod-mod.h"
typedef struct { int state; } cm_mod;
#define AZCM_LOADED 1
#define CM_RA() ((void *)0)
static cm_mod owner;
static int g_sdk_in_init, g_cm_in_frame;
static uint64_t g_sdk_budget_ticks;
static uint64_t sdk_ticks(void);
static void cm_logf(cm_mod *,int,const char *,...);
static cm_mod *cm_self(const char *id,void *ra,const char *service) {
    (void)ra; (void)service;
    return !strcmp(id,"mods-shortcut") ? &owner : NULL;
}
'''

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--upstream',required=True,type=Path)
    p.add_argument('--result',required=True,type=Path)
    a=p.parse_args()
    repo=HERE.parents[1]
    sdk=git(a.upstream,UPSTREAM,'src/azmod/sdk_input.c')
    header=git(a.upstream,UPSTREAM,'src/azmod/azmod-mod.h')
    states=git(a.upstream,UPSTREAM,'src/azmod/azcm.h')
    shortcut=git(repo,MOD,'mods-shortcut/shortcut.h')
    manifest=json.loads(git(repo,MOD,'mods-shortcut/azmod.json'))
    assert manifest['version']=='0.1.3'
    assert re.search(r'AZCM_LOADED\s*=\s*1',states)
    assert re.search(r'#define AZ_BUS_MIXER_IN\s+3\b',header)
    declarations=sdk[sdk.index('#define SDK_FILTER_MAX'):sdk.index('struct sdk_touch_filter')]
    funcs={n:function(sdk,n) for n in ['sdk_strike','cm_frame_filter','sdk_filter_run']}
    extracted=declarations+'\n'+''.join(funcs.values())
    test=(HERE/'test-lanes.c').read_text()
    out=HERE/'out-history'/str(uuid.uuid4()); out.mkdir(parents=True)
    for name,data in {'sdk_input.c':sdk,'azmod-mod.h':header,'shortcut.h':shortcut,
                      'extracted.c':extracted,'test-lanes.c':test,'shim.c':SHIM}.items():
        (out/name).write_text(data)
    c=out/'fixture.c'; c.write_text(SHIM+extracted+test)
    report={'upstream_commit':UPSTREAM,'mod_commit':MOD,'mod_version':'0.1.3',
            'sdk_input_sha256':sha(sdk),'sdk_header_sha256':sha(header),
            'state_header_sha256':sha(states),'shortcut_sha256':sha(shortcut),
            'extraction_sha256':sha(extracted),'functions':{n:sha(v) for n,v in funcs.items()},
            'fixture_sha256':sha(c.read_bytes()),'test_sha256':sha(test),'shim_sha256':sha(SHIM),
            'runner_sha256':sha(Path(__file__).read_bytes()),
            'scope':'serial scripted UI observations between filter calls, monotonic mock ticks and stub host services; no concurrent/ARM64/emulator/hardware execution; unchanged production bodies',
            'runs':[]}
    for mode,flags in [('plain',[]),('asan-ubsan',['-fsanitize=address,undefined']),('tsan',['-fsanitize=thread'])]:
        for variant,defines in [('observed',[]),('authoritative-hypothesis',['-DASSERT_AUTHORITATIVE'])]:
            exe=out/(variant+'-'+mode)
            built=subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fno-omit-frame-pointer',*flags,*defines,str(c),'-o',str(exe)],capture_output=True,text=True)
            (out/(exe.name+'-compile.txt')).write_text(built.stdout+built.stderr)
            if built.returncode: print(built.stderr,flush=True)
            built.check_returncode()
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            output=r.stdout+r.stderr; (out/(exe.name+'.txt')).write_text(output)
            print(exe.name+': '+output,end='',flush=True)
            assert not re.search(r'Sanitizer|runtime error:|DEADLYSIGNAL',output),str(out)
            expected=[] if variant=='observed' else ['split-frame/local-off-implies-core-quarantine']
            actual=re.findall(r'^FAIL (.+)$',r.stderr,re.M)
            result=re.search(r'^checks=(\d+) failures=(\d+) scenarios=4$',r.stdout,re.M)
            assert result and int(result[1])==82+len(expected) and actual==expected and int(result[2])==len(expected) and r.returncode==bool(expected),str(out)
            report['runs'].append({'mode':mode,'variant':variant,'checks':int(result[1]),
                'failures':len(expected),'expected_failures':expected,'output_sha256':sha(output)})
    payload=json.dumps(report,indent=2)+'\n'
    (out/'result.json').write_text(payload); a.result.write_text(payload)

if __name__=='__main__': main()

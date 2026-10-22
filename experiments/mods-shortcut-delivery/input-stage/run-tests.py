#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual pinned SDK filter-chain observability; no runtime or device imports."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid

PIN = '3e0c5cb010454228f4ee63226b60d060494eeaae'
HERE = Path(__file__).resolve().parent

def sha(s):
    return hashlib.sha256(s.encode() if isinstance(s, str) else s).hexdigest()

def function(source, name):
    m = re.search(r'^static [^\n]*\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    assert m, name
    depth = 0
    for t in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]', source[source.index('{',m.start()):], re.S):
        if t.group()=='{': depth+=1
        elif t.group()=='}':
            depth-=1
            if not depth: return source[m.start():source.index('{',m.start())+t.end()]+'\n'
    raise RuntimeError(name)

SHIM = '''#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { int state; } cm_mod;
typedef int (*az_frame_fn)(int,unsigned char*,int,void*);
#define AZCM_LOADED 1
#define AZ_FILTER_RAW (1u<<31)
#define AZ_FRAME_EATEN 1
static int g_cm_in_frame, logged;
static uint64_t mock_tick, g_sdk_budget_ticks;
static uint64_t sdk_ticks(void) { return mock_tick; }
static void cm_logf(cm_mod *c,int force,const char *fmt,...) { (void)c; (void)force; (void)fmt; logged++; }
'''

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--upstream',required=True,type=Path)
    ap.add_argument('--result',required=True,type=Path)
    a=ap.parse_args()
    source=subprocess.check_output(['git','show',PIN+':src/azmod/sdk_input.c'],cwd=a.upstream,text=True)
    bus=subprocess.check_output(['git','show',PIN+':src/azmod/bus_interpose.c'],cwd=a.upstream,text=True)
    declarations=source[source.index('#define SDK_FILTER_MAX'):source.index('struct sdk_touch_filter')]
    names=['sdk_strike','sdk_filter_run','sdk_filter_copy','sdk_filter_raw']
    functions={n:function(source,n) for n in names}
    extracted=declarations+'\n'+''.join(functions.values())
    # Bind the boundary explanation to actual caller ordering and return use.
    frame=function(bus,'bus_mixer_frame')
    chain=['if (r >= 98 && !crc_in)','sdk_filter_raw(', 'mixer_browse_knob(', 'b[34] = knob_anchor', 'sdk_filter_run(']
    positions=[frame.index(s) for s in chain]
    assert positions==sorted(positions)
    assert 'if (crc_in && sdk_filter_run(AZ_BUS_MIXER_IN, b, (int) r))' in frame
    assert 'frame_crc_stamp(b, 96);' in frame[positions[-1]:]
    # Both symbolic values match the pinned public SDK header, not guessed ABI values.
    header=subprocess.check_output(['git','show',PIN+':src/azmod/azmod-mod.h'],cwd=a.upstream,text=True)
    states=subprocess.check_output(['git','show',PIN+':src/azmod/azcm.h'],cwd=a.upstream,text=True)
    assert re.search(r'#define AZ_FILTER_RAW\s+0x80000000u',header)
    assert re.search(r'#define AZ_FRAME_EATEN\s+1',header)
    assert re.search(r'#define AZ_BUS_MIXER_IN\s+3\b',header)
    assert re.search(r'AZCM_LOADED\s*=\s*1',states)
    out=HERE/'out-history'/str(uuid.uuid4()); out.mkdir(parents=True)
    test=(HERE/'test-chain.c').read_text()
    (out/'extracted.c').write_text(extracted)
    (out/'sdk_input.c').write_text(source)
    (out/'bus_mixer_frame.c').write_text(frame)
    variants={'pinned':extracted,'raw-copy-leak':extracted.replace('memcpy(copy, f, (size_t) m); /* the next filter sees the frame as it arrived */','/* strict control: omitted per-filter copy reset */')}
    assert variants['pinned']!=variants['raw-copy-leak']
    report={'upstream_commit':PIN,'source_sha256':sha(source),'bus_frame_sha256':sha(frame),
            'header_sha256':sha(header),'states_header_sha256':sha(states),'extraction_sha256':sha(extracted),'functions':{n:sha(s) for n,s in functions.items()},
            'test_sha256':sha(test),'runner_sha256':sha(Path(__file__).read_bytes()),'shim_sha256':sha(SHIM),
            'scope':'serial actual SDK dispatch with mock timing/logging and manually populated registration table; no runtime, concurrency, hardware timing or native dispatch proof','runs':[]}
    for mode, flags in [('plain',[]),('asan-ubsan',['-fsanitize=address,undefined']),('tsan',['-fsanitize=thread'])]:
        for variant,body in variants.items():
            c=out/(variant+'.c'); c.write_text(SHIM+body+test)
            exe=out/(variant+'-'+mode)
            compiled=subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fno-omit-frame-pointer',*flags,str(c),'-o',str(exe)],capture_output=True,text=True)
            (out/(exe.name+'-compile.txt')).write_text(compiled.stdout+compiled.stderr)
            compiled.check_returncode()
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            output=r.stdout+r.stderr; (out/(exe.name+'.txt')).write_text(output)
            print(exe.name+': '+output,end='',flush=True)
            assert not re.search(r'Sanitizer|runtime error:|DEADLYSIGNAL',output),str(out)
            actual=re.findall(r'^FAIL (.+)$',r.stderr,re.M)
            expected=[] if variant=='pinned' else ['raw-copy/next-copy-reset']
            result=re.search(r'^checks=(\d+) failures=(\d+)$',r.stdout,re.M)
            assert result and int(result[1])==60 and actual==expected and int(result[2])==len(expected) and r.returncode==bool(expected),str(out)
            report['runs'].append({'mode':mode,'variant':variant,'checks':int(result[1]),'failures':len(expected),'expected_failures':expected,'output_sha256':sha(output)})
    payload=json.dumps(report,indent=2)+'\n'; (out/'result.json').write_text(payload); a.result.write_text(payload)

if __name__=='__main__': main()

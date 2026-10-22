#!/usr/bin/env python3
"""Build actual observer.cpp in isolated snapshots; no device or network access."""
import argparse, hashlib, json, os, re, shutil, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
EXPECTED={'budget': 281, 'budget-off': 281, 'burst-capacity': 284, 'burst-collision': 55, 'burst-concurrent': 168, 'burst-holes': 48, 'burst-last-collision': 32, 'burst-reentry': 39, 'burst-saturated': 40, 'burst-visibility': 30, 'claim': 22, 'concurrent': 16025, 'diagnostics': 83, 'empty': 23, 'env': 4, 'errno': 61, 'firmware': 4, 'heartbeat': 32, 'lanes-concurrent': 14025, 'native-gates': 31, 'nested': 28, 'normal': 97, 'prologue': 3, 'raw-budget': 154, 'raw-diagnostics': 44, 'raw-gates': 70, 'raw-nested': 30, 'raw-overflow': 98, 'raw-partial': 37, 'raw-values': 59, 'registration': 8, 'sdk': 12, 'thread': 30, 'thread-negative': 28, 'thread-zero': 28, 'tid-normalization': 30, 'worker': 15}
DIAGNOSTIC=re.compile(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|runtime error:|Sanitizer:DEADLYSIGNAL')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(cmd, directory, receipt, expected=0):
    p=subprocess.run(cmd,cwd=directory,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
    receipt.write_text(p.stdout)
    if DIAGNOSTIC.search(p.stdout):raise RuntimeError('sanitizer diagnostic: '+str(receipt))
    if p.returncode!=expected:raise RuntimeError(f'rc={p.returncode}, wanted {expected}: {receipt}')
    return p.stdout

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--sdk',type=Path,default=ROOT.parents[3]/'az-mods-sdk')
    ap.add_argument('--result',type=Path,default=ROOT/'host-result.json');args=ap.parse_args()
    sdk=args.sdk.resolve(); header=sdk/'include/azmod-mod.h'
    assert header.exists()
    history=ROOT/'out-history';history.mkdir(exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='host-',dir=history))
    (work/'tests').mkdir();shutil.copyfile(ROOT/'observer.cpp',work/'observer.cpp')
    shutil.copyfile(ROOT/'tests/test_observer.cpp',work/'tests/test_observer.cpp')
    freeze=json.loads((ROOT/'frozen-parent.json').read_text())
    for f,v in freeze['tracked_files_sha256'].items():
        assert sha(ROOT.parent/'route-observer'/f)==v, 'frozen parent drift: '+f
    source=(work/'observer.cpp').read_text()
    increment='    erb::native_attempted.fetch_add(1,std::memory_order_relaxed);\n'
    assert source.count(increment)==1
    late=source.replace(increment,'').replace('    const int saved_errno=errno;',increment+'    const int saved_errno=errno;')
    needle='    const unsigned char values[]={frame[6],frame[31],frame[32],frame[33],frame[34],frame[35]};'
    assert source.count(needle)==1
    edits=source.replace(needle,'    frame[34]^=1; // deliberately broken negative control\n'+needle)
    early_needle='        owned.state.store(OFF_WRITING,std::memory_order_relaxed);'
    assert source.count(early_needle)==1
    early=source.replace(early_needle,early_needle+'\n        owned.state.store(OFF_READY,std::memory_order_release);')
    failed_needle='            off_contention_dropped.fetch_add(1,std::memory_order_relaxed); return;'
    assert source.count(failed_needle)==1
    failure_owned=source.replace(failed_needle,'            off_contention_dropped.fetch_add(1,std::memory_order_relaxed);')
    scan_needle='    for (unsigned slot=0;slot<OFF_CAPACITY;++slot)\n        if (pop_off_at(slot,r) && !log_record(w,r,slot+1)) return false;'
    assert source.count(scan_needle)==1
    holes=source.replace(scan_needle,'    for (unsigned slot=0;slot<OFF_CAPACITY;++slot) {\n        if (off_slots[slot].state.load()==OFF_WRITING) break;\n        if (pop_off_at(slot,r) && !log_record(w,r,slot+1)) return false;\n    }')
    reset_needle='    owned.state.store(OFF_CONSUMED,std::memory_order_release);'
    assert source.count(reset_needle)==1
    reset=source.replace(reset_needle,'    owned.state.store(OFF_EMPTY,std::memory_order_release);')
    controls={
      'late-attempt':(late,'native-gates','raw_attempted.load()==1 && native_attempted.load()==1'),
      'frame-write':(edits,'raw-values','!memcmp(before,f,sizeof f)'),
      'early-ready':(early,'burst-holes','!pop_off_at(0,r) && r.sequence==123 && r.x0==456'),
      'failed-cas-owned':(failure_owned,'burst-collision','off_slots[i].state.load()==OFF_EMPTY && !pop_off_at(i,r)'),
      'stop-at-hole':(holes,'burst-holes','records==1 && record_slots[0]==2 && record_sequences[0]==0'),
      'reset-consumed':(reset,'burst-capacity','off_slots[i].state.load()==OFF_CONSUMED && !pop_off_at(i,r)')}
    files=['observer.cpp','mod.c','Makefile','azmod.json','abi/entry.S','tests/test_observer.cpp','tests/run-tests.py','tests/run-focused.sh','frozen-parent.json']
    result={'mod_id':'encoder-route-burst-observer','source_sha256':{f:sha(ROOT/f) for f in files},
      'sdk_header_sha256':sha(header),'frozen_parent_files_verified':len(freeze['tracked_files_sha256']),'modes':{},'controls':{},
      'scope':'Actual observer source with SDK mocks; host ABI/TID and callback entry are simulated. No runtime installation.'}
    compiler=os.environ.get('TEST_CXX','clang++')
    common=[compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function',
      '-fno-omit-frame-pointer','-fexceptions','-fasynchronous-unwind-tables','-I'+str(header.parent),'-pthread']
    for mode,flags in [('plain',[]),('asan',['-fsanitize=address,undefined','-fno-sanitize-recover=all']),('tsan',['-fsanitize=thread'])]:
      binary=work/('test-'+mode)
      run(common+flags+[str(work/'tests/test_observer.cpp'),'-o',str(binary)],work,work/(mode+'-build.log'))
      actual={}
      for scenario,n in EXPECTED.items():
        output=run([str(binary),scenario],work,work/(mode+'-'+scenario+'.log'))
        assert output==f'{scenario}: {n} assertions passed\n',output
        actual[scenario]=n
      result['modes'][mode]={'assertions':sum(actual.values()),'scenarios':actual}
      for name,(mutant,scenario,expected) in controls.items():
        d=work/(mode+'-'+name);(d/'tests').mkdir(parents=True)
        (d/'observer.cpp').write_text(mutant);shutil.copyfile(work/'tests/test_observer.cpp',d/'tests/test_observer.cpp')
        b=d/'control';run(common+flags+[str(d/'tests/test_observer.cpp'),'-o',str(b)],d,d/'build.log')
        output=run([str(b),scenario],d,d/'run.log',expected=1)
        lines=output.splitlines();assert len(lines)==1 and re.fullmatch(r'line \d+: '+re.escape(expected),lines[0]),output
        result['controls'].setdefault(name,{})[mode]={'expected_failure':expected,'failures':1,'sanitizer_diagnostics':0}
      print(f'{mode}: {sum(actual.values())} assertions; 6 strict controls rejected')
    args.result.resolve().write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
if __name__=='__main__':main()

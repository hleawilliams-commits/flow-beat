#!/usr/bin/env python3
"""Build actual observer.cpp in isolated snapshots; no device or network access."""
import argparse, hashlib, json, os, re, shutil, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
EXPECTED={'env':4,'sdk':12,'firmware':4,'prologue':3,'registration':8,'worker':15,'claim':22,'empty':23,
'native-gates':31,'raw-gates':70,'raw-values':59,'raw-overflow':98,'raw-nested':30,'raw-partial':37,
'raw-budget':154,'raw-diagnostics':44,'lanes-concurrent':14025,'normal':97,'thread':30,'thread-zero':28,
 'thread-negative':28,'tid-normalization':30,'errno':61,'mailbox':41,'partial':36,'nested':28,
 'diagnostics':46,'heartbeat':32,'budget':153,'budget-off':153,'concurrent':16025}
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
    source=(work/'observer.cpp').read_text()
    increment='    ero::native_attempted.fetch_add(1,std::memory_order_relaxed);\n'
    assert source.count(increment)==1
    late=source.replace(increment,'').replace('    const int saved_errno=errno;',increment+'    const int saved_errno=errno;')
    needle='    const unsigned char values[]={frame[6],frame[31],frame[32],frame[33],frame[34],frame[35]};'
    assert source.count(needle)==1
    edits=source.replace(needle,'    frame[34]^=1; // deliberately broken negative control\n'+needle)
    controls={
      'late-attempt':(late,'native-gates','raw_attempted.load()==1 && native_attempted.load()==1'),
      'frame-write':(edits,'raw-values','!memcmp(before,f,sizeof f)')}
    files=['observer.cpp','mod.c','Makefile','azmod.json','abi/entry.S','tests/test_observer.cpp','tests/run-tests.py','tests/run-focused.sh']
    result={'mod_id':'encoder-route-observer','source_sha256':{f:sha(ROOT/f) for f in files},
      'sdk_header_sha256':sha(header),'modes':{},'controls':{},
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
      print(f'{mode}: {sum(actual.values())} assertions; 2 strict controls rejected')
    args.result.resolve().write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
if __name__=='__main__':main()

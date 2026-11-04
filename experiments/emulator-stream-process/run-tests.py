#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Real host IPC: unchanged Rust wire backend to retained C stream/model graph."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import signal
import subprocess
import uuid

if not __debug__:
    raise RuntimeError('optimized Python disables verification assertions')

HERE = Path(__file__).resolve().parent
PRODUCTION = ('claim.c', 'retained.c', 'broker_owner.c', 'wire.c',
              'guest_dispatch.c', 'graph.c', 'stream.c', 'stream_native.c')
MODES = {'plain': [], 'asan-ubsan': ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'],
         'tsan': ['-fsanitize=thread', '-fno-omit-frame-pointer']}
ENV_STRIP = ('ASAN_OPTIONS', 'TSAN_OPTIONS', 'UBSAN_OPTIONS', 'LSAN_OPTIONS',
             'DYLD_INSERT_LIBRARIES', 'DYLD_LIBRARY_PATH', 'LD_PRELOAD', 'LD_LIBRARY_PATH')
INPUTS = ('run-tests.py', 'prepare.py', 'peer.c', 'check.rs', 'parent-inputs.json',
          'controls.json', 'test-inventory.json')

def sha(data):
    return hashlib.sha256(data.encode() if isinstance(data,str) else data).hexdigest()

def save(path,value):
    path.write_text(json.dumps(value,sort_keys=True,indent=2)+'\n')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emu',type=Path,required=True)
    parser.add_argument('--result',type=Path)
    args=parser.parse_args()
    if args.result and args.result.exists():
        raise ValueError('result cannot overwrite evidence')
    run=HERE/'private'/('run-'+uuid.uuid4().hex)
    run.mkdir(parents=True)
    try:
        save(run/'attempt.json',{'status':'preparing'})
        captured={n:(HERE/n).read_bytes() for n in INPUTS}
        for n,data in captured.items():
            (run/n).write_bytes(data)
        save(run/'attempt.json',{'status':'preparing','source_sha256':{n:sha(d) for n,d in captured.items()}})
        for name in ENV_STRIP:
            os.environ.pop(name,None)
        pins=json.loads(captured['parent-inputs.json'])
        if len(pins)!=76:
            raise ValueError('frozen input inventory mismatch')
        frozen={n:(HERE.parent/n).read_bytes() for n in pins}
        for name,data in frozen.items():
            if sha(data)!=pins[name]:
                raise ValueError('frozen input drift: '+name)
            target=run/'frozen'/name
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(data)
        prepare=runpy.run_path(str(run/'prepare.py'))['prepare_sources']
        generated,rust,metadata=prepare(args.emu.resolve(),run,frozen)
        if len(generated)!=29 or len(rust)!=3:
            raise ValueError('generated source inventory mismatch')
        (run/'rust/check.rs').write_bytes(captured['check.rs'])
        controls=json.loads(captured['controls.json'])
        cases=json.loads(captured['test-inventory.json'])['cases']
        if cases!={'normal':1,'unknown':1,'lost-reply':1,'partial-request':1}:
            raise ValueError('selector inventory mismatch')
        if len(controls)!=3 or len({c['name'] for c in controls})!=3:
            raise ValueError('control inventory mismatch')
        variants=[]
        for c in controls:
            if c['file']!='stream.c' or c['case'] not in cases or len(c['expected_failures'])!=1:
                raise ValueError('unreviewed control')
            source=generated[c['file']].decode()
            if source.count(c['old'])!=1:
                raise ValueError('control must replace one source expression')
            variants.append((c,source.replace(c['old'],c['new']).encode()))
        env=os.environ.copy()
        cc=shutil.which(os.environ.get('CC','cc'))
        rustc=shutil.which(os.environ.get('RUSTC','rustc'))
        if not cc or not rustc:
            raise ValueError('host compilers missing')
        compilers={name:subprocess.check_output([exe,'--version'],env=env,text=True)
                   for name,exe in [('cc',cc),('rustc',rustc)]}
        save(run/'compilers.json',compilers)
        result={'source_sha256':{n:sha(v) for n,v in captured.items()},
                'frozen_inputs_sha256':pins,
                'generated_sha256':{n:sha(v) for n,v in generated.items()},
                'preparation':metadata,'compilers':compilers,
                'sanitizer_preload_environment_removed':list(ENV_STRIP),
                'compiles':[],'links':[],'runs':[],
                'scope':'Real host child and two unnamed POSIX socketpairs. Unchanged standalone Rust wire/producer/route modules invoke actual C stream owner/native wrappers and frozen graph/core over extracted driver model. Controller descriptors, syscalls, usercopy, RNG and kernel locks remain modeled. C modes instrument only C peer. Rust host is unsanitized. No parent selectors, full Cargo build, Linux ARM64 build, emulator/device endpoint, native custody, production supervisor, concurrency, death recovery, installation or runtime gate permission.'}
        def execute(cmd,log,build=False):
            # Fixture-only process group. A hard host timeout must not leave its
            # private C child behind; this abort is failure, never disposal proof.
            child=subprocess.Popen(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                   text=True,start_new_session=True)
            try:
                stdout,stderr=child.communicate(timeout=45)
            except subprocess.TimeoutExpired as error:
                def raw(value):
                    return value if isinstance(value,bytes) else (value or '').encode()
                log.write_bytes(raw(error.stdout)+raw(error.stderr))
                try:
                    os.killpg(child.pid,signal.SIGKILL)
                except ProcessLookupError:
                    pass
                stdout,stderr=child.communicate(timeout=5)
                log.write_text(stdout+stderr)
                raise RuntimeError('fixture process-group timeout/abort: '+str(log.relative_to(run)))
            proc=subprocess.CompletedProcess(cmd,child.returncode,stdout,stderr)
            output=proc.stdout+proc.stderr
            log.write_text(output)
            if build and (proc.returncode!=0 or output):
                raise RuntimeError('build diagnostic: '+str(log.relative_to(run)))
            return proc,output
        host=run/'host-check'
        proc,output=execute([rustc,'--edition=2021','-Dwarnings',str(run/'rust/check.rs'),
                             '-o',str(host)],run/'rust-compile.txt',True)
        result['compiles'].append({'mode':'plain-host','variant':'candidate','file':'check.rs',
                                  'source_sha256':sha(captured['check.rs']),
                                  'exit_code':proc.returncode,'output_sha256':sha(output)})
        includes=['-I'+str(run/'source')]
        for mode,flags in MODES.items():
            dest=run/mode
            dest.mkdir()
            base=[cc,'-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror',*flags,*includes]
            objects={}
            for name in PRODUCTION:
                obj=dest/(name+'.o')
                proc,output=execute(base+['-c',str(run/'source'/name),'-o',str(obj)],dest/(name+'-compile.txt'),True)
                objects[name]=obj
                result['compiles'].append({'mode':mode,'variant':'candidate','file':name,
                                          'source_sha256':sha(generated[name]),'exit_code':proc.returncode,
                                          'output_sha256':sha(output)})
            harness=dest/'peer.o'
            proc,output=execute(base+['-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable',
                                     '-c',str(run/'peer.c'),'-o',str(harness)],dest/'peer-compile.txt',True)
            result['compiles'].append({'mode':mode,'variant':'candidate','file':'peer.c',
                                      'source_sha256':sha(captured['peer.c']),'exit_code':proc.returncode,
                                      'output_sha256':sha(output)})
            rows=[('candidate',None,None,[(n,[]) for n in cases])]
            rows += [(c['name'],c['file'],source,[(c['case'],c['expected_failures'])]) for c,source in variants]
            for variant,changed,source,selectors in rows:
                vd=dest/variant
                vd.mkdir()
                use=objects.copy()
                if changed:
                    path=vd/changed
                    path.write_bytes(source)
                    obj=vd/(changed+'.o')
                    proc,output=execute(base+['-c',str(path),'-o',str(obj)],vd/'compile.txt',True)
                    use[changed]=obj
                    result['compiles'].append({'mode':mode,'variant':variant,'file':changed,
                                              'source_sha256':sha(source),'exit_code':proc.returncode,
                                              'output_sha256':sha(output)})
                peer=vd/'peer'
                proc,output=execute([cc,'-pthread',*flags,str(harness),*[str(use[n]) for n in PRODUCTION],
                                     '-o',str(peer)],vd/'link.txt',True)
                result['links'].append({'mode':mode,'variant':variant,'exit_code':proc.returncode,
                                        'output_sha256':sha(output)})
                for case,expected in selectors:
                    proc,output=execute([str(host),case,str(peer),str(vd/(case+'-evidence'))],vd/(case+'.txt'))
                    if re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|LeakSanitizer|runtime error:|DEADLYSIGNAL',output):
                        raise RuntimeError('sanitizer diagnostic: '+mode+'/'+variant+'/'+case)
                    expected_out='checks=1 failures='+str(len(expected))+'\n'
                    expected_err=''.join('FAIL '+label+'\n' for label in expected)
                    if proc.returncode!=int(bool(expected)) or proc.stdout!=expected_out or proc.stderr!=expected_err:
                        raise RuntimeError('unexpected functional result: '+mode+'/'+variant+'/'+case)
                    result['runs'].append({'mode':mode,'variant':variant,'case':case,'checks':1,
                                          'failures':expected,'exit_code':proc.returncode,'output_sha256':sha(output)})
                    print(mode,variant,case,expected_out.strip(),flush=True)
        for n,data in captured.items():
            if (HERE/n).read_bytes()!=data:
                raise ValueError('source drift during run: '+n)
        for n,data in frozen.items():
            if (HERE.parent/n).read_bytes()!=data:
                raise ValueError('frozen drift during run: '+n)
        save(run/'result.json',result)
        save(run/'attempt.json',{'status':'passed','result_sha256':sha((run/'result.json').read_bytes())})
        if args.result:
            args.result.write_bytes((run/'result.json').read_bytes())
        print('RESULT',sha((run/'result.json').read_bytes()),'private snapshot',run)
    except BaseException as error:
        save(run/'attempt.json',{'status':'failed','error_type':type(error).__name__,'error':str(error)})
        print('FAILED retained private snapshot',run,flush=True)
        raise

if __name__=='__main__':
    main()

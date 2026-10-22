#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Serial actual-installer behavior plus exact-ELF continuation metadata.
No firmware execution, device contacts, current-upstream writes or install.
"""
import argparse, hashlib, io, json, os, re, struct, subprocess, uuid
from pathlib import Path
from elftools.dwarf.callframe import FDE
from elftools.elf.elffile import ELFFile

HERE=Path(__file__).resolve().parent
PIN='3e0c5cb010454228f4ee63226b60d060494eeaae'
FW='6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
def sha(x): return hashlib.sha256(x.encode() if isinstance(x,str) else x).hexdigest()
def getfn(s,name):
    m=re.search(r'^static [^\n]+\b'+name+r'\(',s,re.M); assert m,name
    end=s.index('\n}',m.start())+2
    return s[m.start():end]+'\n'
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--upstream',type=Path,required=True)
    ap.add_argument('--elf',type=Path,required=True);ap.add_argument('--result',type=Path,required=True)
    a=ap.parse_args();a.upstream=a.upstream.resolve()
    names=['src/azmod/'+n for n in ['inline_hooks.c','community.c','azmod-mod.h','sdk_vslot.c']]
    source={n:subprocess.check_output(['git','show',PIN+':'+n],cwd=a.upstream,text=True) for n in names}
    inline=source[names[0]]
    functions=['azm_pcrel_kind','azm_adrp_target','azm_emit_abs_addr','azm_adrp_reloc_ok',
               'azm_reloc_adrp','azm_abs_jump','azm_ends_fn','azm_patch_ok',
               'azm_tramp_seal','azm_install_hook_at']
    extracted='\n'.join(getfn(inline,n) for n in functions)
    test=(HERE/'test-installer.c').read_text()
    out=HERE/'out-history'/str(uuid.uuid4());out.mkdir(parents=True)
    for name,body in source.items():(out/Path(name).name).write_text(body)
    (out/'test-installer.c').write_text(test);(out/'run-tests.py').write_bytes(Path(__file__).read_bytes())
    raw=a.elf.read_bytes();assert sha(raw)==FW
    elf=ELFFile(io.BytesIO(raw));segs=list(elf.iter_segments())
    assert elf['e_machine']=='EM_AARCH64' and elf['e_type']=='ET_EXEC'
    def read(addr,n):
        matches=[s for s in segs if s['p_type']=='PT_LOAD' and s['p_vaddr']<=addr and addr+n<=s['p_vaddr']+s['p_filesz']]
        assert len(matches)==1
        s=matches[0];at=s['p_offset']+addr-s['p_vaddr'];return raw[at:at+n]
    fdes={f['initial_location']:f for f in elf.get_dwarf_info().EH_CFI_entries() if isinstance(f,FDE)}
    targets={0x237a440:('submit',0x38,0x321b138),0x1b029c0:('execute',0x104,0x2d444e8)}
    facts={}
    for address,(name,extent,slot) in targets.items():
        words=struct.unpack('<4I',read(address,16))
        actual=re.search(r'const uint32_t '+name+r'\[4\]=\{([^}]+)',test).group(1)
        assert tuple(int(v,0) for v in actual.split(','))==words,(name,[hex(v) for v in words])
        fde=fdes[address];assert fde['address_range']==extent
        table=fde.get_decoded().table
        cfi={}
        for offset in (0,4,16):
            row=[r for r in table if r['pc']<=address+offset][-1]
            cfi[hex(offset)]={'cfa_register':row['cfa'].reg,'cfa_offset':row['cfa'].offset,
                              'saved_registers':{str(k):{'type':v.type,'offset':v.arg} for k,v in row.items() if isinstance(k,int)}}
        assert cfi['0x4']['cfa_register']==31 and cfi['0x4']['cfa_offset']==(48 if name=='submit' else 0)
        if name=='submit': assert cfi['0x4']['saved_registers']['30']=={'type':'OFFSET','offset':-40}
        else: assert cfi['0x4']['saved_registers']=={} and cfi['0x10']['cfa_offset']==0
        p=[s for s in segs if s['p_type']=='PT_LOAD' and s['p_vaddr']<=slot and slot+8<=s['p_vaddr']+s['p_memsz']]
        assert len(p)==1 and p[0]['p_flags']&1 and struct.unpack('<Q',read(slot,8))[0]==address
        facts[hex(address)]={'body_sha256':sha(read(address,extent)),'fde_extent':hex(extent),
                             'prologue_sha256':sha(read(address,16)),'continuation_cfi':cfi,
                             'matching_slot':hex(slot),'slot_PT_LOAD_flags':p[0]['p_flags'],
                             'sdk_vslot_data_gate_accepts':False}
    variants={'candidate':extracted}
    early='''        __atomic_store_n(orig, (void *) tramp, __ATOMIC_RELAXED);
        __atomic_store_n(func, b, __ATOMIC_RELEASE);'''
    late='''        __atomic_store_n(func, b, __ATOMIC_RELEASE);
        __atomic_store_n(orig, (void *) tramp, __ATOMIC_RELAXED);'''
    assert extracted.count(early)==1
    variants['late-original']=extracted.replace(early,late)
    assert extracted.count('FUNC + 4 * (uintptr_t) ncopy')==1
    variants['wrong-continuation']=extracted.replace('FUNC + 4 * (uintptr_t) ncopy','FUNC + 8 * (uintptr_t) ncopy')
    expected={'candidate':[],
              'late-original':['submission/original-before-redirection','executor/original-before-redirection',
                               'partial-init-first-claim/original-before-redirection','relock-warning/published-forwarding-valid'],
              'wrong-continuation':['submission/original-plus-four','executor/original-plus-four','partial-init-first-claim/original-plus-four']}
    report={'scope':'Serial source-order/resource mocks; emitted instructions not executed; no cross-core cache/publication or unwind execution proof',
            'upstream_commit':PIN,'firmware_sha256':FW,'source_sha256':{n:sha(s) for n,s in source.items()},
            'extracted_sha256':sha(extracted),'test_sha256':sha(test),'runner_sha256':sha(Path(__file__).read_bytes()),
            'continuations':facts,'runs':[]}
    for name,body in variants.items():
        directory=out/name;directory.mkdir();(directory/'installer-extracted.inc').write_text(body)
        (directory/'test-installer.c').write_text(test)
        for mode,flags in [('plain',[]),('asan-ubsan',['-fsanitize=address,undefined'])]:
            exe=directory/mode
            built=subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fno-omit-frame-pointer',*flags,str(directory/'test-installer.c'),'-o',str(exe)],capture_output=True,text=True)
            (directory/(mode+'-compile.txt')).write_text(built.stdout+built.stderr)
            if built.returncode: print(built.stderr,flush=True)
            built.check_returncode()
            r=subprocess.run([str(exe)],capture_output=True,text=True);output=r.stdout+r.stderr
            (directory/(mode+'.txt')).write_text(output);print(name+'-'+mode+': '+output,end='',flush=True)
            assert not re.search(r'Sanitizer|runtime error:|DEADLYSIGNAL',output),str(out)
            failures=re.findall(r'^FAIL (.+)$',r.stderr,re.M)
            summary=re.search(r'^checks=(\d+) failures=(\d+) scenarios=(\d+)$',r.stdout,re.M)
            assert summary and failures==expected[name] and int(summary[2])==len(failures) and r.returncode==bool(failures),str(out)
            assert int(summary[1])==56 and int(summary[3])==10,output
            report['runs'].append({'variant':name,'mode':mode,'checks':int(summary[1]),'scenarios':int(summary[3]),
                                  'expected_failures':failures,'output_sha256':sha(output)})
    payload=json.dumps(report,indent=2)+'\n';(out/'result.json').write_text(payload);a.result.write_text(payload)
if __name__=='__main__':main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Offline serial counterexample: committed overlays are not a consumed-event queue."""
import argparse, hashlib, json, os, re, subprocess, sys, uuid
from pathlib import Path
HERE=Path(__file__).resolve().parent
FROZEN=HERE.parent/'emulator-overlay-handback'
PINS={
 'prepare.py':'6a35c4ac9c3d9671d1d652bbba57ea6a49f2e4f8865427c1d58751b340ffad84',
 'run-tests.py':'1a8030db691cc6df52bf5afc7d94fd460360875f3c535a43070c859ed2745f83',
 'emulator-overlay-handback.patch':'fa800f32c5e9227cc1953028d35b896e711a40b0d4fd6f2e03dcdf2ac6eaf0dd',
 '../emulator-overlay-ownership/emulator-overlay-ownership.patch':'34d870df06f902c04eb0c2029a1c0411e647519ff4a8fabdc17b16ffe8befb3f',
}
FUNCTIONS=('crc16_x25','xput_bytes','xget_bytes','spi_read','mixer_owned_next','ctrl_ioctl')
def sha(data):return hashlib.sha256(data.encode() if isinstance(data,str) else data).hexdigest()
def once(src,old,new):
 assert src.count(old)==1,(old,src.count(old));return src.replace(old,new)
def main():
 if not __debug__:raise RuntimeError('optimized Python is unsupported')
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--emu',type=Path,required=True);ap.add_argument('--result',type=Path);args=ap.parse_args()
 inputs={name:(FROZEN/name).read_bytes() for name in PINS}
 for name,data in inputs.items():assert sha(data)==PINS[name],name
 local={name:(HERE/name).read_bytes() for name in ('run-dwell-test.py','test-dwell.c')}
 run=HERE/'private'/'dwell-runs'/uuid.uuid4().hex;run.mkdir(parents=True)
 frozen=run/'frozen';frozen.mkdir()
 for name,data in inputs.items():(frozen/Path(name).name).write_bytes(data)
 for name,data in local.items():(run/name).write_bytes(data)
 # Execute captured, hash-verified helper bytes under non-main names. Only
 # prepare() and extract() are called: no frozen test suite is rerun.
 prep={'__name__':'dwell_prepare','__file__':str(FROZEN/'prepare.py')}
 exec(compile(inputs['prepare.py'],str(frozen/'prepare.py'),'exec'),prep)
 old={'__name__':'dwell_extract','__file__':str(FROZEN/'run-tests.py')}
 exec(compile(inputs['run-tests.py'],str(frozen/'run-tests.py'),'exec'),old)
 receipt=prep['prepare'](args.emu,run/'prepared');extract=old['extract']
 assert (run/'prepared/applied-1.patch').read_bytes()==inputs['../emulator-overlay-ownership/emulator-overlay-ownership.patch']
 assert (run/'prepared/applied-2.patch').read_bytes()==inputs['emulator-overlay-handback.patch']
 mod=run/'prepared/guest/modules/subucom_virt';src=(mod/'subucom_virt.c').read_text()
 for name,digest in receipt['candidate_sha256'].items():assert sha((mod/name).read_bytes())==digest
 f={name:extract(src,name) for name in FUNCTIONS};decl=''
 for name in ('spi_crc_len','interval_us','testmode_held'):
  matches=re.findall(r'^static [^;\n]*\b'+name+r'[^;\n]*;',src,re.M);assert len(matches)==1,(name,matches);decl+=matches[0]+'\n'
 start=src.index('static DEFINE_SPINLOCK(miso_lock);');end=src.index('/* MOSI:',start)
 prefix=decl+once(src[start:end],extract(src,'mixer_instance_init'),'')
 (run/'global-declarations.inc').write_text(prefix);(run/'functions').mkdir()
 for name,body in f.items():(run/'functions'/(name+'.c')).write_text(body)
 (run/'linux').mkdir()
 (run/'linux/types.h').write_text('#include <stdint.h>\ntypedef uint32_t __u32;typedef int32_t __s32;typedef uint8_t __u8;\n#define __aligned_u64 uint64_t __attribute__((aligned(8)))\n')
 (run/'linux/ioctl.h').write_text('#define _IOW(t,n,s) ((1u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n#define _IOR(t,n,s) ((2u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n')
 for name in ('subucom_ownership.h','subucom_frames.h'):(run/name).write_bytes((mod/name).read_bytes())
 full=prefix+'\n'.join(f.values())
 variants={'candidate':(full,'all',20,[]),'false-consumption-hypothesis':(full,'false-hypothesis',1,['every_successful_commit_must_be_observed_hypothesis'])}
 modified=dict(f);modified['ctrl_ioctl']=once(f['ctrl_ioctl'],'                memcpy(inject_pending[OWNED_BUS], request.frame, MISO_SIZE);','                if (!has_inject[OWNED_BUS])\n                    memcpy(inject_pending[OWNED_BUS], request.frame, MISO_SIZE);')
 variants['first-overlay-retained']=(prefix+'\n'.join(modified.values()),'latest',1,['latest_commit_replaces_prior_overlay'])
 modified=dict(f);modified['spi_read']=once(f['spi_read'],'        if (has_inject[bus])','        if (false && has_inject[bus])')
 variants['overlay-read-bypassed']=(prefix+'\n'.join(modified.values()),'overlay-read',1,['poll_reads_committed_overlay'])
 cc=os.environ.get('CC','cc');version=subprocess.check_output([cc,'--version'],text=True)
 (run/'compiler.txt').write_text(version)
 result={'base_commit':receipt['base_commit'],'base_sha256':receipt['base_sha256'],'candidate_sha256':receipt['candidate_sha256'],'input_sha256':PINS,'local_sha256':{n:sha(d) for n,d in local.items()},'extracted_function_sha256':{n:sha(b) for n,b in f.items()},'declarations_sha256':sha(prefix),'shim_header_sha256':{n:sha((run/'linux'/n).read_bytes()) for n in ('types.h','ioctl.h')},'compiler':version.splitlines()[0],'runs':[],
  'scope':'Serial actual-source schedule with owned-memory usercopy, synthetic full frames, pthread locks, successful permission shims and a counted non-sleeping SPI-poll shim. Two successful FRAME commits before one SPI read expose only the last overlay; an interposed SPI read exposes the press. The false-consumption hypothesis is an intentional expected failure on unchanged source; two separate source mutants must fail exact oracles. No broker ACK transport, wall-clock dwell, native handler, device, VFS, real kernel scheduling, RNG, registration or deployment proof. No TSan claim for this serial fixture.'}
 modes={'plain':[],'asan-ubsan':['-fsanitize=address,undefined','-fno-omit-frame-pointer']}
 for name,(body,case,checks,labels) in variants.items():
  directory=run/name;directory.mkdir();(directory/'candidate.inc').write_text(body)
  for mode,flags in modes.items():
   exe=directory/mode;command=[cc,'-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,'-I'+str(directory),'-I'+str(run),str(run/'test-dwell.c'),'-o',str(exe)]
   c=subprocess.run(command,capture_output=True,text=True);(directory/(mode+'-compile.txt')).write_text(c.stdout+c.stderr)
   if c.returncode:raise RuntimeError('compile failed; retained '+str(directory))
   p=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=30);out=p.stdout+p.stderr;(directory/(mode+'.txt')).write_text(out)
   if re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|LeakSanitizer|runtime error:|DEADLYSIGNAL',out):raise RuntimeError('sanitizer diagnostic; retained '+str(directory))
   found=re.findall(r'^FAIL (.+)$',p.stderr,re.M);m=re.fullmatch(r'checks=(\d+) failures=(\d+)\n',p.stdout)
   if not m or int(m[1])!=checks or int(m[2])!=len(labels) or found!=labels or p.returncode!=int(bool(labels)):raise RuntimeError('functional mismatch; retained '+str(directory))
   result['runs'].append({'variant':name,'mode':mode,'case':case,'checks':checks,'failures':found,'exit_code':p.returncode,'output_sha256':sha(out),'extraction_sha256':sha(body)})
   print(name,mode,m[0].strip(),flush=True)
 for name,data in inputs.items():assert (FROZEN/name).read_bytes()==data,'frozen input changed: '+name
 for name,data in local.items():assert (HERE/name).read_bytes()==data,'local input changed: '+name
 raw=json.dumps(result,indent=2,sort_keys=True)+'\n';(run/'result.json').write_text(raw)
 if args.result:args.result.write_text(raw)
 print('RESULT',sha(raw),'private snapshot',run)
if __name__=='__main__':main()

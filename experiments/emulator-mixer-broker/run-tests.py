#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Offline broker interaction tests using exact frozen V2 kernel functions."""
import argparse, hashlib, importlib.util, json, os, re, subprocess, sys, uuid
from pathlib import Path
HERE=Path(__file__).resolve().parent
FROZEN=HERE.parent/'emulator-overlay-handback'
PINS={'prepare.py':'6a35c4ac9c3d9671d1d652bbba57ea6a49f2e4f8865427c1d58751b340ffad84','run-tests.py':'1a8030db691cc6df52bf5afc7d94fd460360875f3c535a43070c859ed2745f83','test-guard.c':'37882354077f05ee87a5f9655a8325380d02c4a43b292081d8ecb8fd49ead9a4'}
FUNCTIONS=('mixer_instance_init','xput_bytes','xget_bytes','ctrl_release','mixer_owned_next','ctrl_ioctl','ctrl_write')
COUNTS={'all':203,'stale':1,'conflict':1,'lower':1,'quiesce':1,'adopt':1,'cached':1}
def sha(data):return hashlib.sha256(data.encode() if isinstance(data,str) else data).hexdigest()
def replace_once(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
def load_module(path,name):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--emu',required=True,type=Path);p.add_argument('--modes',nargs='+',choices=['plain','asan-ubsan','tsan'],default=['plain','asan-ubsan','tsan']);p.add_argument('--result',type=Path);a=p.parse_args()
 run=HERE/'private/runs'/uuid.uuid4().hex;run.mkdir(parents=True)
 captured={n:(HERE/n).read_bytes() for n in ('broker.c','broker.h','test-broker.c','run-tests.py')}
 for n,data in captured.items():(run/n).write_bytes(data)
 frozen={n:(FROZEN/n).read_bytes() for n in PINS}
 for n,data in frozen.items():assert sha(data)==PINS[n],n
 (run/'frozen').mkdir()
 for n,data in frozen.items():(run/'frozen'/n).write_bytes(data)
 # Import only verified helper definitions; guarded main does not run old tests.
 extract=load_module(FROZEN/'run-tests.py','frozen_extract').extract
 prep=subprocess.run([sys.executable,str(FROZEN/'prepare.py'),'--emu',str(a.emu.resolve()),'--output',str(run/'prepared')],capture_output=True,text=True)
 (run/'prepare.txt').write_text(prep.stdout+prep.stderr)
 if prep.returncode:raise RuntimeError('prepare failed: '+str(run))
 receipt=json.loads((run/'prepared/prepare-result.json').read_text());mod=run/'prepared/guest/modules/subucom_virt';src=(mod/'subucom_virt.c').read_text()
 for n,digest in receipt['candidate_sha256'].items():assert sha((mod/n).read_bytes())==digest
 funcs={n:extract(src,n) for n in FUNCTIONS};declarations=''
 for name in ('spi_crc_len','n_crc','interval_us','testmode_held'):
  lines=re.findall(r'^static [^;\n]*\b'+name+r'[^;\n]*;',src,re.M);assert len(lines)==1,(name,lines);declarations+=lines[0]+'\n'
 begin=src.index('static DEFINE_SPINLOCK(miso_lock);');end=src.index('/* MOSI:',begin)
 declarations+=src[begin:end].replace(funcs['mixer_instance_init'],'')
 kernel=declarations+'\n'.join(funcs.values());(run/'candidate.inc').write_text(kernel)
 shim=frozen['test-guard.c'].decode().split('#include "candidate.inc"')[0];(run/'kernel-shim.inc').write_text(shim)
 (run/'linux').mkdir();(run/'linux/types.h').write_text('#include <stdint.h>\ntypedef uint32_t __u32;typedef int32_t __s32;typedef uint8_t __u8;\n#define __aligned_u64 uint64_t __attribute__((aligned(8)))\n');(run/'linux/ioctl.h').write_text('#define _IOW(t,n,s) ((1u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n#define _IOR(t,n,s) ((2u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n')
 for n in ('subucom_ownership.h','subucom_frames.h'):(run/n).write_bytes((mod/n).read_bytes())
 broker=captured['broker.c'].decode();variants={'candidate':(broker,'all',[])}
 def mutant(name,old,new,case,label):variants[name]=(replace_once(broker,old,new),case,[label])
 mutant('grant-ignores-epoch','a->run_id == b->run_id && a->epoch == b->epoch && a->producer_id == b->producer_id','a->run_id == b->run_id && a->producer_id == b->producer_id','stale','stale_epoch_refused_before_backend')
 mutant('cache-ignores-payload','memcmp(a->frame, b->frame, MB_FRAME_BYTES) == 0;\n}\nstatic int lock_broker','1;\n}\nstatic int lock_broker','conflict','duplicate_compares_all_frame_bytes')
 mutant('any-valid-state-resolves-unknown','state_valid(&actual) && state_equal(&actual, &b->state.last.expected_after)','state_valid(&actual)','lower','lower_state_is_not_cancellation')
 mutant('quiesce-keeps-admission','int mb_quiesce(struct mb_broker *b)\n{\n    int rc = lock_broker(b); if (rc) return rc;\n    b->state.admission_open = 0;','int mb_quiesce(struct mb_broker *b)\n{\n    int rc = lock_broker(b); if (rc) return rc;\n    (void)b;','quiesce','quiesce_blocks_new_request')
 mutant('readopt-closed-epoch','b->state.admission_open || b->state.grant_issued','b->state.admission_open','adopt','closed_epoch_cannot_reissue_grant')
 mutant('cached-ack-reopens','if (request_equal(&copy, &b->state.last.request)) {\n            *out','if (request_equal(&copy, &b->state.last.request)) {\n            b->state.admission_open = 1;\n            *out','cached','cached_ack_does_not_reopen')
 result={'source_sha256':{n:sha(data) for n,data in captured.items()},'frozen_helpers_sha256':PINS,'kernel_prepare_receipt':receipt,'kernel_extraction_sha256':sha(kernel),'kernel_function_sha256':{n:sha(s) for n,s in funcs.items()},'kernel_declarations_sha256':sha(declarations),'kernel_shim_sha256':sha(shim),'uapi_shims_sha256':{n:sha((run/'linux'/n).read_bytes()) for n in ('types.h','ioctl.h')},'runs':[],
 'scope':'Offline C broker plus seven exact extracted frozen V2 kernel methods. pthread, owned-memory usercopy and deterministic RNG shims; synthetic frames and selected deterministic/concurrent schedules. Direct setup seeds for counter boundaries. No actual kernel, VFS, IPC, broker daemon, crash durability, preview/host-settings/native stop/restore integration, native delivery, device or emulator installation proof.'}
 flags={'plain':[],'asan-ubsan':['-fsanitize=address,undefined','-fno-omit-frame-pointer'],'tsan':['-fsanitize=thread','-fno-omit-frame-pointer']};cc=os.environ.get('CC','cc')
 result['compiler']=subprocess.check_output([cc,'--version'],text=True).splitlines()[0]
 for name,(body,case,expected) in variants.items():
  dest=run/name;dest.mkdir();(dest/'broker-under-test.inc').write_text(body)
  for mode in a.modes:
   command=[cc,'-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable',*flags[mode],'-I'+str(dest),'-I'+str(run),str(run/'test-broker.c'),'-o',str(dest/mode)]
   compiled=subprocess.run(command,capture_output=True,text=True);(dest/(mode+'-compile.txt')).write_text(compiled.stdout+compiled.stderr)
   if compiled.returncode:raise RuntimeError('compile failed: '+str(dest))
   proc=subprocess.run([str(dest/mode),case],capture_output=True,text=True,timeout=40);output=proc.stdout+proc.stderr;(dest/(mode+'.txt')).write_text(output)
   if re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|LeakSanitizer|runtime error:|DEADLYSIGNAL',output):raise RuntimeError('sanitizer diagnostic: '+str(dest))
   failed=sorted(re.findall(r'^FAIL (.+)$',proc.stderr,re.M));match=re.search(r'^checks=(\d+) failures=(\d+)$',proc.stdout,re.M)
   if not match or int(match[1])!=COUNTS[case] or int(match[2])!=len(expected) or failed!=sorted(expected) or proc.returncode!=bool(expected):raise RuntimeError('unexpected functional result: '+str(dest))
   result['runs'].append({'variant':name,'mode':mode,'case':case,'checks':int(match[1]),'failures':failed,'exit_code':proc.returncode,'broker_sha256':sha(body),'output_sha256':sha(output)})
   print(name,mode,match[0],flush=True)
 for n,data in captured.items():assert (HERE/n).read_bytes()==data,'source changed during run: '+n
 for n,data in frozen.items():assert (FROZEN/n).read_bytes()==data,'frozen helper changed: '+n
 raw=json.dumps(result,sort_keys=True,indent=2)+'\n';(run/'result.json').write_text(raw)
 if a.result:a.result.write_text(raw)
 print('RESULT',sha(raw),'private snapshot',run)
if __name__=='__main__':main()

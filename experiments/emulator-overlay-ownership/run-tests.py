#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Prepare pinned source and test exact kernel methods offline with host shims."""
import argparse,hashlib,json,os,re,subprocess,sys,uuid
from pathlib import Path
HERE=Path(__file__).resolve().parent
FUNCTIONS=('crc16_x25','crc_lengths_set','crc_lengths_get','xput_bytes','xget_bytes','spi_open','spi_release','spi_read','ctrl_open','ctrl_release','mixer_owned_next','ctrl_ioctl','ctrl_write')
def sha(b):return hashlib.sha256(b.encode() if isinstance(b,str) else b).hexdigest()
def extract(src,name):
 m=re.search(r'^static [^\n]*\b'+name+r'\([^;]*?\)\s*\{',src,re.M);assert m,name
 tokens=re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]',re.S);depth=0
 for t in tokens.finditer(src,src.index('{',m.start())):
  if t.group()=='{':depth+=1
  elif t.group()=='}':
   depth-=1
   if not depth:return src[m.start():t.end()]+'\n'
 raise AssertionError(name)
def replace_once(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
def main():
 p=argparse.ArgumentParser();p.add_argument('--emu',type=Path,required=True);p.add_argument('--modes',nargs='+',choices=['plain','asan-ubsan','tsan'],default=['plain','asan-ubsan','tsan']);p.add_argument('--result',type=Path);a=p.parse_args()
 run=HERE/'private'/'runs'/uuid.uuid4().hex;run.mkdir(parents=True)
 (run/'runner.py').write_bytes(Path(__file__).read_bytes());(run/'test-guard.c').write_bytes((HERE/'test-guard.c').read_bytes());(run/'prepare.py').write_bytes((HERE/'prepare.py').read_bytes());(run/'candidate.patch').write_bytes((HERE/'emulator-overlay-ownership.patch').read_bytes())
 prep=subprocess.run([sys.executable,str(HERE/'prepare.py'),'--emu',str(a.emu.resolve()),'--output',str(run/'prepared')],capture_output=True,text=True)
 (run/'prepare.stdout').write_text(prep.stdout);(run/'prepare.stderr').write_text(prep.stderr)
 if prep.returncode:raise RuntimeError('prepare failed; retained '+str(run))
 receipt=json.loads((run/'prepared/prepare-result.json').read_text());mod=run/'prepared/guest/modules/subucom_virt';src=(mod/'subucom_virt.c').read_text()
 for n,d in receipt['candidate_sha256'].items():assert sha((mod/n).read_bytes())==d
 anchors={
 'control_handler_registered':'.unlocked_ioctl = ctrl_ioctl,' in src,
 'no_compat_handler':'.compat_ioctl' not in src,
 'opt_in_default_zero':'static bool allow_overlay_ownership;' in src,
 'opt_in_immutable_mode':'module_param(allow_overlay_ownership, bool, 0444);' in src,
 'crc_callback_registered':'module_param_cb(spi_crc_len, &crc_lengths_ops, NULL, 0644);' in src,
 'old_crc_array_removed':'module_param_array(spi_crc_len' not in src,
 'crc_array_metadata_once':re.findall(r'__MODULE_PARM_TYPE\(\s*spi_crc_len\s*,[^)]*\)',src)==['__MODULE_PARM_TYPE(spi_crc_len, \"array of int\")'],
 'crc_ops_set_get':'.set = crc_lengths_set,' in src and '.get = crc_lengths_get,' in src,
 }
 for line in ('sizeof(struct az_overlay_command) == 32','sizeof(struct az_overlay_frame) == 184','sizeof(struct az_overlay_state) == 192','offsetof(struct az_overlay_command, run_id) == 8','offsetof(struct az_overlay_command, epoch) == 16','offsetof(struct az_overlay_command, sequence) == 24','offsetof(struct az_overlay_frame, frame) == 32','offsetof(struct az_overlay_state, frame) == 40','AZ_OVERLAY_BYTES == MISO_SIZE'):
  anchors['static_assert '+line]='static_assert('+line+');' in src
 assert all(anchors.values()),anchors
 f={name:extract(src,name) for name in FUNCTIONS}
 declaration_names=('spi_crc_len','n_crc','interval_us','testmode_held')
 declarations=''
 for name in declaration_names:
  m=re.findall(r'^static [^;\n]*\b'+name+r'[^;\n]*;',src,re.M);assert len(m)==1,(name,m);declarations+=m[0]+'\n'
 start=src.index('static DEFINE_SPINLOCK(miso_lock);');end=src.index('/* MOSI:',start)
 globals_source=src[start:end]
 prefix=declarations+globals_source
 (run/'global-declarations.inc').write_text(prefix)
 (run/'functions').mkdir()
 for name,text in f.items():(run/'functions'/ (name+'.c')).write_text(text)
 # Header is unchanged; these stand-in include files encode the native Linux
 # 64-bit UAPI for a host compiler, not a compat/kernel execution proof.
 (run/'linux').mkdir()
 (run/'linux/types.h').write_text('#include <stdint.h>\ntypedef uint32_t __u32;typedef int32_t __s32;typedef uint8_t __u8;\n#define __aligned_u64 uint64_t __attribute__((aligned(8)))\n')
 (run/'linux/ioctl.h').write_text('#define _IOW(t,n,s) ((1u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n#define _IOR(t,n,s) ((2u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n')
 for name in ('subucom_ownership.h','subucom_frames.h'):(run/name).write_bytes((mod/name).read_bytes())
 full=prefix+'\n'.join(f.values());variants={'candidate':(full,'all',[])}
 def mutant(name,fn,text,case,label):
  changed=dict(f);changed[fn]=text;variants[name]=(prefix+'\n'.join(changed.values()),case,[label])
 # Remove only the final payload gate, retaining the earlier single-clear gate.
 old='''        if (bus == OWNED_BUS && mixer_fenced) {
            spin_unlock_irqrestore(&miso_lock, flags);
            return -EBUSY;
        }
        memcpy(inject_pending[bus], tmp, MISO_SIZE);'''
 mutant('missing-final-fence','ctrl_write',replace_once(f['ctrl_write'],old,'        memcpy(inject_pending[bus], tmp, MISO_SIZE);'),'fence','queued_legacy_write_refused_at_commit')
 old='''        if (mixer_fenced) {
            spin_unlock_irqrestore(&miso_lock, flags);
            return -EBUSY;
        }
'''
 mutant('missing-clear-all-fence','ctrl_write',replace_once(f['ctrl_write'],old,''),'clear','clear_all_atomically_refused')
 mutant('missing-file-identity','mixer_owned_next',replace_once(f['mixer_owned_next'],'    if (mixer_owner != filp)\n        return -EACCES;','    (void)filp;'),'owner','file_identity_required')
 mutant('missing-exact-sequence','mixer_owned_next',replace_once(f['mixer_owned_next'],'    if (h->sequence != mixer_sequence + 1)\n        return -ESTALE;',''),'sequence','duplicate_sequence_refused')
 mutant('close-clears-fence','ctrl_release',replace_once(f['ctrl_release'],'        mixer_owner = NULL;','        mixer_owner = NULL;\n    mixer_fenced = false;'),'close','close_keeps_permanent_fence')
 mutant('partial-parser-commit','crc_lengths_set',replace_once(f['crc_lengths_set'],'        count++;','        spi_crc_len[count] = parsed[count];\n        count++;'),'parse','parse_failure_no_partial_commit')
 old='''        len = spi_crc_len[bus];
        spin_unlock_irqrestore(&miso_lock, flags);'''
 mutant('crc-after-snapshot-unlock','spi_read',replace_once(f['spi_read'],old,'''        spin_unlock_irqrestore(&miso_lock, flags);
        len = spi_crc_len[bus];'''),'crc-snapshot','read_frame_and_crc_same_snapshot')
 result={'base_commit':receipt['base_commit'],'base_sha256':receipt['base_sha256'],'patch_sha256':receipt['patch_sha256'],'candidate_sha256':receipt['candidate_sha256'],'runner_sha256':sha(Path(__file__).read_bytes()),'test_sha256':sha((HERE/'test-guard.c').read_bytes()),'prepare_sha256':sha((HERE/'prepare.py').read_bytes()),'source_integration_checks':anchors,'extracted_function_sha256':{n:sha(s) for n,s in f.items()},'declarations_sha256':sha(prefix),'shim_header_sha256':{n:sha((run/'linux'/n).read_bytes()) for n in ('types.h','ioctl.h')},'runs':[],
 'scope':'Exact extracted candidate methods with pthread mutex, owned usercopy and kernel-service shims. Synthetic frames, deterministic interleavings and concurrent reads/writes. No real kernel parser, VFS reference lifecycle, kernel registration execution, module install, device, native input, recovery or handback proof.'}
 flags={'plain':[],'asan-ubsan':['-fsanitize=address,undefined','-fno-omit-frame-pointer'],'tsan':['-fsanitize=thread','-fno-omit-frame-pointer']};cc=os.environ.get('CC','cc')
 for variant,(body,case,expected) in variants.items():
  directory=run/variant;directory.mkdir();(directory/'candidate.inc').write_text(body)
  for mode in a.modes:
   exe=directory/mode;command=[cc,'-std=gnu11','-O1','-g','-pthread','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags[mode],'-I'+str(directory),'-I'+str(run),str(run/'test-guard.c'),'-o',str(exe)]
   compiled=subprocess.run(command,capture_output=True,text=True);(directory/(mode+'-compile.txt')).write_text(compiled.stdout+compiled.stderr)
   if compiled.returncode:raise RuntimeError('compile failed; retained '+str(directory))
   proc=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=45);output=proc.stdout+proc.stderr;(directory/(mode+'.txt')).write_text(output)
   if re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|ThreadSanitizer|LeakSanitizer|runtime error:|DEADLYSIGNAL',output):raise RuntimeError('unexpected sanitizer diagnostic; retained '+str(directory))
   failed=sorted(re.findall(r'^FAIL (.+)$',proc.stderr,re.M));match=re.search(r'^checks=(\d+) failures=(\d+)$',proc.stdout,re.M)
   expected_checks=146 if variant=='candidate' else (3 if case in ('fence','crc-snapshot') else 1)
   if not match or int(match[1])!=expected_checks or failed!=sorted(expected) or int(match[2])!=len(expected) or proc.returncode!=bool(expected):raise RuntimeError('unexpected functional result; retained '+str(directory))
   result['runs'].append({'variant':variant,'mode':mode,'case':case,'checks':int(match[1]),'failures':failed,'exit_code':proc.returncode,'output_sha256':sha(output),'extraction_sha256':sha(body)})
   print(variant,mode,match[0],flush=True)
 for source,snapshot in ((HERE/'test-guard.c',run/'test-guard.c'),(Path(__file__),run/'runner.py'),(HERE/'prepare.py',run/'prepare.py'),(HERE/'emulator-overlay-ownership.patch',run/'candidate.patch')):assert source.read_bytes()==snapshot.read_bytes(),'source changed during run'
 raw=json.dumps(result,indent=2,sort_keys=True)+'\n';(run/'result.json').write_text(raw)
 if a.result:a.result.write_text(raw)
 print('RESULT',sha(raw),'private snapshot',run)
if __name__=='__main__':main()

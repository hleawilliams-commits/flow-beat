#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Prepare pinned source and test exact kernel methods offline with host shims."""
import argparse,hashlib,json,os,re,subprocess,sys,uuid
from pathlib import Path
HERE=Path(__file__).resolve().parent
EXPECTED_CHECKS={'all':228,'init-wait':1,'init-zero':1,'instance':1,'rotation':1,'overflow':1,'stale-rotation':3,'atomic-rotation':3,'owner':1}
FUNCTIONS=('mixer_instance_init','crc16_x25','crc_lengths_set','crc_lengths_get','xput_bytes','xget_bytes','spi_open','spi_release','spi_read','ctrl_open','ctrl_release','mixer_owned_next','ctrl_ioctl','ctrl_write')
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
 (run/'runner.py').write_bytes(Path(__file__).read_bytes());(run/'test-guard.c').write_bytes((HERE/'test-guard.c').read_bytes());(run/'prepare.py').write_bytes((HERE/'prepare.py').read_bytes());(run/'candidate.patch').write_bytes((HERE/'emulator-overlay-handback.patch').read_bytes())
 phase_one=HERE.parent/'emulator-overlay-ownership/emulator-overlay-ownership.patch';(run/'phase-one.patch').write_bytes(phase_one.read_bytes())
 prep=subprocess.run([sys.executable,str(HERE/'prepare.py'),'--emu',str(a.emu.resolve()),'--output',str(run/'prepared')],capture_output=True,text=True)
 (run/'prepare.stdout').write_text(prep.stdout);(run/'prepare.stderr').write_text(prep.stderr)
 if prep.returncode:raise RuntimeError('prepare failed; retained '+str(run))
 assert (run/'prepared/applied-1.patch').read_bytes()==(run/'phase-one.patch').read_bytes()
 assert (run/'prepared/applied-2.patch').read_bytes()==(run/'candidate.patch').read_bytes()
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
 'instance_init_before_device_allocation':bool(re.search(r'int ret, i, n_added = 0, ctrl_added = 0;\s*ret = mixer_instance_init\(\);\s*if \(ret\)\s*return ret;\s*ret = alloc_chrdev_region',extract(src,'subucom_virt_init'))),
 'instance_init_registered': 'module_init(subucom_virt_init);' in src,
 'rng_kernel_header':'#include <linux/random.h>' in src,
 'crc_ops_set_get':'.set = crc_lengths_set,' in src and '.get = crc_lengths_get,' in src,
 }
 for line in ('sizeof(struct az_overlay_command) == 48','sizeof(struct az_overlay_frame) == 200','sizeof(struct az_overlay_state) == 208','offsetof(struct az_overlay_command, instance) == 8','offsetof(struct az_overlay_command, run_id) == 24','offsetof(struct az_overlay_command, epoch) == 32','offsetof(struct az_overlay_command, sequence) == 40','offsetof(struct az_overlay_frame, frame) == 48','offsetof(struct az_overlay_state, instance) == 8','offsetof(struct az_overlay_state, frame) == 56','AZ_OVERLAY_BYTES == MISO_SIZE'):
  anchors['static_assert '+line]='static_assert('+line+');' in src
 assert all(anchors.values()),anchors
 (run/'module-init.c').write_text(extract(src,'subucom_virt_init'))
 f={name:extract(src,name) for name in FUNCTIONS}
 declaration_names=('spi_crc_len','n_crc','interval_us','testmode_held')
 declarations=''
 for name in declaration_names:
  m=re.findall(r'^static [^;\n]*\b'+name+r'[^;\n]*;',src,re.M);assert len(m)==1,(name,m);declarations+=m[0]+'\n'
 start=src.index('static DEFINE_SPINLOCK(miso_lock);');end=src.index('/* MOSI:',start)
 globals_source=src[start:end].replace(f['mixer_instance_init'],'')
 prefix=declarations+globals_source
 (run/'global-declarations.inc').write_text(prefix)
 (run/'functions').mkdir()
 for name,text in f.items():(run/'functions'/ (name+'.c')).write_text(text)
 # The actual V2 header is unedited; these include shims encode native Linux
 # 64-bit UAPI for a host compiler, not a compat/kernel execution proof.
 (run/'linux').mkdir()
 (run/'linux/types.h').write_text('#include <stdint.h>\ntypedef uint32_t __u32;typedef int32_t __s32;typedef uint8_t __u8;\n#define __aligned_u64 uint64_t __attribute__((aligned(8)))\n')
 (run/'linux/ioctl.h').write_text('#define _IOW(t,n,s) ((1u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n#define _IOR(t,n,s) ((2u<<30)|((unsigned)sizeof(s)<<16)|((unsigned)(t)<<8)|(n))\n')
 for name in ('subucom_ownership.h','subucom_frames.h'):(run/name).write_bytes((mod/name).read_bytes())
 full=prefix+'\n'.join(f.values());variants={'candidate':(full,'all',[])}
 def mutant(name,fn,text,case,label):
  changed=dict(f);changed[fn]=text;variants[name]=(prefix+'\n'.join(changed.values()),case,[label])
 # Each mutant changes an actual extracted function and is required to fail
 # exactly its named functional oracle, without sanitizer diagnostics.
 mutant('wait-error-ignored','mixer_instance_init',replace_once(f['mixer_instance_init'],'    if (ret)\n        return ret;','    (void)ret;'),'init-wait','random_wait_failure_before_fill')
 mutant('zero-instance-accepted','mixer_instance_init',replace_once(f['mixer_instance_init'],'    if (!(mixer_instance[0] | mixer_instance[1]))\n        return -EIO;',''),'init-zero','zero_instance_refused')
 old='''    if (memcmp(h->instance, mixer_instance, sizeof(mixer_instance))) {
        ret = -ESTALE;
    } else if (cmd == AZ_OVERLAY_CLAIM) {'''
 mutant('instance-not-bound','ctrl_ioctl',replace_once(f['ctrl_ioctl'],old,'    if (cmd == AZ_OVERLAY_CLAIM) {'),'instance','instance_identity_required')
 mutant('missing-file-identity','mixer_owned_next',replace_once(f['mixer_owned_next'],'    if (mixer_owner != filp)\n        return -EACCES;','    (void)filp;'),'owner','file_identity_required')
 old='''                mixer_epoch++;
                mixer_sequence = 0;'''
 mutant('rotation-keeps-epoch','ctrl_ioctl',replace_once(f['ctrl_ioctl'],old,'                mixer_sequence = 0;'),'rotation','rotation_commits_frame_epoch_and_zero_sequence')
 mutant('rotation-keeps-sequence','ctrl_ioctl',replace_once(f['ctrl_ioctl'],old,'                mixer_epoch++;\n                mixer_sequence = h->sequence;'),'rotation','rotation_commits_frame_epoch_and_zero_sequence')
 mutant('rotation-omits-frame','ctrl_ioctl',replace_once(f['ctrl_ioctl'],'                memcpy(inject_pending[OWNED_BUS], request.frame, MISO_SIZE);','                if (cmd == AZ_OVERLAY_FRAME)\n                    memcpy(inject_pending[OWNED_BUS], request.frame, MISO_SIZE);'),'rotation','rotation_commits_frame_epoch_and_zero_sequence')
 mutant('rotation-wraps-epoch','ctrl_ioctl',replace_once(f['ctrl_ioctl'],'        if (!ret && cmd == AZ_OVERLAY_ROTATE && mixer_epoch == U64_MAX)\n            ret = -EOVERFLOW;',''),'overflow','rotation_epoch_exhaustion_unchanged')
 mutant('old-epoch-accepted','mixer_owned_next',replace_once(f['mixer_owned_next'],'h->run_id != mixer_run || h->epoch != mixer_epoch','h->run_id != mixer_run'),'stale-rotation','copied_old_generation_refused_after_rotation')
 old='                has_inject[OWNED_BUS] = 1;'
 mutant('rotation-unlocked-between-frame-and-epoch','ctrl_ioctl',replace_once(f['ctrl_ioctl'],old,old+'\n                spin_unlock_irqrestore(&miso_lock, flags);\n                spin_lock_irqsave(&miso_lock, flags);'),'atomic-rotation','rotation_is_one_locked_state_transition')
 result={'base_commit':receipt['base_commit'],'base_sha256':receipt['base_sha256'],'phase_one_patch_sha256':receipt['phase_one_patch_sha256'],'phase_one_sha256':receipt['phase_one_sha256'],'patch_sha256':receipt['patch_sha256'],'candidate_sha256':receipt['candidate_sha256'],'runner_sha256':sha(Path(__file__).read_bytes()),'test_sha256':sha((HERE/'test-guard.c').read_bytes()),'prepare_sha256':sha((HERE/'prepare.py').read_bytes()),'source_integration_checks':anchors,'extracted_function_sha256':{n:sha(s) for n,s in f.items()},'module_init_sha256':sha(extract(src,'subucom_virt_init')),'declarations_sha256':sha(prefix),'shim_header_sha256':{n:sha((run/'linux'/n).read_bytes()) for n in ('types.h','ioctl.h')},'runs':[],
 'scope':'Exact extracted candidate methods with pthread mutex, owned usercopy and kernel-service shims. Synthetic frames, deterministic RNG values/interleavings and concurrent reads/writes. V2 instance binding and same-open-file-description rotation only. Phase-one checks run on the new source as interaction regression. No entropy quality, real kernel parser, VFS reference lifecycle, kernel registration execution, module install, device, broker generation retention, native cancellation, recovery or producer handback proof.'}
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
   expected_checks=EXPECTED_CHECKS[case]
   if not match or int(match[1])!=expected_checks or failed!=sorted(expected) or int(match[2])!=len(expected) or proc.returncode!=bool(expected):raise RuntimeError('unexpected functional result; retained '+str(directory))
   result['runs'].append({'variant':variant,'mode':mode,'case':case,'checks':int(match[1]),'failures':failed,'exit_code':proc.returncode,'output_sha256':sha(output),'extraction_sha256':sha(body)})
   print(variant,mode,match[0],flush=True)
 for source,snapshot in ((HERE/'test-guard.c',run/'test-guard.c'),(Path(__file__),run/'runner.py'),(HERE/'prepare.py',run/'prepare.py'),(HERE/'emulator-overlay-handback.patch',run/'candidate.patch'),(phase_one,run/'phase-one.patch')):assert source.read_bytes()==snapshot.read_bytes(),'source changed during run'
 raw=json.dumps(result,indent=2,sort_keys=True)+'\n';(run/'result.json').write_text(raw)
 if a.result:a.result.write_text(raw)
 print('RESULT',sha(raw),'private snapshot',run)
if __name__=='__main__':main()

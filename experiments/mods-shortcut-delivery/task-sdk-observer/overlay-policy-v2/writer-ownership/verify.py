#!/usr/bin/env python3
"""Pinned local source audit and fake-transport Rust sender counterexamples."""
import argparse,ast,hashlib,json,os,re,subprocess,uuid
from pathlib import Path
HERE=Path(__file__).resolve().parent
PINS={
 'az-preview/src/main.rs':'a13cf052beca509185cb71a56e4196624e0cf6e34d5e62ea43e8fe5f03abecde',
 'az-preview/src/app.rs':'f6a7bb28c3f2dbce759582ac2c7b1147adf93f5137838a833ed5d35f1834864b',
 'az-preview/src/boot.rs':'f907da478aa758d4cb2a9f740f529fbe256376230d77635a061a746ce75ec1eb',
 'az-preview/src/controls.rs':'b9145e4b41439761abbfdec461b854412748af2fe95183fd5843ad34d4298445',
 'az-preview/src/config.rs':'0a1986611ec2642379a42f1f517d8d1decf1764e0cee8ab2f37c3e6c04782992',
 'tools/az-drive.py':'6f88798ec54785dc2ce9180e5c2839a1a8e8ec370c997bc7f48e8d1cc06e640f',
 'tools/azemu.py':'fdc5674f1729fafa3d0f0c48cd3877780e5dddd95b26249c0d0c9ec0334baeb5',
 'tools/az-boot-m1.sh':'65b4c3b298c51819ccb6dbed526883bdc9a47ce74e29e096c4eccf1d9b2bc285',
 'guest/subucom/forwarder.c':'88ea53aa7a3e0dd022a9b0ca494737cf799eb82b6c56546e076a7493a1879c57',
 'guest/subucom/live.c':'cbc4a9242a0377a8b2fbec93aed36d59cc5e972f138cb9bcd1200bbe7d8806d4',
 'guest/modules/subucom_virt/subucom_virt.c':'9631ca05810408eb9cd40cb38fa3f90d8dd2ac1d4d09a2e73d084ea0dbe03654',
 'initramfs-patch/patch-rootfs.d/11-virtio-stream-services.sh':'8c123e8d0ad2f353c33e45ef4a7ca44b5a390ea98ca042b96246267cf1f848f4',
 'guest/ep122_shim/gpio_inject.c':'2209d984e0cda5195df3a2779fd320d129ba29e38ad21c83cffa5faf56428107',
}
KERNEL='guest/modules/subucom_virt/subucom_virt.c'
FORWARD='guest/subucom/forwarder.c'
SERVICE='initramfs-patch/patch-rootfs.d/11-virtio-stream-services.sh'
CHECKS=[]
def sha(b):return hashlib.sha256(b).hexdigest()
def check(ok,label):
 if not ok:raise ValueError(label)
 CHECKS.append(label)
def block(text,pattern):
 m=re.search(pattern,text);check(m is not None,'extract '+pattern)
 start=m.start();i=text.index('{',m.end());depth=0
 while i<len(text):
  if text.startswith('//',i):i=text.index('\n',i);continue
  if text.startswith('/*',i):i=text.index('*/',i+2)+2;continue
  if text[i]=='"':
   i+=1
   while i<len(text):
    if text[i]=='\\':i+=2
    elif text[i]=='"':i+=1;break
    else:i+=1
   continue
  c=re.match(r"'(?:[^'\\\n]|\\.)'",text[i:]) if text[i]=="'" else None
  if c:i+=len(c[0]);continue
  if text[i]=='{':depth+=1
  elif text[i]=='}':
   depth-=1
   if not depth:return text[start:i+1]
  i+=1
 raise ValueError('unterminated extraction')
def fn(s,name):return block(s,r'(?:pub )?fn '+re.escape(name)+r'\b[^\{]*')
def cfn(s,name):return block(s,r'static\s+\w+\s+'+re.escape(name)+r'\s*\([^\{]*')
def without_comments(s):return re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S)
def pins(src):
 for p,h in PINS.items():check(sha(src[p].encode())==h,'pin '+p)
def audit(src):
 k=src[KERNEL];f=src[FORWARD];s=src[SERVICE]
 for n in ('ctrl_open','ctrl_release'):
  body=without_comments(cfn(k,n)).split('{',1)[1].rsplit('}',1)[0]
  check(body.strip()=='return 0;',n+' no owner state')
 w=without_comments(cfn(k,'ctrl_write'))
 check(len(re.findall(r'\bfilp\b',w))==1,'ctrl_write ignores opener file object')
 check(re.search(r'if\s*\(count == 0\)',w) is not None and 'has_inject[i] = 0' in w,'zero length clears all')
 check('if (count == 1)' in w and 'has_inject[bus] = 0' in w,'one byte clears selected bus')
 check('if (count < 1 + MISO_SIZE)' in w,'short overlay rejected')
 check('memcpy(inject_pending[bus], tmp, MISO_SIZE)' in w and 'has_inject[bus] = 1' in w,'whole sticky overlay replacement')
 check(w.index('spin_lock_irqsave',w.index('u8 tmp')) < w.index('memcpy(inject_pending') < w.index('spin_unlock_irqrestore',w.index('memcpy(inject_pending')),'replacement serialized but no ownership')
 r=cfn(k,'spi_read')
 check(r.index('spin_unlock_irqrestore') < r.index('len = spi_crc_len[bus]'),'CRC parameter read outside overlay lock')
 check('module_param_array(spi_crc_len, int, &n_crc, 0644)' in k,'CRC parameter writable0644')
 check('static int spi_crc_len[N_SPI] = { 28, 38, 96, 38 }' in k,'default CRC array')
 check('.write   = ctrl_write' in k and '.release = ctrl_release' in k,'ctrl fops write and release')
 check('.unlocked_ioctl = spi_ioctl' in k and 'memcpy(mosi_frame[bus], led, LED_FRAME_SIZE)' in cfn(k,'spi_ioctl'),'SPI ioctl publishes separate MOSI')
 check('open(CTRL_PATH, O_WRONLY)' in cfn(f,'open_ctrl_w'),'forwarder control descriptor writable')
 check('write_exact(ctrl_wfd, frame, AZ_CTRL_SIZE)' in f,'forwarder full control forwarding')
 check('read_exact(vport_fd, frame, AZ_CTRL_SIZE)' in f and '#define AZ_CTRL_SIZE 153' in f,'forwarder stream framing')
 check('Restart=always' in s and 'RestartSec=2s' in s,'forwarder automatic restart policy')
 check('mixer_tx_publish' in f and 'level_thread' in f and 'led_thread' in f,'forwarder has other outputs')
 main=src['az-preview/src/main.rs'];controls=src['az-preview/src/controls.rs'];app=src['az-preview/src/app.rs']
 check('let (ctrl_tx, ctrl_rx) = channel()' in main,'preview normal mpsc queue')
 sender=fn(controls,'ctrl_thread')
 check('while let Ok(msg) = rx.try_recv()' in sender,'disconnected sender drains queue')
 check('let replay = latest.messages()' in sender,'latest replay selection')
 check('replay.iter().any(|m| send(&mut stream, m).is_err())' in sender,'replay sends before recv')
 check(sender.index('latest.note(&msg)',sender.index('let Ok(msg) = rx.recv()')) < sender.index('send(&mut stream, &msg).is_err()'),'remember before possibly failing write')
 check('boot::stop(launched)' in fn(app,'on_exit') and 'EmuCmd::Quit' in fn(app,'on_exit'),'preview exit may stop owned QEMU')
 check('drop(child.stdin.take())' in fn(src['az-preview/src/boot.rs'],'stop'),'preview stop closes supervisor lifetime pipe')
 tree=ast.parse(src['tools/az-drive.py']);send=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='send')
 ssh=[n for n in ast.walk(send) if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and isinstance(n.func.value,ast.Name) and n.func.value.id=='azemu' and n.func.attr=='ssh']
 check(len(ssh)==2 and all(isinstance(n.args[0],ast.Constant) and n.args[0].value=='cat > /dev/subucom_ctrl' for n in ssh),'driver direct SSH bypass and fallback')
 check('for attempt in (0, 1)' in ast.get_source_segment(src['tools/az-drive.py'],send),'driver retries transport')
 check('root@127.0.0.1' in src['tools/azemu.py'],'helper root local SSH')
 boot=src['tools/az-boot-m1.sh']
 check('socket,id=vctrl,path=$SOCK/ctrl.sock,server=on,wait=off' in boot,'QEMU host control endpoint')
 check('"$SOCK/mixer.state"' in boot and 'rm -f "$SOCK/knob.ctr"' in boot,'new boot clears saved host state')
 check('GPIO_PORT "/dev/virtio-ports/cdj3k.gpio"' in src['guest/ep122_shim/gpio_inject.c'] and 'g_gpio_level[gpio] = level' in src['guest/ep122_shim/gpio_inject.c'],'separate GPIO input route')
 check('open(dev, O_RDWR)' in src['guest/subucom/live.c'] and not re.search(r'\b(?:write|ioctl)\s*\(',without_comments(src['guest/subucom/live.c'])),'live viewer holds writable descriptor without explicit write or ioctl')
 return sender

def rejected(label,fnc):
 before=len(CHECKS)
 try:fnc()
 except ValueError:del CHECKS[before:];return label
 raise AssertionError('control accepted '+label)

def execute_fixture(folder,source,name,expected):
 path=folder/(name+'.rs');path.write_text(source)
 p=subprocess.run(['rustc','--edition=2021','-C','opt-level=1','-o',str(folder/name),str(path)],capture_output=True,text=True,timeout=60)
 (folder/(name+'-build.log')).write_text(p.stdout+p.stderr)
 if p.returncode:raise RuntimeError('fixture build failed '+name)
 env=os.environ.copy();env.pop('AZ_CTRL_LOG',None)
 p=subprocess.run([str(folder/name)],capture_output=True,text=True,timeout=10,env=env)
 (folder/(name+'.log')).write_text(p.stdout+p.stderr)
 fails=re.findall(r'^FAIL (.+)$',p.stdout,re.M)
 if fails!=expected or p.returncode!=(1 if expected else 0):raise AssertionError((name,p.returncode,fails,expected))
 if not re.search(r'checks=17 failures='+str(len(expected))+r'\n$',p.stdout):raise AssertionError('fixture accounting')
 return {'checks':17,'failures':fails,'stdout_sha256':sha(p.stdout.encode())}

def run(root):
 src={p:(root/p).read_bytes().decode() for p in PINS};pins(src);audit(src)
 c=src['az-preview/src/controls.rs']
 pieces={'latest':block(c,r'#\[derive\(Default\)\]\s*pub struct LatestFrames[^\n]*;\s*impl LatestFrames\s*'),
         'wire':fn(c,'wire_frame'),'sender':fn(c,'ctrl_thread')}
 # Exact extracted definitions are retained privately, not copied into this repository.
 template=(HERE/'test-sender.rs').read_text()
 actual='\n'.join(pieces.values())+'\n'+template
 folder=HERE/'private'/('run-'+uuid.uuid4().hex);folder.mkdir(parents=True)
 (folder/'sources.json').write_text(json.dumps(PINS,sort_keys=True,indent=2)+'\n')
 candidate=execute_fixture(folder,actual,'candidate',[])
 mutants={
  'drop-disconnected-latest':(actual.replace('latest.note(&msg);','/* removed by negative control */',1),['disconnected-keeps-latest','disconnected-persists-replay']),
  'drop-before-write-note':(actual.replace('            latest.note(&msg);\n            if send','            /* removed by negative control */\n            if send',1),['uncertain-write-is-replayed','uncertain-success-persistence']),
  'skip-replay':(actual.replace('if replay.iter().any(|m| send(&mut stream, m).is_err()) {','if false {',1),['disconnected-keeps-latest','uncertain-write-is-replayed']),
 }
 mutation_results={}
 for name,(mutated,expected) in mutants.items():
  if mutated==actual:raise AssertionError('control not applied '+name)
  mutation_results[name]=execute_fixture(folder,mutated,name,expected)
 controls=[]
 for p in PINS:
  m=dict(src);m[p]+='\n';controls.append(rejected('source drift '+p,lambda m=m:pins(m)))
 changes=[(KERNEL,'return 0;\n}\n\nstatic int ctrl_release','return -1;\n}\n\nstatic int ctrl_release','open semantics'),
  (KERNEL,'module_param_array(spi_crc_len, int, &n_crc, 0644)','module_param_array(spi_crc_len, int, &n_crc, 0444)','CRC writability'),
  (KERNEL,'len = spi_crc_len[bus]','len = 96','CRC dynamic read'),
  (SERVICE,'Restart=always','Restart=no','service automatic restart'),
  (FORWARD,'write_exact(ctrl_wfd, frame, AZ_CTRL_SIZE)','write_exact(vport_fd, frame, AZ_CTRL_SIZE)','forwarding target')]
 for p,old,new,name in changes:
  m=dict(src)
  if old not in m[p]:raise AssertionError('control target absent '+name)
  m[p]=m[p].replace(old,new,1);controls.append(rejected(name,lambda m=m:audit(m)))
 result={'scope':'offline source binding and actual extracted Rust sender with real mpsc queues and fake transport/sleep/log/persistence; no socket, process, emulator or kernel operations',
  'source_sha256':PINS,'static_checks_passed':len(CHECKS),'static_controls':controls,'static_controls_passed':len(controls),
  'rustc':subprocess.check_output(['rustc','--version'],text=True).strip(),'extraction_sha256':{k:sha(v.encode()) for k,v in pieces.items()},
  'fixture_source_sha256':sha(template.encode()),'candidate':candidate,'executed_controls':mutation_results,
  'ownership':'No exclusive overlay or CRC lease exists in these sources. Process/socket snapshots and the coordinator lock do not establish writer ownership.',
  'limits':['No actual UnixStream, reconnect timing, kernel buffering, Linux access control or native consumption is exercised.',
   'A fake write error may follow delivery; repeated send proves source retry behavior, not that real transport delivered twice.',
   'The bounded source inventory is not a census of all installed binaries, arbitrary root commands or future writers.',
   'No live exclusion/queue drain has been performed, and no runtime gate is opened.']}
 (folder/'result.json').write_text(json.dumps(result,sort_keys=True,indent=2)+'\n')
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,required=True);p.add_argument('--result',type=Path,required=True);a=p.parse_args()
 result=run(a.source_root);a.result.write_text(json.dumps(result,sort_keys=True,indent=2)+'\n')
 print(json.dumps({'static_checks':result['static_checks_passed'],'static_controls':result['static_controls_passed'],'sender_checks':17,'executed_controls':len(result['executed_controls']),'result_sha256':sha(a.result.read_bytes())},sort_keys=True))

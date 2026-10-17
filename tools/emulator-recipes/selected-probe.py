import sys,time,subprocess,importlib.util
from pathlib import Path
p=Path('outputs/az-modding/xdj-az-emu/tools').resolve();sys.path.insert(0,str(p));import azemu
s=importlib.util.spec_from_file_location('d',p/'az-drive.py');d=importlib.util.module_from_spec(s);s.loader.exec_module(d)
o=Path('work/selected-probe');o.mkdir(exist_ok=True)
for n,(side,name) in enumerate([(1,'decksel1'),(1,'decksel3'),(1,'decksel1'),(2,'decksel4'),(2,'decksel2')]):
 d.press(side,name,.7);time.sleep(.5)
 r=subprocess.run(azemu._ssh_base()+['dd if=/dev/subucom_ctrl bs=157 count=140 2>/dev/null'],capture_output=True,check=True)
 (o/(str(n)+'.bin')).write_bytes(r.stdout)
 frames=[r.stdout[i+1:i+153] for i in range(0,len(r.stdout)-156,157) if r.stdout[i]==0]
 print(n,name,'frames',len(frames),flush=True)
 if frames: print('first',frames[0][:64].hex(),flush=True)

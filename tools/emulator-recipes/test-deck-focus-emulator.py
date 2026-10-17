import sys,time,importlib.util
from pathlib import Path
p=Path('outputs/az-modding/xdj-az-emu/tools').resolve();sys.path.insert(0,str(p));import azemu
s=importlib.util.spec_from_file_location('d',p/'az-drive.py');d=importlib.util.module_from_spec(s);s.loader.exec_module(d)
o=Path('outputs/az-modding/selected-deck-preview').resolve();o.mkdir(exist_ok=True);d.SHOT_DIR=str(o)
q=d.Qmp();q.tap(500,230);time.sleep(1);q.tap(1500,380);time.sleep(1);d.knobclick(.7);time.sleep(2)
d.panel('search',.7);time.sleep(1);q.tap(1250,70);time.sleep(.3)
for xy in [(480,1320),(1080,1480),(1080,1480),(780,1150),(680,1320),(680,1320)]:q.tap(*xy);time.sleep(.35)
time.sleep(1)
f=Path('work/selected-probe/all-load.bin');f.write_bytes(bytes([2]+d._mixer(33,15,True)));azemu.push_file(f,'/tmp/selected-all-load.bin')
f=Path('work/selected-probe/idle.bin');f.write_bytes(bytes([2]+d._mixer()));azemu.push_file(f,'/tmp/selected-idle.bin')
print(azemu.ssh('cat /tmp/selected-all-load.bin > /dev/subucom_ctrl; sleep 0.2; cat /tmp/selected-idle.bin > /dev/subucom_ctrl'),flush=True)
time.sleep(4);q.tap(684,68);time.sleep(1);q.shot('startup')
for label,commands in [('selected-1-2',[(1,'decksel1'),(2,'decksel2')]),('selected-3-2',[(1,'decksel3')]),('selected-3-4',[(2,'decksel4')]),('selected-1-4',[(1,'decksel1')])]:
 for side,name in commands:d.press(side,name,.7);time.sleep(.3)
 time.sleep(.5);print(q.shot(label),flush=True)
q.tap(420,68);time.sleep(1);q.shot('two-deck-hidden');q.tap(684,68);time.sleep(1);q.shot('four-deck-return')

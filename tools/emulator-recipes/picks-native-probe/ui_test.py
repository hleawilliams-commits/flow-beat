import sys,time,importlib.util
from pathlib import Path
p=Path('outputs/az-modding/xdj-az-emu/tools').resolve();sys.path.insert(0,str(p));import azemu
s=importlib.util.spec_from_file_location('drive',p/'az-drive.py');d=importlib.util.module_from_spec(s);s.loader.exec_module(d)
q=d.Qmp();q.tap(500,230);time.sleep(1);q.tap(1500,380);time.sleep(1);d.knobclick();time.sleep(1)
d.panel('search',1);time.sleep(1)
for xy in [(480,1320),(1080,1480),(1080,1480),(780,1150),(680,1320),(680,1320)]:q.tap(*xy);time.sleep(.35)
time.sleep(1);d.load(1,.7);time.sleep(3);q.shot('picks-030-waveform')
q.tap(160,52);time.sleep(2);q.shot('picks-030-opened')

import importlib.util,sys,time,json
from pathlib import Path
root=Path('outputs/az-modding/xdj-az-emu/tools').resolve()
sys.path.insert(0,str(root))
spec=importlib.util.spec_from_file_location('azdrive',root/'az-drive.py'); d=importlib.util.module_from_spec(spec); spec.loader.exec_module(d)
with Path('outputs/az-modding/stems-four-deck-test/events.jsonl').open('a') as f: f.write(json.dumps({'time':time.time(),'event':'Requested simultaneous load of Access Denied on decks 1-4'})+'\n')
d.send(d.BUS_MIXER,d._mixer(33,0x0f,True)); time.sleep(.15); d.send(d.BUS_MIXER,d._mixer())
print('Sent all four LOAD buttons together')

import sys,time,json,re
from pathlib import Path
sys.path.insert(0,'outputs/az-modding/xdj-az-emu/tools'); import azemu
out=Path('outputs/az-modding/stems-four-deck-test/telemetry.jsonl')
start=time.monotonic()
with out.open('a') as f:
 for i in range(90):
  rc,s=azemu.ssh("printf 'PID '; pidof EP147; grep -E 'MemAvailable|MemFree' /proc/meminfo; cat /tmp/azstem.probestat; cat /tmp/master_out.capture.txt 2>/dev/null",timeout=10)
  row={'wall_time':time.time(),'elapsed':round(time.monotonic()-start,2),'rc':rc,'text':s}
  f.write(json.dumps(row)+'\n'); f.flush()
  if i%10==0: print(row['elapsed'],s.splitlines()[:3],re.findall(r'deck\d ctrl=\S+ frames=\d+',s),flush=True)
  time.sleep(2)

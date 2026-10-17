import numpy as np,json,re
from pathlib import Path
out=Path('outputs/az-modding/stems-four-deck-test')
p=Path('work/stems-four-deck-capture.wav'); n=(p.stat().st_size-68)//32
x=np.memmap(p,dtype='<i4',mode='r',offset=68,shape=(n,8))
events=[json.loads(s) for s in (out/'events.jsonl').read_text().splitlines()]
telemetry=[json.loads(s) for s in (out/'telemetry.jsonl').read_text().splitlines()]
res={'recording_seconds':n/44100,'pids':sorted(set(re.findall(r'PID (\d+)','\n'.join(a['text'] for a in telemetry)))),'min_available_mib':min(int(re.search(r'MemAvailable:\s+(\d+)',a['text'])[1]) for a in telemetry)/1024,'reload_checks':[],'rail_samples':[]}
def longest(z):
 b=np.diff(np.r_[False,z,False].astype('i1')); starts=np.where(b==1)[0]; ends=np.where(b==-1)[0]
 return int((ends-starts).max()) if len(starts) else 0
for e in [a for a in events if 'audio_s' in a and a['event'].startswith('Load ')]:
 t=e['audio_s']; decks=[0,1,2] if 'deck 4 while' in e['event'] else [0,3]
 checks=[]
 for d in decks:
  a=x[int((t-3)*44100):min(n,int((t+8)*44100)),d*2:d*2+2]
  checks.append({'deck':d+1,'longest_zero_ms':longest(np.all(a==0,axis=1))/44.1,'longest_held_ms':longest(np.all(a[1:]==a[:-1],axis=1))/44.1,'rail_samples':int(np.count_nonzero((a==2147483647)|(a==-2147483648)))})
 res['reload_checks'].append({'event':e,'checks':checks})
for d in range(4):
 a=x[:,2*d:2*d+2]; rail=(a==2147483647)|(a==-2147483648)
 frames=np.flatnonzero(np.any(rail,axis=1))
 res['rail_samples'].append({'deck':d+1,'samples':int(rail.sum()),'first_audio_s':float(frames[0]/44100) if len(frames) else None,'last_audio_s':float(frames[-1]/44100) if len(frames) else None})
# Positive control: deck 4 must go silent while the other decks continue.
mute=next(a['audio_s'] for a in events if a['event'].startswith('Intentional'))
restore=next(a['audio_s'] for a in events if a['event'].startswith('Restored'))
a=x[int((mute+2)*44100):int((restore-3)*44100)]
res['mute_control']={'window':[mute+2,restore-3],'channel_nonzero_samples':[int(np.count_nonzero(a[:,d*2:d*2+2])) for d in range(4)]}
(out/'audio-analysis.json').write_text(json.dumps(res,indent=2)+'\n')
print(json.dumps(res,indent=2))

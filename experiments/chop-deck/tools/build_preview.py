#!/usr/bin/env python3
"""Embed private local demo audio; never commit the resulting preview."""
import argparse, base64, json
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('manifest',type=Path)
p.add_argument('output',type=Path)
a=p.parse_args()
m=json.loads(a.manifest.read_text())
paths=[m['backing']['mp3']]+[s['mp3'] for s in m['slices']]
data={'bpm':m['bpm'],'loopSeconds':m['backing']['duration_s'],'audio':[base64.b64encode(Path(f).read_bytes()).decode('ascii') for f in paths]}
template=(Path(__file__).resolve().parents[1]/'demo.html').read_text()
assert template.count('__AUDIO_DATA__')==1
out=template.replace('__AUDIO_DATA__',json.dumps(data,separators=(',',':')))
assert len(out.encode())<1_000_000
assert '\\"' not in out
assert not any(x in out for x in ['fetch(', 'XMLHttpRequest', 'WebSocket'])
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(out)
print(f'Built {a.output} ({len(out.encode())} bytes)')

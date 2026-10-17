from pathlib import Path
from collections import Counter
src=Path('/tmp/az-led-debug.log')
state={1:{'seq':None,'next':0,'data':bytearray()},3:{'seq':None,'next':0,'data':bytearray()}}
counts=Counter(); saved=0
for line in src.read_text().splitlines():
    try: w=bytes.fromhex(line)
    except ValueError: continue
    if len(w)<153 or w[0] not in state: continue
    bus=w[0]; f=w[1:153]; s=state[bus]
    if f[0x4c]!=1 or f[0x50] not in (0x10,0x20): continue
    seq=int.from_bytes(f[0x4e:0x50],'little')
    if seq==s['seq']: continue
    s['seq']=seq; counts[(bus,f[0x51])]+=1
    if f[0x51]!=0x33: continue
    i=int.from_bytes(f[0x52:0x54],'little'); total=int.from_bytes(f[0x54:0x56],'little')
    if i==1: s['data']=bytearray(); s['next']=1
    if not i or i!=s['next']: s['next']=0; continue
    s['data']+=f[0x56:0x90]; s['next']+=1
    if i!=total: continue
    s['next']=0; raw=s['data']; n=int.from_bytes(raw[:3],'little'); jpg=raw[3:3+n]
    if len(jpg)==n and jpg[:2]==b'\xff\xd8':
        out=Path('outputs/az-modding/camelot-jog-preview')/f'jog-{1 if bus==3 else 2}-received.jpg'
        out.write_bytes(jpg); saved+=1
        print(f'{out}: {n} JPEG bytes, {total} bus packets')
print('Message counts',dict(counts),'completed JPEGs',saved)

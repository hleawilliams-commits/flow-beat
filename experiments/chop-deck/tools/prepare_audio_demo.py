#!/usr/bin/env python3
"""Build private Chop Deck practice assets from an existing local stem set.

Requires ffmpeg/ffprobe. Does not modify source stems. Audio outputs must stay
outside Git. This preview is a rendered musical sketch, not hardware validation.
"""
import argparse, array, base64, json, math, subprocess, wave
from pathlib import Path

RATE = 44100

def run(args):
    return subprocess.check_output(args)

def decode(path, start, duration):
    raw = run(['ffmpeg','-v','error','-ss',str(start),'-i',str(path),'-t',str(duration),'-ar',str(RATE),'-ac','2','-f','f32le','-'])
    a = array.array('f'); a.frombytes(raw)
    return a[:round(duration*RATE)*2]

def normalize(a, peak):
    old = max(abs(x) for x in a) or 1
    return array.array('f', (x * peak / old for x in a))

def fade(a, ms=5):
    a = array.array('f', a)
    n = min(int(RATE*ms/1000), len(a)//4)
    for i in range(n):
        gain = i/max(1,n-1)
        for c in (0,1):
            a[2*i+c] *= gain
            a[len(a)-2*(i+1)+c] *= gain
    return a

def write(path, a):
    pcm = array.array('h',(round(max(-1,min(1,x))*32767) for x in a))
    with wave.open(str(path),'wb') as f:
        f.setnchannels(2); f.setsampwidth(2); f.setframerate(RATE); f.writeframes(pcm.tobytes())
    mp3 = path.with_suffix('.mp3')
    subprocess.run(['ffmpeg','-v','error','-y','-i',str(path),'-codec:a','libmp3lame','-b:a','96k',str(mp3)],check=True)
    return {'wav':str(path.resolve()),'mp3':str(mp3.resolve()),'duration_s':len(a)/(2*RATE),'peak_dbfs':round(20*math.log10(max(abs(x) for x in a) or 1e-10),2),'rms_dbfs':round(20*math.log10(math.sqrt(sum(x*x for x in a)/len(a)) or 1e-10),2),'mp3_bytes':mp3.stat().st_size}

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--stems',type=Path,required=True)
    p.add_argument('--track',type=Path,required=True,help='Original track, used only for beat-grid metadata')
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--phrase-start',type=float,default=51.76)
    p.add_argument('--slice-duration',type=float,default=.3)
    p.add_argument('--slice-hop',type=float,default=.18)
    p.add_argument('--backing-beat',type=int,default=64)
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    meta=json.loads(run(['ffprobe','-v','error','-show_entries','format_tags','-of','json',str(args.track)]))['format']['tags']
    grid=json.loads(base64.b64decode(meta['BEATGRID']))
    bpm=float(grid['tempo']); beat=60/bpm; backing_start=grid['beats'][0]+args.backing_beat*beat
    length=round(16*beat*RATE)*2
    backing=array.array('f',[0])*length
    for name in ('drums','bass','other'):
        data=decode(args.stems/(name+'.ogg'),backing_start,16*beat)
        for i,x in enumerate(data[:length]): backing[i]+=x
    backing=fade(normalize(backing,.59))
    vocal=normalize(decode(args.stems/'vocals.ogg',args.phrase_start,8*args.slice_duration),.76)
    manifest={'title':'Chop Deck — local practice sketch','artist':meta.get('ARTIST'),'track':meta.get('TITLE'),'source':'Existing user-prepared local stems; no source files altered or published.','bpm':bpm,'sample_rate':RATE,'backing_start_s':backing_start,'backing_beats':16,'phrase_start_s':args.phrase_start,'slice_mode':'Eight overlapping 300 ms fragments at 180 ms intervals; manually selected phrase, not automatic word detection. Slice peaks balanced for pad playback.','backing':write(args.output/'backing-110bpm.wav',backing),'phrase':write(args.output/'original-phrase.wav',fade(vocal)),'slices':[]}
    slices=[]; size=round(args.slice_duration*RATE)*2
    for i in range(8):
        offset=round(i*args.slice_hop*RATE)*2
        s=fade(normalize(vocal[offset:offset+size],.68)); slices.append(s)
        entry=write(args.output/f'chop-{i+1:02d}.wav',s); entry.update({'pad':i+1,'name':f'CHOP {i+1}','start_s':args.phrase_start+i*args.slice_hop})
        manifest['slices'].append(entry)
    # 12 bars: 4 bars original phrase; 4 bars rearranged chops; 4 bars crossfader performance.
    total=round(48*beat*RATE); preview=array.array('f',[0])*(total*2)
    vocal_lane=array.array('f',[0])*(total*2)
    def put(dst,src,when,gain=1):
        at=round(when*RATE)*2
        for j,x in enumerate(src):
            if 0<=at+j<len(dst): dst[at+j]+=x*gain
    for bar in (0,2): put(vocal_lane,fade(vocal),bar*4*beat+.5*beat)
    pattern=[0,0,3,1,6,4,2,7,0,3,0,4,6,7,2,1]
    for i,pad in enumerate(pattern*2): put(vocal_lane,slices[pad],(16+i*.5)*beat)
    for i,pad in enumerate([0,0,0,3,4,6,7,7]*4): put(vocal_lane,slices[pad],(32+i*.5)*beat)
    for frame in range(total):
        position=frame/RATE/beat
        if position<16: ga,gb=.75,.9
        elif position<32: ga,gb=.70,1.0
        else:
            # Simulate fader from backing (A) to chopped vocal (B), then sharp cuts.
            local=position-32
            x=min(1,local/6) if local<8 else (0 if int((local-8)*2)%4==0 else 1)
            ga,gb=math.cos(x*math.pi/2),math.sin(x*math.pi/2)
        for c in (0,1): preview[2*frame+c]=backing[(2*frame+c)%len(backing)]*ga+vocal_lane[2*frame+c]*gb
    preview=fade(normalize(preview,.82),ms=20)
    manifest['preview']=write(args.output/'chop-deck-preview.wav',preview)
    manifest['preview']['sections']=[{'start_s':0,'label':'Original phrase with backing'},{'start_s':16*beat,'label':'Rearranged chops over backing'},{'start_s':32*beat,'label':'Crossfade to isolated chops and rhythmic cuts'}]
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))

if __name__=='__main__': main()

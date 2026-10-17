import importlib.util, sys, time, json, re
from pathlib import Path

root = Path('outputs/az-modding/xdj-az-emu/tools').resolve()
sys.path.insert(0, str(root))
import azemu
spec = importlib.util.spec_from_file_location('drive', root/'az-drive.py')
d = importlib.util.module_from_spec(spec); spec.loader.exec_module(d)
out = Path('outputs/az-modding/stems-reload-fix').resolve()
d.SHOT_DIR = str(out)
q = d.Qmp()
events = []

def record(event, **kw):
    row = dict(event=event, time=time.time(), **kw)
    events.append(row)
    (out/'emulator-events.json').write_text(json.dumps(events, indent=2))
    print(json.dumps(row), flush=True)

def state(label):
    rc, s = azemu.ssh('pidof EP147; grep MemAvailable /proc/meminfo; cat /tmp/azstem.probestat; cat /home/root/settings/azstem.curtrack*')
    assert rc == 0
    (out/(label+'.txt')).write_text(s)
    return s

def wait_frames(deck, frames, timeout=15):
    end = time.monotonic()+timeout
    while time.monotonic()<end:
        rc, s = azemu.ssh('cat /tmp/azstem.probestat')
        if re.search(rf'deck{deck-1} ctrl=\S+ frames={frames}\b', s):
            return
        time.sleep(.5)
    raise AssertionError(f'deck {deck} did not reach frames={frames}: {s}')

def search(name):
    d.panel('search'); time.sleep(.7)
    q.tap(1250,70); time.sleep(.3)
    coords = [(480,1320),(1080,1480),(1080,1480),(780,1150),(680,1320),(680,1320)] if name=='access' else [(1080,1480),(1980,1150),(1680,1480),(1080,1320),(780,1150),(1180,1150),(1180,1150),(1780,1150)]
    for x,y in coords:
        q.tap(x,y); time.sleep(.35)
    time.sleep(1)

def logtext():
    return azemu.ssh('cat /tmp/azstem.log')[1]

log_start = len(logtext())
try:
    search('confetti')
    d.send(d.BUS_MIXER,d._mixer(33,0x0f,True));time.sleep(.15);d.send(d.BUS_MIXER,d._mixer())
    for deck in range(1,5): wait_frames(deck,10209856)
    record('four simultaneous initial loads succeeded')
    state('initial-four-loaded')
    for deck in range(1,5):
        search('access');d.load(deck);wait_frames(deck,8599552)
        search('confetti')
        before=logtext()
        rc,_=azemu.ssh('nohup /tmp/stems-memory-pressure > /tmp/stems-memory-pressure.log 2>&1 < /dev/null &')
        time.sleep(2)
        pressure=azemu.ssh('cat /tmp/stems-memory-pressure.log')[1]
        assert 'READY' in pressure,pressure
        d.load(deck);time.sleep(3);wait_frames(deck,0)
        after=logtext();new=after[len(before):]
        assert f'stems REFUSED d{deck-1}' in new,new[-2000:]
        state(f'deck{deck}-refused');q.shot(f'deck{deck}-refused')
        record('memory refusal confirmed',deck=deck,pressure=pressure.strip())
        # Repeated reloads must not spin the stem decoder while RAM stays low.
        before=logtext()
        d.panel('search');time.sleep(.7);d.load(deck);time.sleep(5)
        new=logtext()[len(before):]
        retries=new.count(f'stems: retry d{deck-1}')
        assert retries==0,(deck,retries,new[-3000:])
        wait_frames(deck,0)
        record('no decode retry loop under memory pressure',deck=deck)
        side = 1 if deck in (1,3) else 2
        d.press(side, f'decksel{deck}'); time.sleep(.3)
        d.press(side, 'play'); time.sleep(2)
        azemu.ssh('kill $(pidof stems-memory-pressure)')
        s1=state(f'deck{deck}-playing-memory-released')
        time.sleep(5)
        wait_frames(deck,0) # no automatic switch to stems under a moving track
        s2=state(f'deck{deck}-playing-still-passthrough')
        pat=rf'deck{deck-1} .* obs=(\d+)'
        assert int(re.search(pat,s2)[1]) > int(re.search(pat,s1)[1])
        record('playing deck kept native audio after memory recovered',deck=deck)
        before=logtext()
        d.press(side, 'play') # pause; automatic recovery can now occur
        wait_frames(deck,10209856)
        time.sleep(2)
        new=logtext()[len(before):]
        assert new.count(f'stems: retry d{deck-1}')==1,new[-3000:]
        assert f'stems loaded d{deck-1}' in new
        state(f'deck{deck}-recovered')
        record('paused deck recovered stems without changing track',deck=deck)
    q.shot('all-four-recovered')
    state('final-four-loaded')
    record('PASS all four decks recovered; moving decks left untouched')
finally:
    azemu.ssh('pidof stems-memory-pressure >/dev/null && kill $(pidof stems-memory-pressure)')
    (out/'emulator-azstem.log').write_text(logtext()[log_start:])

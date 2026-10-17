import importlib.util
import json
from pathlib import Path
import tempfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('recovery', Path(__file__).with_name('mod_recovery.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
results = []

def ok(label):
    results.append('PASS ' + label)
    print(results[-1], flush=True)

def fails(fn):
    try:
        fn()
    except (RuntimeError, OSError):
        return
    raise AssertionError('Expected refusal')

with tempfile.TemporaryDirectory(prefix='recovery-test-') as tmp:
    t = Path(tmp); mods = t/'USB/MODS'; kit = t/'kit'
    (mods/'community-mods/flow-beat').mkdir(parents=True)
    (mods/'deck').mkdir()
    kit.mkdir()
    (mods/'azmod.so').write_bytes(b'official core 1')
    (mods/'azstem.so').write_bytes(b'official stems 1')
    (mods/'deck/loader').write_bytes(b'official loader 1')
    (mods/'community-mods/flow-beat/flow-beat.so').write_bytes(b'working version')
    cfg = mods/'azmods.cfg'
    cfg.write_text('version = 1\ncommunity-mods.flow-beat = on\nflow-beat:show = 1\ncore.theme = old\n')
    saved = r.snapshot(mods, kit, 'working', 'test-uuid')
    r.verify_snapshot(saved, 'test-uuid')
    ok('complete backup verified against source and SHA-256 manifest')
    (mods/'azmod.so').write_bytes(b'official core 2')
    (mods/'azstem.so').write_bytes(b'official stems 2')
    (mods/'deck/loader').write_bytes(b'official loader 2')
    cfg.write_text('version = 1\ncommunity-mods = on\ncommunity-mods.flow-beat = off\nflow-beat:show = 0\ncore.theme = new\n')
    (mods/'community-mods/flow-beat/flow-beat.so').write_bytes(b'broken version')
    protected = r.protected(mods)
    r.disable(mods, kit, 'test-uuid')
    assert r.cfg_parse(cfg.read_bytes())[1]['community-mods'] == 'off'
    assert r.protected(mods) == protected
    ok('disable only changes community switch, leaving official core intact')
    r.restore(mods, kit, saved, 'test-uuid')
    values = r.cfg_parse(cfg.read_bytes())[1]
    assert (mods/'community-mods/flow-beat/flow-beat.so').read_bytes() == b'working version'
    assert r.protected(mods) == protected
    assert values['core.theme'] == 'new' and values['flow-beat:show'] == '1'
    assert values['community-mods.flow-beat'] == 'on' and 'community-mods' not in values
    ok('community rollback preserves a newer official core, loader, and unrelated settings')
    before = r.inventory(mods)
    original = saved/'MODS/community-mods/flow-beat/flow-beat.so'
    original.write_bytes(b'tampered')
    fails(lambda: r.restore(mods, kit, saved, 'test-uuid'))
    assert r.inventory(mods) == before
    original.write_bytes(b'working version')
    ok('corrupt backup refused before USB writes')
    fails(lambda: r.restore(mods, kit, saved, 'wrong-uuid'))
    assert r.inventory(mods) == before
    ok('wrong USB backup identity refused before writes')
    cfg.write_text('version = 2\ncommunity-mods = on\n')
    before = r.inventory(mods)
    fails(lambda: r.disable(mods, kit, 'test-uuid'))
    fails(lambda: r.restore(mods, kit, saved, 'test-uuid'))
    assert r.inventory(mods) == before
    ok('future settings version refused without rewriting it')
    cfg.write_text('version = 1\ncommunity-mods = on\ncore.theme = new\n')
    (mods/'community-mods/flow-beat/flow-beat.so').write_bytes(b'current before failed restore')
    real_replace = r.os.replace
    def fail_install(src, dst):
        if '.recovery-stage-' in str(src) and Path(dst) == mods/'community-mods':
            raise OSError('simulated failed rename')
        return real_replace(src, dst)
    with patch.object(r.os, 'replace', side_effect=fail_install):
        fails(lambda: r.restore(mods, kit, saved, 'test-uuid'))
    assert (mods/'community-mods/flow-beat/flow-beat.so').read_bytes() == b'current before failed restore'
    assert r.cfg_parse(cfg.read_bytes())[1]['community-mods'] == 'off'
    assert r.protected(mods) == protected
    ok('failed swap restores prior community files and leaves them disabled')
    (mods/'community-mods/escape').symlink_to(t)
    fails(lambda: r.snapshot(mods, kit, 'symlink', 'test-uuid'))
    ok('symlink payload refused')

Path(__file__).with_name('TEST RESULTS.txt').write_text('\n'.join(results) + '\nAll restore/disable tests ran on temporary folders, not the physical USB.\n')

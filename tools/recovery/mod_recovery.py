#!/usr/bin/env python3
"""Local USB backup/recovery. Never installs or restores AZ-MODS core binaries."""
import argparse
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone

HERE = Path(__file__).resolve().parent
ID_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def inventory(root):
    result = {}
    if not root.exists():
        return result
    if root.is_symlink():
        raise RuntimeError('Symlink refused: ' + str(root))
    for p in sorted(root.rglob('*')):
        if any(x.startswith('._') for x in p.relative_to(root).parts):
            continue  # macOS AppleDouble metadata is not a mod payload
        if p.is_symlink():
            raise RuntimeError('Symlink refused: ' + str(p))
        if p.is_file():
            result[p.relative_to(root).as_posix()] = digest(p)
        elif not p.is_dir():
            raise RuntimeError('Special file refused: ' + str(p))
    return result


def copy_tree(src, dst):
    inventory(src)  # refuse links before copying
    shutil.copytree(src, dst, copy_function=shutil.copyfile,
                    ignore=shutil.ignore_patterns('._*'))


def sync_dir(path):
    fd = os.open(str(path), os.O_RDONLY)
    try:
        try:
            os.fsync(fd)
        except OSError as e:
            if e.errno not in (errno.EINVAL, errno.ENOTSUP):
                raise
    finally:
        os.close(fd)


def atomic_write(path, data):
    fd, tmp = tempfile.mkstemp(prefix='.' + path.name + '.', dir=str(path.parent))
    tmp = Path(tmp)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        if tmp.read_bytes() != data:
            raise RuntimeError('Write verification failed: ' + str(path))
        os.replace(tmp, path)
        sync_dir(path.parent)
    finally:
        if tmp.exists():
            tmp.unlink()


def cfg_parse(data):
    if len(data) > 65536:
        raise RuntimeError('Settings exceed 64 KiB; refusing to rewrite them.')
    text = data.decode('utf-8')
    values = {}
    for line in text.splitlines():
        s = line.strip()
        if s and not s.startswith('#') and '=' in s:
            k, v = s.split('=', 1)
            values[k.strip()] = v.strip()
    if values.get('version') != '1':
        raise RuntimeError('Unknown settings version; use the current AZ-MODS app for recovery.')
    return text, values


def cfg_replace(data, remove, values):
    text, _ = cfg_parse(data)
    lines = []
    for line in text.splitlines():
        s = line.strip()
        key = s.split('=', 1)[0].strip() if '=' in s and not s.startswith('#') else None
        if key is None or not remove(key):
            lines.append(line)
    lines.extend(k + ' = ' + v for k, v in sorted(values.items()))
    result = ('\n'.join(lines) + '\n').encode()
    cfg_parse(result)
    return result


def mod_ids(root):
    if not root.exists():
        return set()
    return {p.name for p in root.iterdir() if p.is_dir() and ID_RE.fullmatch(p.name)}


def protected(mods):
    return {p: h for p, h in inventory(mods).items()
            if not p.startswith('community-mods/') and p != 'azmods.cfg'
            and not p.startswith('.recovery-')}


def snapshot(mods, kit, reason, volume_uuid):
    backups = kit / 'backups'
    backups.mkdir(mode=0o700, parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    dest = backups / stamp
    dest.mkdir(mode=0o700)
    try:
        before = inventory(mods)
        copy_tree(mods, dest / 'MODS')
        if inventory(dest / 'MODS') != before or inventory(mods) != before:
            raise RuntimeError('USB changed during backup, or copy verification failed.')
        meta = dict(volume_uuid=volume_uuid, reason=reason, files=before,
                    created_utc=stamp, format=1)
        atomic_write(dest / 'manifest.json', (json.dumps(meta, indent=2) + '\n').encode())
        return dest
    except BaseException:
        shutil.rmtree(dest)
        raise


def verify_snapshot(saved, expected_uuid):
    meta = json.loads((saved / 'manifest.json').read_text())
    if meta.get('format') != 1 or meta.get('volume_uuid') != expected_uuid:
        raise RuntimeError('Backup format or USB identity mismatch.')
    if inventory(saved / 'MODS') != meta['files']:
        raise RuntimeError('Backup integrity check failed; nothing restored.')
    return meta


def disable(mods, kit, volume_uuid):
    before = protected(mods)
    cfg = mods / 'azmods.cfg'
    data = cfg.read_bytes()
    replacement = cfg_replace(data, lambda k: k == 'community-mods', {'community-mods': 'off'})
    saved = snapshot(mods, kit, 'before disabling community mods', volume_uuid)
    atomic_write(cfg, replacement)
    if protected(mods) != before:
        raise RuntimeError('Protected files changed during operation; inspect the backup before continuing.')
    return saved


def restore(mods, kit, saved, volume_uuid):
    verify_snapshot(saved, volume_uuid)
    source = saved / 'MODS' / 'community-mods'
    if not source.is_dir():
        raise RuntimeError('This restore point has no community-mods folder.')
    current = mods / 'community-mods'
    before = protected(mods)
    cfg = mods / 'azmods.cfg'
    original_cfg = cfg.read_bytes()
    _, old_settings = cfg_parse((saved / 'MODS' / 'azmods.cfg').read_bytes())
    ids = mod_ids(current) | mod_ids(source)
    def community_key(k):
        return k == 'community-mods' or k.startswith('community-mods.') or k.split(':', 1)[0] in ids
    desired_cfg = cfg_replace(original_cfg, community_key,
                              {k: v for k, v in old_settings.items() if community_key(k)})
    off_cfg = cfg_replace(original_cfg, lambda k: k == 'community-mods', {'community-mods': 'off'})
    # Verified pre-change backup is independent of the chosen restore point.
    safety = snapshot(mods, kit, 'before community restore', volume_uuid)
    stage = Path(tempfile.mkdtemp(prefix='.recovery-stage-', dir=str(mods)))
    old = mods / ('.recovery-previous-' + safety.name)
    moved = installed = False
    try:
        copy_tree(source, stage / 'community-mods')
        if inventory(stage / 'community-mods') != inventory(source):
            raise RuntimeError('Staged community files failed verification.')
        for p in (stage / 'community-mods').rglob('*'):
            if p.is_file():
                with p.open('rb') as f:
                    os.fsync(f.fileno())
        # An interrupted swap leaves community mods disabled; Kyle's core remains.
        atomic_write(cfg, off_cfg)
        if current.exists():
            os.replace(current, old)
            moved = True
        os.replace(stage / 'community-mods', current)
        installed = True
        sync_dir(mods)
        if inventory(current) != inventory(source):
            raise RuntimeError('Restored community files failed read-back verification.')
        atomic_write(cfg, desired_cfg)
        if protected(mods) != before:
            raise RuntimeError('Protected files changed during operation.')
    except BaseException:
        # Ordinary failures roll the folder back. Keep community mods off on error.
        atomic_write(cfg, off_cfg)
        if installed:
            shutil.rmtree(current)
        if moved:
            os.replace(old, current)
        raise
    finally:
        shutil.rmtree(stage, ignore_errors=True)
    if old.exists():
        shutil.rmtree(old)  # its verified copy is in the Mac's safety snapshot
    return safety


def volume_guard(volume, expected_uuid):
    info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(volume)]))
    if info.get('VolumeUUID') != expected_uuid or info.get('Internal') is not False:
        raise RuntimeError('This is not the registered external FLOWMATIC USB. Nothing changed.')
    if Path(info.get('MountPoint', '')).resolve() != volume.resolve():
        raise RuntimeError('USB mount point mismatch.')
    mods = volume / 'MODS'
    if mods.is_symlink() or not mods.is_dir():
        raise RuntimeError('Expected a real MODS folder on the registered USB.')
    return mods


def main():
    os.umask(0o077)
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action', choices=['backup', 'save-restore-point', 'verify', 'disable-community', 'restore-community'])
    ap.add_argument('--volume', type=Path, default=Path('/Volumes/FLOWMATIC'))
    a = ap.parse_args()
    registration = json.loads((HERE / 'registered-usb.json').read_text())
    uuid = registration['volume_uuid']
    mods = volume_guard(a.volume, uuid)
    with (HERE / '.operation.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        point = HERE / 'restore-point.json'
        if a.action in ('backup', 'save-restore-point'):
            saved = snapshot(mods, HERE, a.action, uuid)
            if a.action == 'save-restore-point':
                atomic_write(point, json.dumps({'snapshot': saved.name}).encode())
            print('Verified backup:', saved)
        elif a.action == 'disable-community':
            saved = disable(mods, HERE, uuid)
            print('Community mods disabled. Kyle\'s core files were left unchanged. Backup:', saved)
        else:
            name = json.loads(point.read_text())['snapshot']
            if Path(name).name != name or name in ('.', '..'):
                raise RuntimeError('Invalid restore-point name.')
            saved = HERE / 'backups' / name
            verify_snapshot(saved, uuid)
            if a.action == 'verify':
                print('Restore-point integrity verified:', saved)
            else:
                safety = restore(mods, HERE, saved, uuid)
                print('Saved community mods restored. Current official core preserved. Safety backup:', safety)
        print('Safely eject the USB in Finder before returning it to the deck.')


if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        raise SystemExit('Stopped: ' + str(e))

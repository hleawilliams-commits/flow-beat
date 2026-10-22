#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline module binding and pure identity/frame policy; no device/network I/O.

The CLI reads only local artifacts and Git objects. Import performs no I/O and
requires no ELF dependencies. Receipt checks do not establish how it was acquired.
"""
import argparse
import copy
import hashlib
import io
import json
from pathlib import Path
import stat
import struct
import subprocess

EMULATOR_COMMIT = '06fc981707e69982c0554bb508e0c646c005a979'
MODULE_SHA256 = 'c97d19459a3e99116de70c0f3f781527baf710846a158a9ba051c36537fc338d'
BUILD_ID = 'd8e5bc2ea181713ce4d69f8baebec6b23362de2e'
NOTE = bytes.fromhex('040000001400000003000000474e5500' + BUILD_ID)
SYSFS_REALPATH = '/sys/devices/virtual/subucom_virt_class/subucom_spi3.0'
SOURCES = {
 'guest/modules/subucom_virt/subucom_virt.c': '9631ca05810408eb9cd40cb38fa3f90d8dd2ac1d4d09a2e73d084ea0dbe03654',
 'guest/modules/subucom_virt/subucom_frames.h': '4d9d833e583b7c6a5aa3f12de0fea2b7245e9dac915477ab96b9cfdfa75c2c0b',
 'guest/modules/subucom_virt/Makefile': 'a85021fa4fb059ccbb89c3c2db6659720bcf8dfd80e826831b2363474f2e1be6',
 'docker/Dockerfile': 'eaff7007f1ff5107ccd21eb70e64e1d28473b993684336f7e44f25f470317a62',
 'build.sh': '551dc1d3f3d9ed758a00a9571e93caa070266e9f7a6e774f820eec6da62a4db9',
}
IMAGES = {
 'build/Image': '4aa9e89f0c6f7c0869c94be6ea556e75d4a1093b5dc2cd129c194ff63c2caf08',
 'build/docker-out/Image': '4aa9e89f0c6f7c0869c94be6ea556e75d4a1093b5dc2cd129c194ff63c2caf08',
 'build/initramfs-patched.cpio.gz': 'fe98cf4a36c97a080afbec868189982d487318428b69d14b4542750531531eba',
}
FUNCTIONS = {
 'spi_open': (0x0, 24, '4b33dda8e74379196ef694df6838b2f26fbfae7e66985ace48c0719ab944a41b'),
 'spi_release': (0x18, 8, '2f6110664dbd16b514a0c2c7c8326932a36e85d2f15b91e57ca76efd2c7a625a'),
 'spi_poll': (0x20, 8, 'db9a09ae82f1d2215486903f3be242f72c5cbfebb026edc96d685ce8e88ccede'),
 'spi_read': (0x26c, 612, '73216e834b211dbae5d44d3f8320fec40a9ff9a49716f0d4f8b4c57b7a39a280'),
 'ctrl_read': (0x38, 564, '57c4c39a1a7c939a0a372b29e2a38d6faf6517bfe75da76fb449abe435257a6b'),
}
FOPS = {
 0x0: ('__this_module', 0, 257),
 0x10: ('.text', 0x26c, 257),
 0x40: ('.text', 0x20, 257),
 0x48: ('.text', 0x70c, 257),
 0x68: ('.text', 0x0, 257),
 0x78: ('.text', 0x18, 257),
}
CALLS = {
 0x2b0: 'memset', 0x2b8: '__usecs_to_jiffies',
 0x2c0: 'schedule_timeout_interruptible', 0x2e8: '_raw_spin_lock_irqsave',
 0x394: '_raw_spin_unlock_irqrestore', 0x44c: '__arch_copy_to_user',
 0x4cc: '__stack_chk_fail',
}

def sha(data):
    return hashlib.sha256(data).hexdigest()


def identity_errors(receipt):
    """Fail closed on copied identity facts. No acquisition or coherence claim."""
    if not isinstance(receipt, dict):
        return ('receipt is not an object',)
    r, errors = receipt, []
    def need(ok, name):
        if not ok:
            errors.append(name)
    need(r.get('disk_sha256') == MODULE_SHA256, 'module disk hash')
    need(r.get('loaded_build_id') == BUILD_ID, 'loaded build ID')
    # Bind the full note header/owner/size, not an arbitrary substring.
    need(r.get('loaded_note_hex') == NOTE.hex(), 'exact loaded GNU note')
    need(r.get('module_state') == 'live', 'module live state')
    mode = r.get('node_mode')
    need(type(mode) is int and mode >= 0 and stat.S_ISCHR(mode), 'character node')
    keys = ('node_rdev_major', 'node_rdev_minor', 'sysfs_dev_major',
            'sysfs_dev_minor', 'proc_devices_major')
    ints = all(type(r.get(k)) is int for k in keys)
    need(ints, 'device number types')
    need(ints and 0 < r['node_rdev_major'] < 4096 and
         r['node_rdev_major'] == r['sysfs_dev_major'] == r['proc_devices_major'],
         'dynamic major binding')
    need(ints and r['node_rdev_minor'] == r['sysfs_dev_minor'] == 2,
         'mixer minor binding')
    need(r.get('sysfs_name') == 'subucom_spi3.0', 'sysfs device name')
    need(r.get('sysfs_class') == 'subucom_virt_class', 'sysfs class')
    need(r.get('proc_devices_name') == 'subucom_virt', 'registered driver name')
    need(r.get('sysfs_class_realpath') == SYSFS_REALPATH and
         r.get('sysfs_char_realpath') == SYSFS_REALPATH, 'sysfs character mapping')
    lens = r.get('crc_lengths')
    need(type(lens) is list and len(lens) == 4 and
         all(type(x) is int and 0 <= x <= 150 for x in lens) and lens[2] == 96,
         'mixer CRC parameter')
    return tuple(errors)


def crc16_x25(data):
    crc = 0xffff
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0x8408 if crc & 1 else 0)
    return crc ^ 0xffff


def validate_frame(frame):
    """Return copied frame facts or raise ValueError. Does not read any device."""
    if type(frame) is not bytes or len(frame) != 128:
        raise ValueError('expected exactly 128 copied bytes')
    crc = crc16_x25(frame[:96])
    if struct.unpack_from('<H', frame, 96)[0] != crc:
        raise ValueError('mixer CRC-16/X25 mismatch')
    return dict(length=128, crc_prefix=96, crc16_x25=crc,
                **{f'byte{i}': frame[i] for i in (6,31,32,33,34,35)})


def fops_errors(actual):
    return () if actual == FOPS else ('SPI file-operation relocations',)


def read_body_errors(instructions, calls):
    """Narrow decoded body check, not a whole-kernel side-effect proof."""
    errors = []
    if calls != CALLS:
        errors.append('read helper calls')
    for ins in instructions:
        mnemonic, operands = ins['mnemonic'], ins['op_str']
        if mnemonic.startswith('st') and '[sp' not in operands:
            errors.append('non-stack store in spi_read')
        if mnemonic in ('blr', 'br'):
            errors.append('indirect branch in spi_read')
        if mnemonic.startswith('b') or mnemonic in ('cbz','cbnz','tbz','tbnz'):
            if mnemonic == 'bl':
                if ins['address'] not in calls:
                    errors.append('unbound read helper call')
            else:
                # Every immediate branch in the selected body is local.
                immediate = operands.rsplit('#', 1)[-1]
                try:
                    target = int(immediate, 0)
                except ValueError:
                    errors.append('unparsed read branch')
                    continue
                if not 0x26c <= target < 0x4d0:
                    errors.append('read branch outside body')
    return tuple(errors)


def synthetic_checks(check):
    good = dict(disk_sha256=MODULE_SHA256, loaded_build_id=BUILD_ID,
                loaded_note_hex=NOTE.hex(), module_state='live',
                node_mode=stat.S_IFCHR | 0o600, node_rdev_major=241, node_rdev_minor=2,
                sysfs_dev_major=241, sysfs_dev_minor=2, proc_devices_major=241,
                sysfs_name='subucom_spi3.0', sysfs_class='subucom_virt_class',
                proc_devices_name='subucom_virt', crc_lengths=[28,38,96,38],
                sysfs_class_realpath=SYSFS_REALPATH, sysfs_char_realpath=SYSFS_REALPATH)
    check(not identity_errors(good), 'synthetic identity accepted')
    other = copy.deepcopy(good)
    for key in ('node_rdev_major','sysfs_dev_major','proc_devices_major'):
        other[key] = 250
    check(not identity_errors(other), 'major is dynamically bound, not fixed')
    controls = []
    changes = {
        'disk_sha256': '0'*64, 'loaded_build_id': '0'*40,
        'loaded_note_hex': (b'\x00'+NOTE[1:]).hex(), 'module_state': 'going',
        'node_mode': stat.S_IFREG | 0o600, 'node_rdev_major': 0,
        'node_rdev_minor': 4, 'sysfs_dev_major': 240, 'sysfs_dev_minor': 1,
        'proc_devices_major': 242, 'sysfs_name': 'subucom_spi1.0',
        'sysfs_class': 'subucom_spiclass', 'proc_devices_name': 'other',
        'crc_lengths': [28,38,38,38], 'sysfs_class_realpath': '/sys/devices/other',
        'sysfs_char_realpath': SYSFS_REALPATH+'-other',
    }
    for key, value in changes.items():
        bad = copy.deepcopy(good); bad[key] = value
        check(bool(identity_errors(bad)), 'reject modified '+key)
        controls.append('modified '+key)
    for key in good:
        bad = copy.deepcopy(good); del bad[key]
        check(bool(identity_errors(bad)), 'reject missing '+key)
        controls.append('missing '+key)
    for label, value in [('boolean major', True), ('string major', '241'),
                         ('oversized major', 4096)]:
        bad = copy.deepcopy(good); bad['node_rdev_major'] = value
        check(bool(identity_errors(bad)), 'reject '+label); controls.append(label)
    for label, note in [('truncated note', NOTE[:-1]), ('extra note byte', NOTE+b'\0'),
                         ('bad owner', NOTE[:12]+b'BAD\0'+NOTE[16:]),
                         ('bad note type', NOTE[:8]+struct.pack('<I',1)+NOTE[12:]),
                         ('bad descriptor width', NOTE[:4]+struct.pack('<I',19)+NOTE[8:])]:
        bad = copy.deepcopy(good); bad['loaded_note_hex'] = note.hex()
        check(bool(identity_errors(bad)), 'reject '+label); controls.append(label)
    check(identity_errors(None) == ('receipt is not an object',), 'reject non-object receipt')
    controls.append('non-object receipt')
    check(crc16_x25(b'123456789') == 0x906e, 'independent X25 known-answer vector')
    frame = bytearray((i*7+11)&255 for i in range(128)); frame[34] = 203
    struct.pack_into('<H', frame, 96, crc16_x25(frame[:96]))
    raw = bytes(frame); result = validate_frame(raw)
    check(result['byte34'] == 203 and result['length'] == 128 and result['crc_prefix'] == 96,
          'synthetic mixer frame copied values')
    check(raw == bytes(frame), 'frame validation leaves bytes unchanged')
    frames = {'short frame': raw[:-1], 'long frame': raw+b'\0', 'mutable input': frame}
    for label, index in [('payload corruption',34),('CRC low corruption',96),('CRC high corruption',97)]:
        bad = bytearray(raw); bad[index] ^= 1; frames[label] = bytes(bad)
    for label, bad in frames.items():
        try:
            validate_frame(bad)
        except ValueError:
            check(True, 'reject '+label)
        else:
            check(False, 'reject '+label)
        controls.append(label)
    # Bytes beyond the CRC are intentionally not authenticated by this protocol.
    bad = bytearray(raw); bad[120] ^= 1
    check(validate_frame(bytes(bad)) == result, 'CRC does not cover tail bytes 98..127')
    return controls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', required=True, type=Path)
    parser.add_argument('--result', required=True, type=Path)
    args = parser.parse_args()
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
    from elftools.elf.elffile import ELFFile
    from elftools.elf.relocation import RelocationSection
    checks = []
    def check(ok, label):
        if not ok:
            raise AssertionError(label)
        checks.append(label)
    repo = args.emulator.resolve()
    commit = subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'], text=True).strip()
    check(commit == EMULATOR_COMMIT, 'emulator checkout commit')
    for relative, expected in SOURCES.items():
        pinned = subprocess.check_output(['git','-C',str(repo),'show',EMULATOR_COMMIT+':'+relative])
        check(sha(pinned) == expected, 'pinned source '+relative)
        check((repo/relative).read_bytes() == pinned, 'working source matches '+relative)
    for relative, expected in IMAGES.items():
        check(sha((repo/relative).read_bytes()) == expected, 'local artifact '+relative)
    data = (repo/'build/docker-out/modules/subucom_virt.ko').read_bytes()
    check(sha(data) == MODULE_SHA256, 'exact module hash')
    elf = ELFFile(io.BytesIO(data))
    check(elf.elfclass == 64 and elf.little_endian and elf['e_machine'] == 'EM_AARCH64'
          and elf['e_type'] == 'ET_REL', 'AArch64 ELF64 relocatable module')
    check(elf.get_section_by_name('.note.gnu.build-id').data() == NOTE, 'exact ELF GNU note')
    modinfo = elf.get_section_by_name('.modinfo').data().split(b'\0')
    check(b'name=subucom_virt' in modinfo, 'module name')
    check(b'vermagic=6.6.138-dirty SMP preempt mod_unload aarch64' in modinfo,
          'module kernel ABI string')
    symtab = elf.get_section_by_name('.symtab')
    def symbol(name):
        values = symtab.get_symbol_by_name(name)
        check(values is not None and len(values) == 1, 'unique symbol '+name)
        return values[0]
    text_section = elf.get_section_by_name('.text')
    functions = {}
    for name, (offset, size, expected) in FUNCTIONS.items():
        sym = symbol(name); section = elf.get_section(sym['st_shndx'])
        check(section.name == '.text' and sym['st_value'] == offset and sym['st_size'] == size,
              'extent '+name)
        body = section.data()[offset:offset+size]
        check(sha(body) == expected, 'code hash '+name)
        functions[name] = dict(section='.text', offset=offset, size=size, sha256=expected)
    relocation_tables = {}
    for section in elf.iter_sections():
        if not isinstance(section, RelocationSection):
            continue
        target = elf.get_section(section['sh_info']).name
        table = relocation_tables.setdefault(target,{})
        linked = elf.get_section(section['sh_link'])
        for rel in section.iter_relocations():
            sym = linked.get_symbol(rel['r_info_sym'])
            name = sym.name
            if not name and isinstance(sym['st_shndx'],int):
                name = elf.get_section(sym['st_shndx']).name
            table[rel['r_offset']] = (name,rel['r_addend'],rel['r_info_type'])
    fops = symbol('spi_fops')
    check(elf.get_section(fops['st_shndx']).name == '.rodata' and
          fops['st_value'] == 0 and fops['st_size'] == 264, 'SPI fops extent')
    actual = {offset: value for offset,value in relocation_tables['.rodata'].items() if 0 <= offset < 264}
    check(not fops_errors(actual), 'exact fops handler relocations')
    check(elf.get_section_by_name('.rodata').data()[0x18:0x20] == b'\0'*8 and 0x18 not in actual,
          'write slot is absent')
    bad = dict(actual); bad[0x10] = ('.text',0x38,257)
    check(bool(fops_errors(bad)), 'strict control rejects LED ctrl_read handler')
    controls = ['read handler replaced with ctrl_read']
    bad = dict(actual); bad[0x18] = ('.text',0x38,257)
    check(bool(fops_errors(bad)), 'strict control rejects added write handler')
    controls.append('write handler added')
    start, size, _ = FUNCTIONS['spi_read']; body = text_section.data()[start:start+size]
    instructions = [dict(address=x.address,mnemonic=x.mnemonic,op_str=x.op_str)
                    for x in Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN).disasm(body,start)]
    check(len(instructions)*4 == size, 'complete spi_read disassembly')
    calls = {address:value[0] for address,value in relocation_tables['.text'].items()
             if start <= address < start+size and value[2] == 283}
    check(not read_body_errors(instructions,calls), 'stack-only body stores and bound helper calls')
    bad_ins = copy.deepcopy(instructions)
    next(x for x in bad_ins if x['mnemonic'] == 'strb')['op_str'] = 'w0, [x20]'
    check('non-stack store in spi_read' in read_body_errors(bad_ins,calls),
          'strict control rejects persistent write')
    controls.append('body persistent write')
    bad_calls = dict(calls); bad_calls[0x2b0] = 'ctrl_write'
    check('read helper calls' in read_body_errors(instructions,bad_calls),
          'strict control rejects alternate helper call')
    controls.append('alternate body helper')
    crc = symbol('spi_crc_len')
    check(elf.get_section(crc['st_shndx']).name == '.data' and crc['st_value'] == 8 and
          crc['st_size'] == 16 and struct.unpack_from('<4i',elf.get_section_by_name('.data').data(),8)
          == (28,38,96,38), 'compiled CRC defaults')
    controls.extend(synthetic_checks(check))
    result = dict(schema=1, scope='offline exact module plus synthetic identity/frame policy; no runtime access',
        emulator_commit=commit, source_sha256=SOURCES, module_sha256=MODULE_SHA256, build_id=BUILD_ID,
        local_image_sha256=IMAGES, functions=functions,
        spi_fops_relocations={hex(k):list(v) for k,v in FOPS.items()},
        spi_read_helper_calls={hex(k):v for k,v in CALLS.items()},
        checks=len(checks), check_names=checks, rejection_controls=len(controls), control_names=controls,
        verifier_sha256=sha(Path(__file__).read_bytes()),
        limits=['no loaded-module or node observation', 'no device read',
                'no source-to-binary reproducible-build attestation',
                'body store/call checks do not prove external kernel helpers side-effect-free',
                'receipt values are not an atomic or fd-bound identity snapshot',
                'CRC authenticates only the 96-byte prefix, not bytes 98..127',
                'emulator MISO sample would not prove native input consumption'])
    args.result.parent.mkdir(parents=True,exist_ok=True)
    # Keep previous evidence intact: caller must choose a fresh result path.
    with args.result.open('x') as output:
        json.dump(result,output,indent=2,sort_keys=True); output.write('\n')
    print(json.dumps(dict(checks=len(checks), rejection_controls=len(controls), result_sha256=sha(args.result.read_bytes()))))

if __name__ == '__main__':
    main()

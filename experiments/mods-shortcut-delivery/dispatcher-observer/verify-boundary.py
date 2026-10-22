#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind observer constants to the exact private ELF and existing static evidence.

Read-only inputs. This does not execute firmware, claim a hook, or contact a device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

HERE=Path(__file__).resolve().parent
FIRMWARE_SHA='6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--elf',type=Path,required=True)
    ap.add_argument('--result',type=Path)
    args=ap.parse_args()
    blob=args.elf.read_bytes()
    source=(HERE/'observer.cpp').read_text()
    boundary=json.loads((HERE.parent/'dispatcher-boundary/static-result.json').read_text())
    checks=0
    def check(condition):
        nonlocal checks
        checks+=1
        if not condition: raise AssertionError('binding assertion '+str(checks))
    check(hashlib.sha256(blob).hexdigest()==FIRMWARE_SHA)
    check(blob[:6]==b'\x7fELF\x02\x01')
    phoff=struct.unpack_from('<Q',blob,32)[0]
    ents,num=struct.unpack_from('<HH',blob,54)
    def read(address,size):
        for i in range(num):
            kind,flags,off,va,pa,filesz,memsz,align=struct.unpack_from('<IIQQQQQQ',blob,phoff+i*ents)
            if kind==1 and va<=address and address+size<=va+filesz:
                return blob[off+address-va:off+address-va+size]
        raise AssertionError('unmapped static address')
    m=re.search(r'ENTRY=(0x[0-9a-f]+), SLOT=(0x[0-9a-f]+);',source)
    check(m is not None)
    entry,slot=(int(v,16) for v in m.groups())
    check(entry==0x1adeee0 and slot==0x2d61d98)
    words=re.search(r'const uint32_t expected\[\]=\{([^}]+)\};',source)
    check(words is not None)
    expected=[int(v.strip(),16) for v in words[1].split(',')]
    check(len(expected)==4)
    prologue=read(entry,16)
    check(prologue==struct.pack('<4I',*expected))
    check(struct.unpack('<Q',read(slot,8))[0]==entry)
    digest=hashlib.sha256(prologue).hexdigest()
    check(digest==boundary['candidates'][hex(entry)]['prologue_sha256'])
    check(boundary['firmware_sha256']==FIRMWARE_SHA)
    check(boundary['waveform_secondary_at_plus_0x118_slot_0x18']==hex(entry))
    result={'checks':checks,'firmware_sha256':FIRMWARE_SHA,'observer_source_sha256':hashlib.sha256(source.encode()).hexdigest(),
            'entry':hex(entry),'secondary_slot':hex(slot),'prologue_sha256':digest,
            'boundary_evidence_sha256':hashlib.sha256((HERE.parent/'dispatcher-boundary/static-result.json').read_bytes()).hexdigest(),
            'native_pointer_reads_in_observation':False,'hook_claim_attempted':False,
            'scope':'Static initialization binding only; instruction eligibility is recorded separately; runtime ownership/island/thread/view/input provenance remain unproven.'}
    if args.result: args.result.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()

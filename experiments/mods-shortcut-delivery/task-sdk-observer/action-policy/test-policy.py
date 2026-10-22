#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline policy checks with source-extracted idle values; no driver import.

The actual byte fixtures stay in memory and are never emitted in the result.
"""
import argparse
import ast
import builtins
import hashlib
import importlib.util
import json
from pathlib import Path
import re

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('pure_task_action_policy',HERE/'action-policy.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
checks=[]
controls=[]


def check(ok,name):
    if not ok:raise AssertionError(name)
    checks.append(name)


def reject(name,reason,fn):
    try:fn()
    except ValueError as error:
        check(str(error)==reason,name+': exact rejection')
        controls.append(dict(name=name,rejected_by=reason))
    else:raise AssertionError(name+': accepted')


def literal(node):
    if isinstance(node,ast.Constant):return node.value
    if isinstance(node,ast.List):return [literal(x) for x in node.elts]
    if isinstance(node,ast.Dict):return {literal(k):literal(v) for k,v in zip(node.keys,node.values)}
    if isinstance(node,ast.BinOp) and isinstance(node.op,ast.Add):return literal(node.left)+literal(node.right)
    if isinstance(node,ast.BinOp) and isinstance(node.op,ast.Mult):return literal(node.left)*literal(node.right)
    raise ValueError('unexpected pinned literal AST')


def driver_values(raw):
    check(hashlib.sha256(raw).hexdigest()==p.DRIVER_SHA256,'exact driver source hash')
    values={}
    for node in ast.parse(raw).body:
        if isinstance(node,ast.Assign):
            for target in node.targets:
                if isinstance(target,ast.Name) and target.id in ('SPI3_IDLE','MIXER_KNOBS','FX_BYTE'):
                    check(target.id not in values,'single driver literal '+target.id)
                    values[target.id]=literal(node.value)
    check(set(values)=={'SPI3_IDLE','MIXER_KNOBS','FX_BYTE'},'three selected literal declarations')
    return values


def stamp(frame):
    data=bytearray(frame);data[96:98]=p.crc16_x25(data[:96]).to_bytes(2,'little');return bytes(data)


def main(driver,frames):
    driver_raw=driver.read_bytes();values=driver_values(driver_raw)
    idle=bytes(values['SPI3_IDLE'])
    check(len(idle)==152 and hashlib.sha256(idle).hexdigest()==p.IDLE_SHA256,'source-extracted idle bytes hash')
    check(set(values['MIXER_KNOBS'].values())|{values['FX_BYTE']}==p.MIXER_OFFSETS and values['FX_BYTE']==p.FX_BYTE,
          'allowlist matches exact driver declarations')
    frames_raw=frames.read_bytes()
    check(hashlib.sha256(frames_raw).hexdigest()=='4d9d833e583b7c6a5aa3f12de0fea2b7245e9dac915477ab96b9cfdfa75c2c0b',
          'exact kernel frame source hash')
    matched=re.search(rb'az_frame_spi3_mixer\[152\]\s*=\s*\{([^}]+)\}',frames_raw)
    check(matched is not None,'single named kernel mixer template')
    kernel=bytes(int(v.strip(),16) for v in matched.group(1).split(b',') if v.strip())
    check(len(kernel)==152,'kernel source-extracted frame length')
    check(p.crc16_x25(b'123456789')==0x906e,'independent CRC X25 known-answer vector')
    check(p.parse_mixer_state(None)=={} and p.parse_mixer_state(b'{}')=={},'missing or empty object settings')
    state={key:(254 if key==p.FX_BYTE else 255) for key in p.MIXER_OFFSETS}
    encoded=json.dumps({str(k):v for k,v in state.items()}).encode()
    check(p.parse_mixer_state(encoded)==state,'all28 reviewed byte offsets parse')
    check(p.parse_mixer_state(b' { "10" : 0, "22": 254 }\n')=={10:0,22:254},'JSON whitespace and integer endpoints')
    original=dict(state)
    planned=p.neutral_frame(idle,state,128)
    check(state==original and hashlib.sha256(idle).hexdigest()==p.IDLE_SHA256,'neutral construction does not mutate inputs')
    check(all(planned[k]==v for k,v in state.items()) and planned[34]==128,'neutral construction applies only reviewed state and counter')
    observed=stamp(planned)
    check(p.validate_frames(observed,observed,planned)==128,'two stable neutral fullframes')
    for counter in (1,203,254):
        observed=stamp(p.neutral_frame(idle,state,counter))
        check(p.validate_frames(observed,observed,planned)==counter,'observed counter independent of pre-alignment planned value '+str(counter))
    # Planned driver CRC is cosmetic; observed frames alone require fresh valid CRC.
    changed=bytearray(planned);changed[96]^=0xff;changed[97]^=0x55
    observed=stamp(planned)
    check(p.validate_frames(observed,observed,bytes(changed))==128,'planned cosmetic CRC bytes ignored')

    cases=[
      ('empty bytes',b'','invalid mixer-state bytes'),
      ('oversized state',b' '*4097,'invalid mixer-state bytes'),
      ('mutable settings',bytearray(b'{}'),'invalid mixer-state bytes'),
      ('text settings','{}','invalid mixer-state bytes'),
      ('invalid UTF8',b'\xff','mixer state is not UTF-8'),
      ('broken JSON',b'{','invalid mixer JSON'),
      ('BOM JSON',b'\xef\xbb\xbf{}','invalid mixer JSON'),
      ('nonobject JSON',b'[]','mixer JSON must be an object'),
      ('null JSON',b'null','mixer JSON must be an object'),
      ('duplicate key',b'{"10":1,"10":2}','duplicate mixer key'),
      ('escaped duplicate key',b'{"10":1,"\\u0031\\u0030":2}','duplicate mixer key'),
      ('leading zero key',b'{"010":1}','mixer key must be canonical decimal'),
      ('signed key',b'{"+10":1}','mixer key must be canonical decimal'),
      ('negative key',b'{"-10":1}','mixer key must be canonical decimal'),
      ('whitespace key',b'{" 10":1}','mixer key must be canonical decimal'),
      ('Unicode digit key',b'{"\\u0661\\u0660":1}','mixer key must be canonical decimal'),
      ('unreviewed button key',b'{"32":64}','unreviewed mixer offset'),
      ('counter override',b'{"34":129}','unreviewed mixer offset'),
      ('tail override',b'{"151":1}','unreviewed mixer offset'),
      ('CRC override',b'{"96":1}','unreviewed mixer offset'),
      ('float value',b'{"10":1.0}','mixer value must be an integer byte'),
      ('boolean value',b'{"10":true}','mixer value must be an integer byte'),
      ('string value',b'{"10":"1"}','mixer value must be an integer byte'),
      ('negative value',b'{"10":-1}','mixer value must be an integer byte'),
      ('oversized value',b'{"10":256}','mixer value must be an integer byte'),
      ('null value',b'{"10":null}','mixer value must be an integer byte'),
      ('nested value',b'{"10":{"10":1}}','mixer value must be an integer byte'),
      ('list value',b'{"10":[1]}','mixer value must be an integer byte'),
      ('NaN value',b'{"10":NaN}','nonfinite JSON value'),
      ('Infinity value',b'{"10":Infinity}','nonfinite JSON value'),
      ('held FX state',b'{"22":1}','FX ON must be released'),
    ]
    for name,raw,reason in cases:reject(name,reason,lambda raw=raw:p.parse_mixer_state(raw))
    for name,bad,reason in [('string direct key',{'10':1},'unreviewed mixer offset'),
                          ('boolean direct key',{True:1},'unreviewed mixer offset'),
                          ('button direct state',{31:1},'unreviewed mixer offset'),
                          ('boolean direct value',{10:True},'mixer value must be an integer byte'),
                          ('held direct FX',{22:255},'FX ON must be released'),
                          ('non-dict direct state',[],'mixer state must be a dict')]:
        reject(name,reason,lambda bad=bad:p.neutral_frame(idle,bad,128))
    for value in (0,255,-1,256,True,1.0,'128'):
        reject('invalid counter '+repr(value),'counter outside 1..254',lambda v=value:p.neutral_frame(idle,{},v))
    changed=bytearray(idle);changed[10]^=1
    reject('changed input idle template','idle template hash mismatch',lambda:p.neutral_frame(bytes(changed),{},128))
    for name,bad in [('short idle',idle[:-1]),('long idle',idle+b'\0'),('mutable idle',bytearray(idle))]:
        reject(name,'expected exactly 152 immutable bytes',lambda bad=bad:p.neutral_frame(bad,{},128))

    planned=p.neutral_frame(idle,{},128);observed=stamp(planned)
    for slot in range(3):
        for suffix,bad in [('short',observed[:-1]),('long',observed+b'\0'),('mutable',bytearray(observed))]:
            inputs=[observed,observed,planned];inputs[slot]=bad
            reject(f'frame{slot} {suffix}','expected exactly 152 immutable bytes',lambda a=inputs:p.validate_frames(*a))
    for offset in (96,97):
        changed=bytearray(observed);changed[offset]^=1
        reject('observed CRC byte '+str(offset),'mixer CRC-16/X25 mismatch',lambda raw=bytes(changed):p.validate_frames(raw,observed,planned))
    changed=bytearray(observed);changed[34]=129;changed=stamp(changed)
    reject('different observed counters','observed counters differ',lambda:p.validate_frames(observed,changed,planned))
    for counter in (0,255):
        changed=bytearray(observed);changed[34]=counter;changed=stamp(changed)
        reject('observed boundary counter '+str(counter),'counter outside 1..254',lambda raw=changed:p.validate_frames(raw,raw,planned))
    reject('kernel placeholder mismatch','press latch must be released',lambda:p.validate_frames(stamp(kernel),stamp(kernel),planned))

    def reason_for(offset):
        if offset==6:return 'press latch must be released'
        if offset in (31,32,33,35):return 'panel and knob controls must be neutral'
        if offset==24:return 'MIXER SETTINGS must be released'
        if offset==22:return 'FX ON must be released'
        if offset==29:return 'X-PAD must be released'
        if offset not in p.PROTECTED_EXCLUDED:return 'non-mixer bytes differ from pinned neutral template'
        return 'observed frame differs from planned neutral frame'
    # Every compared byte has a discriminating corruption, including all24 extra tail bytes.
    for offset in range(152):
        if offset in (34,96,97):continue
        changed=bytearray(observed);changed[offset]^=0x10 if offset==24 else 1
        changed=stamp(changed)
        reject('observed byte '+str(offset),reason_for(offset),lambda raw=changed:p.validate_frames(raw,observed,planned))
    for offset in (6,22,24,29,31,32,33,35,128,151):
        changed=bytearray(planned);changed[offset]^=0x10 if offset==24 else 1
        # Adversarial supplied plan cannot bless a held control or changed tail.
        raw=stamp(changed)
        reject('matching bad plan '+str(offset),reason_for(offset),lambda raw=raw:p.validate_frames(raw,raw,raw))
    original_open=builtins.open
    def no_open(*args,**kwargs):raise AssertionError('policy attempted ambient file access')
    builtins.open=no_open
    try:
        state=p.parse_mixer_state(b'{"10":127,"22":16}')
        neutral=p.neutral_frame(idle,state,128)
        check(p.validate_frames(stamp(neutral),stamp(neutral),neutral)==128,'API runs with file opening forbidden')
    finally:builtins.open=original_open
    check(driver.read_bytes()==driver_raw and frames.read_bytes()==frames_raw,'reference sources unchanged')
    return dict(result='PASS',checks=len(checks),check_labels=checks,failures=0,
        strict_rejection_control_count=len(controls),strict_rejection_controls=controls,
        compared_byte_controls=149,extra_tail_byte_controls=24,
        source_sha256={name:hashlib.sha256((HERE/name).read_bytes()).hexdigest() for name in ('action-policy.py','test-policy.py')},
        driver_source_sha256=p.DRIVER_SHA256,kernel_frames_source_sha256=hashlib.sha256(frames_raw).hexdigest(),
        idle_sha256=p.IDLE_SHA256,protected_sha256=p.PROTECTED_SHA256,
        reviewed_mixer_offsets=sorted(p.MIXER_OFFSETS),raw_fixtures_included=False,
        runtime_access=False,freshness_proven=False,native_consumption_proven=False,
        scope='Pure settings/frame policy with source-extracted and synthetic in-memory cases only')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--driver',type=Path,required=True)
    parser.add_argument('--frames',type=Path,required=True)
    parser.add_argument('--result',type=Path)
    args=parser.parse_args();result=main(args.driver,args.frames)
    output=json.dumps(result,indent=2,sort_keys=True)+'\n'
    if args.result:
        with args.result.open('x') as f:f.write(output)
    else:print(output,end='')

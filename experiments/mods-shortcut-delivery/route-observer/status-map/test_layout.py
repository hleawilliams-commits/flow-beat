#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import copy
import json
import hashlib
from pathlib import Path
from layout import parse_maps, plan_reads

MODULE = '/fixture with spaces/observer.so'
MAPS = '''100000-102000 r-xp 00000000 08:01 7 /fixture with spaces/observer.so
111000-112000 r--p 00001000 08:01 7 /fixture with spaces/observer.so
112000-113000 rw-p 00002000 08:01 7 /fixture with spaces/observer.so
113000-115000 rw-p 00000000 00:00 0
'''
LAYOUT = dict(sha256='synthetic', loads=[
    dict(vaddr=0, offset=0, filesz=0x1cc0, memsz=0x1cc0, flags=5),
    dict(vaddr=0x11d50, offset=0x1d50, filesz=0x500, memsz=0x2800, flags=6)],
    symbols={'state':dict(name='state',value=0x12100,size=4),
             'sequence':dict(name='sequence',value=0x13108,size=8)})
checks=0
def check(value, label):
    global checks
    checks += 1
    if not value: raise AssertionError(label)
def reject(label, layout=None, text=MAPS, path=MODULE, page=4096):
    try: plan_reads(copy.deepcopy(LAYOUT) if layout is None else layout, parse_maps(text), path, page)
    except ValueError: check(True, label)
    else: check(False, label)

r=plan_reads(LAYOUT, parse_maps(MAPS), MODULE)
check(r['load_bias']==0x100000,'RX-derived bias')
check(r['reads']['state']['address']==0x112100,'RW vaddr, not offset')
check(r['reads']['state']['size']==4,'state width')
check(r['reads']['sequence']['address']==0x113108,'anonymous BSS')
check(r['reads']['sequence']['size']==8,'sequence width')
check(r['reads']['sequence']['mapping']=='anonymous-bss','BSS identity')
check(not r['coherent_snapshot'] and not r['native_objects'],'scope')
check(0x111000-0x1000 != r['load_bias'],'naive RW bias differs')
relro=MAPS.replace('112000-113000 rw-p','112000-113000 r--p')
check(plan_reads(LAYOUT,parse_maps(relro),MODULE)['reads']['state']['address']==0x112100,'readonly data split accepted')
reject('no RX anchor',text=MAPS.replace('r-xp','r--p'))
reject('wrong path',path='/wrong.so')
reject('deleted',text=MAPS.replace(MODULE,MODULE+' (deleted)'))
reject('mixed inode',text=MAPS.replace('00002000 08:01 7','00002000 08:01 8'))
reject('mixed device',text=MAPS.replace('00002000 08:01 7','00002000 08:02 7'))
reject('wrong RW offset',text=MAPS.replace('00002000 08:01','00003000 08:01'))
reject('wrong RW displacement',text=MAPS.replace('111000-112000','121000-122000'))
reject('data execute flag',text=MAPS.replace('rw-p 00002000','rwxp 00002000'))
reject('unreadable scalar',text=MAPS.replace('rw-p 00002000','-w-p 00002000'))
reject('BSS belongs to other file',text=MAPS.replace('00000000 00:00 0','00000000 08:01 8 /other.so'))
reject('BSS has named anonymous owner',text=MAPS.replace('00000000 00:00 0','00000000 00:00 0 [heap]'))
reject('BSS executable',text=MAPS.replace('113000-115000 rw-p','113000-115000 rwxp'))
reject('BSS missing',text='\n'.join(MAPS.splitlines()[:-1]))
reject('map overlap',text=MAPS+'112000-114000 rw-p 0 00:00 0\n')
reject('page size',page=3000)
for field,value in [('value',0x100),('value',0x20000),('size',16),('value',0x12101)]:
    x=copy.deepcopy(LAYOUT); x['symbols']['state'][field]=value
    reject('invalid scalar '+field,layout=x)
x=copy.deepcopy(LAYOUT); x['loads'][1]['offset']+=1
reject('incongruent segment',layout=x)
x=copy.deepcopy(LAYOUT); x['loads'][1]['memsz']=1
reject('file exceeds memory',layout=x)
x=copy.deepcopy(LAYOUT); x['loads'].append(copy.deepcopy(x['loads'][1]))
reject('ambiguous scalar load',layout=x)
# Same file mapped twice must not silently select one module instance.
reject('two module instances',text=MAPS+MAPS.replace('100000-102000','200000-202000').replace('111000-112000','211000-212000').replace('112000-113000','212000-213000').replace('113000-115000','213000-215000'))
# Reproduced original failures are now rejected; targeted mutants below must
# accept the same inputs, proving each new guard changes behavior.
file_into_bss = MAPS.replace('112000-113000 rw-p', '112000-115000 rw-p').replace('113000-115000 rw-p 00000000 00:00 0\n', '')
unaligned_anchor = MAPS.replace('100000-102000 r-xp 00000000', '100001-102000 r-xp 00000001')
bss_only = copy.deepcopy(LAYOUT)
del bss_only['symbols']['state']
no_data_prefix = MAPS.splitlines()[0] + '\n' + MAPS.splitlines()[-1] + '\n'
reject('file mapping cannot extend into whole BSS pages', text=file_into_bss)
reject('maps start and offset must be page aligned', text=unaligned_anchor)
reject('BSS-only request needs data file prefix', layout=bss_only, text=no_data_prefix)
reject('one-page gap in scalar file prefix', text=MAPS.replace(MAPS.splitlines()[1]+'\n',''))
reject('unaligned map end', text=MAPS.replace('113000-115000','113000-114fff'))
reject('negative file offset', text=MAPS.replace('00002000 08:01','-0001000 08:01'))
reject('negative inode', text=MAPS.replace('08:01 7','08:01 -7'))
reject('overflowing map end', text=MAPS.replace('113000-115000','113000-10000000000000001'))
x=copy.deepcopy(LAYOUT); x['symbols']={}
reject('no requested symbols', layout=x)
x=copy.deepcopy(LAYOUT); x['loads'][1]['vaddr']=1<<64
reject('overflowing segment bounds', layout=x)
# A file-mapped partial last page legitimately includes the initial zero-fill.
x=copy.deepcopy(LAYOUT); x['symbols']={'tail':dict(name='tail',value=0x12250,size=8)}
check(plan_reads(x,parse_maps(MAPS),MODULE)['reads']['tail']['mapping']=='file', 'partial last file page zero-fill allowed')
# The anonymous VMA may be merged with adjacent storage; requested bytes remain
# inside the exact PT_LOAD memory extent, not merely its rounded map boundary.
merged=MAPS.replace('113000-115000','113000-119000')
x=copy.deepcopy(LAYOUT); x['symbols']={'last':dict(name='last',value=0x14548,size=8)}
check(plan_reads(x,parse_maps(merged),MODULE)['reads']['last']['address']==0x114548, 'last exact memory bytes in merged anonymous VMA')
x['symbols']['last']['value']=0x14550
reject('read outside exact memory size in merged VMA',layout=x,text=merged)
x=copy.deepcopy(LAYOUT)
x['loads'].append(dict(vaddr=0x15000,offset=0x3000,filesz=0,memsz=0x1000,flags=6))
x['symbols']={'zero':dict(name='zero',value=0x15000,size=4)}
reject('pure BSS load without file prefix', layout=x, text=merged)
# Genuine ASLR displacement changes all rows together and preserves file offsets.
rows=parse_maps(MAPS)
for row in rows:
    row['start']+=0x500000
    row['end']+=0x500000
check(plan_reads(LAYOUT,rows,MODULE)['load_bias']==0x600000, 'second ASLR bias')
# Page size belongs to the target. The same algorithm supports 64 KiB pages.
large=dict(sha256='synthetic-64k',loads=[
    dict(vaddr=0,offset=0,filesz=0x3000,memsz=0x3000,flags=5),
    dict(vaddr=0x230000,offset=0x30000,filesz=0x1000,memsz=0x20000,flags=6)],
    symbols={'sequence':dict(name='sequence',value=0x240008,size=8)})
large_maps='70000000-70010000 r-xp 00000000 08:01 7 /fixture with spaces/observer.so\n70230000-70240000 r--p 00030000 08:01 7 /fixture with spaces/observer.so\n70240000-70250000 rw-p 00000000 00:00 0\n'
check(plan_reads(large,parse_maps(large_maps),MODULE,65536)['reads']['sequence']['address']==0x70240008, '64 KiB target page size')

# A loader reservation is not a readable segment. Its file offset follows
# the initial mapping, while the actual RW segment uses its own displacement.
guard='102000-111000 ---p 00002000 08:01 7 '+MODULE+'\n'
check(plan_reads(LAYOUT,parse_maps(MAPS+guard),MODULE)==r, 'inaccessible intersegment reservation allowed')
split_guard='102000-108000 ---p 00002000 08:01 7 '+MODULE+'\n108000-111000 ---p 00008000 08:01 7 '+MODULE+'\n'
check(plan_reads(LAYOUT,parse_maps(MAPS+split_guard),MODULE)==r, 'split inaccessible reservation allowed')
reject('readable intersegment mapping', text=MAPS+guard.replace('---p','r--p'))
reject('shared reservation mapping', text=MAPS+guard.replace('---p','---s'))
reject('reservation wrong file offset', text=MAPS+guard.replace('00002000','00003000'))
reject('reservation extends outside gap', text=MAPS+guard.replace('102000-111000','102000-112000'))
reject('reservation after final load', text=MAPS+'115000-116000 ---p 00015000 08:01 7 '+MODULE+'\n')
# A reservation cannot replace the actual data prefix even if only BSS is read.
reject('reservation cannot supply scalar prefix', layout=bss_only, text=no_data_prefix+guard)

source=Path(__file__).with_name('layout.py').read_text()
controls=[]
for label, old, new, model, maps in [
    ('remove-file-extent-bound',
     "and r['end'] <= bias + up(s['vaddr'] + s['filesz'])",
     "and r['end'] <= bias + up(s['vaddr'] + s['memsz'])", LAYOUT, file_into_bss),
    ('remove-page-alignment-guard',
     "    for r in rows:\n        require(r['start'] % page_size == 0 and r['end'] % page_size == 0\n                and r['offset'] % page_size == 0, 'unaligned maps row')\n", '', LAYOUT, unaligned_anchor),
    ('remove-prefix-coverage-guard',
     "        require(prefix_complete, 'scalar PT_LOAD file prefix is incomplete')\n", '', bss_only, no_data_prefix)]:
    check(source.count(old)==1, label+' exact mutation anchor')
    scope={}
    exec(compile(source.replace(old,new), '<guard-removal-control>', 'exec'),scope)
    mutated=scope['plan_reads'](copy.deepcopy(model),scope['parse_maps'](maps),MODULE)
    check(bool(mutated['reads']), label+' exposes expected false acceptance')
    controls.append(dict(name=label, expected='accept invalid layout', observed='accepted', discriminating=True))
print(json.dumps(dict(checks=checks, failures=0, scope='synthetic offline layout only',
                     controls=controls,
                     source_sha256=hashlib.sha256(Path(__file__).with_name('layout.py').read_bytes()).hexdigest()),indent=2))

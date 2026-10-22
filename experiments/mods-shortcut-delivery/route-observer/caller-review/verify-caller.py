#!/usr/bin/env python3
"""Static review of the observed return site; no runtime/native-object reads."""
import argparse,hashlib,io,json,struct,subprocess,tempfile
from pathlib import Path
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
from elftools.dwarf.callframe import FDE
from elftools.elf.elffile import ELFFile

FIRMWARE='6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925'
PIN='3e0c5cb010454228f4ee63226b60d060494eeaae'
FUNCTIONS={0x22c6c48:0x260,0x2360f80:0x134,0x1b01cf8:0x2e8,
           0x22c18b8:0x1cc,0x22c1a88:0x144}
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--elf',type=Path,required=True)
ap.add_argument('--upstream',type=Path,required=True)
ap.add_argument('--cc',default='cc')
args=ap.parse_args()
digest=lambda data:hashlib.sha256(data).hexdigest()
raw=args.elf.read_bytes();assert digest(raw)==FIRMWARE
elf=ELFFile(io.BytesIO(raw))
assert elf.elfclass==64 and elf.little_endian
assert elf['e_machine']=='EM_AARCH64' and elf['e_type']=='ET_EXEC'
segments=list(elf.iter_segments())
def read(address,n):
    for s in segments:
        if s['p_type']=='PT_LOAD' and s['p_vaddr']<=address and address+n<=s['p_vaddr']+s['p_filesz']:
            off=s['p_offset']+address-s['p_vaddr'];return raw[off:off+n]
    raise AssertionError('address not file-backed')
def word(address):return struct.unpack('<Q',read(address,8))[0]
fdes={x['initial_location']:x['address_range'] for x in elf.get_dwarf_info().EH_CFI_entries() if isinstance(x,FDE)}
md=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN);ins={}
for start,size in FUNCTIONS.items():
    assert fdes[start]==size
    ins.update({x.address:(x.mnemonic,x.op_str) for x in md.disasm(read(start,size),start)})
anchors={
  0x22c6c48:('stp','x29, x30, [sp, #-0x80]!'),
  0x22c6c4c:('mov','x29, sp'),
  0x22c6c50:('ldr','x3, [x0, #0xb8]'),
  0x22c6c54:('cbz','x3, #0x22c6e40'),
  0x22c6c5c:('mov','x2, x1'),
  0x22c6c60:('mov','x1, x3'),
  0x22c6c64:('ldr','x4, [x0]'),
  0x22c6c68:('mov','x19, x0'),
  0x22c6c6c:('ldr','x3, [x4, #0x20]'),
  0x22c6c70:('blr','x3'),
  0x22c6c74:('ldr','w0, [x19, #0xb0]'),
  0x22c6d50:('mov','w1, #0xc'),
  0x22c6d54:('ldr','x21, [x0, #0xa0]!'),
  0x22c6d58:('ldr','w25, [x0, #0x10]'),
  0x22c6d5c:('smaddl','x25, w25, w1, x21'),
  0x22c6d94:('ldr','x0, [x21]'),
  0x22c6d98:('str','x0, [sp, #0x70]'),
  0x22c6d9c:('ldr','x0, [x19, #0x88]'),
  0x22c6da4:('ldr','w1, [x21, #8]'),
  0x22c6dac:('str','w1, [sp, #0x78]'),
  0x22c6dc0:('ldr','x27, [x20]'),
  0x22c6df0:('ldr','x3, [x19]'),
  0x22c6df4:('mov','x1, x27'),
  0x22c6df8:('add','x2, sp, #0x70'),
  0x22c6dfc:('mov','x0, x19'),
  0x22c6e00:('ldr','x3, [x3, #0x10]'),
  0x22c6e04:('blr','x3'),
  0x22c6e08:('add','x20, x20, #8'),
  0x22c6e18:('add','x21, x21, #0xc'),
  0x2360f80:('adrp','x3, #0x28fa000'),
  0x2360fa8:('mov','x21, x2'),
  0x2360fb4:('mov','x20, x0'),
  0x2360fb8:('mov','x19, x1'),
  0x2361018:('ldr','w3, [x21]'),
  0x236101c:('cbz','w3, #0x2360fc8'),
  0x236102c:('ldr','x2, [x19]'),
  0x2361034:('ldr','x22, [x2, #0x18]'),
  0x2361040:('ldr','x0, [x20, #0xd0]'),
  0x2361068:('ldr','w4, [x21, #4]'),
  0x236106c:('mov','x2, x0'),
  0x2361070:('add','x1, x20, #0xc0'),
  0x2361074:('mov','x0, x19'),
  0x2361078:('mov','x5, x22'),
  0x236107c:('ldp','x19, x20, [sp, #0x10]'),
  0x2361080:('ldp','x21, x22, [sp, #0x20]'),
  0x2361084:('ldp','x29, x30, [sp], #0x40'),
  0x2361088:('br','x5'),
  0x236108c:('mov','x0, x20'),
  0x2361090:('blr','x1'),
  0x2361094:('ldr','w3, [x21]'),
  0x2361098:('b','#0x2361044'),
  0x1b01cf8:('stp','x29, x30, [sp, #-0xc0]!'),
  0x1b01cfc:('mov','x29, sp'),
  0x1b01d00:('stp','x19, x20, [sp, #0x10]'),
  0x1b01d04:('stp','x21, x22, [sp, #0x20]'),
  0x1b01d08:('mov','w21, w4'),
  0x1b01d10:('mov','w24, w3'),
  0x1b01d18:('mov','x26, x2'),
  0x1b01d1c:('sub','x25, x0, #0x118'),
  0x22c1a44:('add','x1, sp, #0x28'),
  0x22c1a48:('ldursh','w3, [x20, #3]'),
  0x22c1a50:('ldr','x0, [x19, #0xd0]'),
  0x22c1a54:('sub','w3, w3, w4'),
  0x22c1a5c:('str','w3, [sp, #0x28]'),
  0x22c1a64:('bl','#0x22c6c48'),
  0x22c1aac:('ldr','x0, [x0, #0x130]'),
  0x22c1ac4:('ldr','w1, [x0, #0xd8]'),
  0x22c1ac8:('sub','w20, w20, w1, sxth'),
  0x22c1acc:('add','x1, sp, #0x38'),
  0x22c1ad0:('str','w20, [sp, #0x38]'),
  0x22c1ad8:('bl','#0x22c6c48'),
}
def verify(actual,expected):
    assert all(actual.get(a)==v for a,v in expected.items()),'instruction anchor mismatch'
verify(ins,anchors)
words={0x3216930:0x3214f50,0x3216948:0x2360f80,0x3216958:0x235c270,
       0x2d453e8:0x1b01cf8,0x2d45758:0x1b01cf8}
for a,v in words.items():assert word(a)==v
name=read(word(0x3214f50+8),64).split(b'\0')[0].decode()
assert name=='N4meow10HuiEncoderE'
# Direct call references supplement, but do not choose a live input source.
xrefs=[]
for s in segments:
    if s['p_type']!='PT_LOAD' or not s['p_flags']&1:continue
    data=raw[s['p_offset']:s['p_offset']+(s['p_filesz']//4)*4]
    for i,(w,) in enumerate(struct.iter_unpack('<I',data)):
        if w&0x7c000000!=0x14000000:continue
        disp=w&0x3ffffff
        if disp&(1<<25):disp-=1<<26
        a=s['p_vaddr']+4*i
        if a+4*disp==0x22c6c48:xrefs.append(a)
assert xrefs==[0x22c1a64,0x22c1ad8]
assert 0x22c6e04+4==0x22c6e08
# Wrong semantics must cause this same verifier to reject the altered expectation.
controls=[
 (0x22c6e04,('bl','#0x1b01cf8')),
 (0x22c6e00,('ldr','x3, [x3, #0x18]')),
 (0x22c6df8,('mov','x2, x21')),
 (0x2361068,('ldr','w4, [x21]')),
 (0x2361084,('ldp','x29, x29, [sp], #0x40')),
 (0x2361088,('blr','x5')),
 (0x22c6e18,('add','x21, x21, #8')),
]
for a,wrong in controls:
    altered=dict(anchors);altered[a]=wrong
    try:verify(ins,altered)
    except AssertionError:pass
    else:raise AssertionError('negative control was accepted')
try:
    assert 0x22c6e04+4==0x236108c
except AssertionError:pass
else:raise AssertionError('wrong immediate LR accepted')
# Run actual pinned host predicates, never firmware; do not relax installer policy.
h=subprocess.check_output(['git','show',PIN+':src/azmod/inline_hooks.c'],cwd=args.upstream).decode()
def extract(name):
    start=h.index('static int '+name+'(');brace=h.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(h[end]=='{')-(h[end]=='}');end+=1
    return h[start:end]
policy='\n\n'.join(extract(n) for n in ('azm_pcrel_kind','azm_ends_fn','azm_patch_ok'))
install=extract('azm_install_hook_at')
assert install.index('if (!azm_patch_ok(func, 1))')<install.index('azm_island_slot(FUNC)')
rows={a:struct.unpack('<4I',read(a,16)) for a in (0x22c6c48,0x2360f80,0x1b01cf8)}
arrays='\n'.join('uint32_t '+n+'[4]={'+','.join(hex(w)+'u' for w in rows[a])+'};'
    for n,a in [('caller',0x22c6c48),('notify',0x2360f80),('accepted',0x1b01cf8)])
fixture='#include <stdint.h>\n#include <stdio.h>\n'+policy+'''
static int n;
#define CHECK(v) do { ++n; if (!(v)) return 1; } while(0)
int main(void) {
'''+arrays+'''
    CHECK(azm_pcrel_kind(caller[3]) == 1);
    CHECK(azm_pcrel_kind(notify[0]) == 2);
    CHECK(!azm_patch_ok(caller,1)); CHECK(!azm_patch_ok(caller,4));
    CHECK(azm_patch_ok(notify,1)); CHECK(azm_patch_ok(notify,4));
    CHECK(azm_patch_ok(accepted,1)); CHECK(azm_patch_ok(accepted,4));
    notify[0]=0x10000003u;
    CHECK(!azm_patch_ok(notify,1)); CHECK(!azm_patch_ok(notify,4));
    caller[3]=0xd503201fu; notify[0]=0xd503201fu;
    CHECK(azm_patch_ok(caller,1)); CHECK(azm_patch_ok(caller,4));
    CHECK(azm_patch_ok(notify,1)); CHECK(azm_patch_ok(notify,4));
    printf("%d\\n",n); return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='az-caller-review-') as temp:
    c=Path(temp)/'policy.c';exe=Path(temp)/'policy';c.write_text(fixture)
    subprocess.run([args.cc,'-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    policy_checks=int(subprocess.check_output([str(exe)],text=True))
assert policy_checks==14
result={
 'firmware_sha256':FIRMWARE,'upstream_commit':PIN,
 'instruction_anchors':len(anchors),'data_word_anchors':len(words),'rtti_anchors':1,
 'semantic_rejection_controls':len(controls)+1,'actual_policy_assertions':policy_checks,
 'policy_source_sha256':digest(h.encode()),'extracted_policy_sha256':digest(policy.encode()),
 'functions':{hex(a):{'fde_extent':hex(n),'body_sha256':digest(read(a,n))} for a,n in FUNCTIONS.items()},
 'observed_lr_static_meaning':{'lr':'0x22c6e08','call':'0x22c6e04',
   'callee_selection':'source vtable +0x10','x0':'source object opaque',
   'x1':'observer opaque','x2':'address of current stack copy of twelve-byte event',
   'immediate_scalar_dispatch_proven':False,'known_tail_route_preserves_lr':True},
 'static_encoder_route':{'rtti':name,'vptr':'0x3216938','notify':'0x2360f80',
   'observer_slot':'0x18','scalar_tail_branch':'0x2361088',
   'captures':['x0 observer opaque','x1 encoder+0xc0 opaque','x2 full-width ID opaque',
               'w3 event+0 bits','w4 event+4 bits'],
   'live_vtable_or_object_class_read':False},
 'direct_call_sites':[hex(a) for a in xrefs],
 'ineligible_entries':{'0x22c6c48':'CBZ at +0xc in four-instruction scan'},
 'notification_policy_only':{'entry':'0x2360f80','first_instruction_kind':2,
   'adrp_relocatable':True,'one_word_allowed':True,'four_word_allowed':True,
   'full_static_or_live_claim_eligibility_audited':False},
 'next_step':'Separately tested bounded off-thread scalar lane at existing accepted 0x1b01cf8; no new hook needed',
 'limits':['Static tail chain is consistent with observed LR but does not identify a live source vtable',
           'Two direct source call sites do not identify which input source occurred at runtime',
           'Do not dereference event/source/observer pointers or retain native objects',
           'No return type, acceptance, completion, enqueue, queue ownership, cancellation or executor thread proof',
           'Current static review performs no runtime hook, emulator/network contact or firmware execution']
}
print(json.dumps(result,indent=2,sort_keys=True))

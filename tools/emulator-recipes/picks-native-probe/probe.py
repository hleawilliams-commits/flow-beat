from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
import struct,sys
f=open('work/picks-native-probe/EP147','rb'); e=ELFFile(f); b=f.read() if False else open(f.name,'rb').read(); md=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
loads=[p for p in e.iter_segments() if p['p_type']=='PT_LOAD']
def addr(off):
 for p in loads:
  if p['p_offset']<=off<p['p_offset']+p['p_filesz']: return off-p['p_offset']+p['p_vaddr']
def off(a):
 for p in loads:
  if p['p_vaddr']<=a<p['p_vaddr']+p['p_filesz']: return a-p['p_vaddr']+p['p_offset']
def refs(a):
 n=struct.pack('<Q',a); start=0; out=[]
 while (start:=b.find(n,start))>=0:
  if start%8==0: out.append(addr(start))
  start+=1
 return out
def dis(a,n=120):
 o=off(a)
 for i in md.disasm(b[o:o+n],a): print(hex(i.address),i.mnemonic,i.op_str)
if __name__=='__main__':
 for name in ['N3gui13browser_logic22TrackFilterEventFacadeE','N3gui26TrackFilterFollowingMasterE','N3gui19TrackFilterBpmStateE','N3gui19TrackFilterKeyStateE']:
  a=addr(b.index(name.encode()+b'\0')); print(name,hex(a))
  for r in refs(a):
   ti=r-8; print(' typeinfo',hex(ti))
   for v in refs(ti):
    print(' vtable',hex(v+8))
    o=off(v+8)
    for j in range(14):
     fn=struct.unpack_from('<Q',b,o+j*8)[0]
     if off(fn) is None: break
     print(' slot',j,hex(fn));dis(fn,32)

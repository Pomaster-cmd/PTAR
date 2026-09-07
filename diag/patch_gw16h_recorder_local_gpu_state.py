#!/usr/bin/env python3
"""GW16H UNIFIEDREC3 SAFEPOINT3 / LOCALGPU1 surgical binary patch.

Base: exact SAFEPOINT2/NATIVEUSR1 runtime.

Field evidence from SAFEPOINT2 proved that CTRL+F9 and QSV process startup now work
on the native USR FG-OFF route, but every recorder GPU conversion submit fails before
query/ring admission. B18K18 conversion still borrowed two device-owned D3D11 state
objects from the isolated FG presenter: the fullscreen vertex shader and rasterizer
state. Those objects are absent in native-USR mode and cannot safely be shared across
D3D11 devices.

This patch makes B18K18 own a private fullscreen VS + rasterizer state, creates them
on the exact active recorder device after the original recorder GPU initialization,
tracks the owning device, and re-creates them on device changes. The conversion path
uses only these recorder-local objects. Existing recorder resources, QSV pipeline,
SAFEPOINT2 routing, FG algorithm, pacing and hotkeys remain unchanged.
"""
from pathlib import Path
import hashlib, struct, sys
BASE_SHA='7c56148f4d00f7623c4bf44e5f460d81145b660ff5743a5ea5b57610c1d0c630'
FINAL_SHA='961b7c0211239364920516e990f9f5a6a3284709bcd4a2a17e6a21b1a76556f2'
SIZE=320000
INIT_CALL_SITE=0x27AD5
ORIG_INIT=0x2A400
COM_RELEASE_SLOT=0x9FB0
MEMZERO=0x5B80
COMPILE_SHADER=0x21AB0
VS_SOURCE=0x320E0
VS_TARGET=0x42C3A
SHARED_VS=0x2C93A38
SHARED_RS=0x2C93A80
PRIVATE_OWNER=0x34FF4C0
PRIVATE_VS=0x34FF4C8
PRIVATE_RS=0x34FF4D0
INIT_WRAPPER=0x34FF4E0
CAVE_START=0x34FF4BF
CAVE_END=0x34FF700
VS_PRECOND_SITE=0x2B747
VS_BIND_SITE=0x2BA2D
RS_PRECOND_SITE=0x2B7B1
RS_BIND_SITE=0x2BA7C

def sha(b): return hashlib.sha256(bytes(b)).hexdigest()
def parse(b):
 e=struct.unpack_from('<I',b,0x3c)[0];coff=e+4;n=struct.unpack_from('<H',b,coff+2)[0];optsz=struct.unpack_from('<H',b,coff+16)[0];opt=coff+20;sh=opt+optsz;secs=[]
 for i in range(n):
  o=sh+i*40;name=b[o:o+8].rstrip(b'\0').decode('ascii');vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);ch=struct.unpack_from('<I',b,o+36)[0];secs.append((name,vs,va,rs,rp,ch))
 return opt,secs
def roff(b,rva):
 for _n,vs,va,rs,rp,_ch in parse(b)[1]:
  if va<=rva<va+max(vs,rs): return rp+(rva-va)
 raise RuntimeError('RVA not mapped %#x'%rva)
def rel32(src,ilen,dst): return struct.pack('<i',dst-(src+ilen))
def checksum(blob,off):
 x=bytearray(blob);struct.pack_into('<I',x,off,0);s=0
 for i in range(0,len(x)-1,2): s+=x[i]|(x[i+1]<<8);s=(s&0xffff)+(s>>16)
 if len(x)&1:s+=x[-1]
 s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16);return (s+len(x))&0xffffffff
def call_target(b,site):
 o=roff(b,site);x=b[o:o+5]
 return site+5+struct.unpack_from('<i',x,1)[0] if len(x)==5 and x[0]==0xe8 else None
def rip_target(b,site,ilen,disp_off):
 o=roff(b,site);return site+ilen+struct.unpack_from('<i',b,o+disp_off)[0]
class E:
 def __init__(self,base): self.base=base;self.b=bytearray();self.labels={};self.fix=[]
 @property
 def rva(self): return self.base+len(self.b)
 def emit(self,x): self.b+=bytes(x)
 def label(self,n): self.labels[n]=self.rva
 def call(self,dst): s=self.rva;self.emit(b'\xe8'+rel32(s,5,dst))
 def jcc(self,cc,label):
  op={'e':b'\x0f\x84','ne':b'\x0f\x85','s':b'\x0f\x88'}[cc];self.emit(op);p=len(self.b);self.emit(b'\0'*4);self.fix.append((p,label))
 def jmp(self,label): self.emit(b'\xe9');p=len(self.b);self.emit(b'\0'*4);self.fix.append((p,label))
 def rip(self,prefix,target,ilen): s=self.rva;self.emit(prefix+struct.pack('<i',target-(s+ilen)))
 def finish(self):
  for p,l in self.fix: struct.pack_into('<i',self.b,p,self.labels[l]-(self.base+p+4))
  return bytes(self.b)

def build_wrapper():
 e=E(INIT_WRAPPER)
 e.emit(b'\x53\x56\x57')                 # push rbx,rsi,rdi => caller alignment restored
 e.emit(b'\x48\x81\xec\x80\x00\x00\x00')
 e.emit(b'\x48\x89\xcb')                 # mov rbx,rcx device
 e.call(ORIG_INIT)
 e.emit(b'\x85\xc0'); e.jcc('e','orig_fail')
 # Reuse recorder-local state only if it belongs to this exact device and both objects exist.
 e.rip(b'\x48\x8b\x05',PRIVATE_OWNER,7); e.emit(b'\x48\x39\xd8'); e.jcc('ne','recreate')
 e.rip(b'\x48\x83\x3d',PRIVATE_VS,8); e.emit(b'\x00'); e.jcc('e','recreate')
 e.rip(b'\x48\x83\x3d',PRIVATE_RS,8); e.emit(b'\x00'); e.jcc('e','recreate')
 e.emit(b'\xb8\x01\x00\x00\x00'); e.jmp('done')
 e.label('recreate')
 e.rip(b'\x48\x8d\x0d',PRIVATE_VS,7); e.call(COM_RELEASE_SLOT)
 e.rip(b'\x48\x8d\x0d',PRIVATE_RS,7); e.call(COM_RELEASE_SLOT)
 e.rip(b'\x48\xc7\x05',PRIVATE_OWNER,11); e.emit(b'\x00\x00\x00\x00')
 # ID3DBlob* local = NULL
 e.emit(b'\x48\xc7\x44\x24\x28\x00\x00\x00\x00')
 e.rip(b'\x48\x8d\x0d',VS_SOURCE,7)
 e.rip(b'\x48\x8d\x15',VS_TARGET,7)
 e.emit(b'\x4c\x8d\x44\x24\x28')
 e.call(COMPILE_SHADER)
 e.emit(b'\x85\xc0'); e.jcc('s','fail_release')
 e.emit(b'\x48\x8b\x4c\x24\x28\x48\x85\xc9'); e.jcc('e','fail_release')
 e.emit(b'\x48\x8b\x31')                 # rsi = blob vtbl
 e.emit(b'\xff\x56\x18')                 # GetBufferPointer
 e.emit(b'\x48\x89\xc7')                 # rdi = bytecode ptr
 e.emit(b'\x48\x8b\x4c\x24\x28\xff\x56\x20') # GetBufferSize
 e.emit(b'\x48\x89\xc6')                 # rsi = size
 # device->CreateVertexShader(bytecode,size,NULL,&PRIVATE_VS)
 e.emit(b'\x48\x89\xd9\x48\x8b\x03')
 e.rip(b'\x4c\x8d\x15',PRIVATE_VS,7)
 e.emit(b'\x4c\x89\x54\x24\x20\x48\x89\xfa\x49\x89\xf0\x45\x31\xc9')
 e.emit(b'\xff\x50\x60')
 e.emit(b'\x89\xc6')                      # save HRESULT in esi
 e.emit(b'\x48\x8d\x4c\x24\x28'); e.call(COM_RELEASE_SLOT)
 e.emit(b'\x85\xf6'); e.jcc('s','fail_release')
 e.rip(b'\x48\x83\x3d',PRIVATE_VS,8); e.emit(b'\x00'); e.jcc('e','fail_release')
 # D3D11_RASTERIZER_DESC { SOLID, NONE, FrontCCW=0, DepthBias=0, ..., DepthClipEnable=1 }
 e.emit(b'\x48\x8d\x4c\x24\x30\xba\x28\x00\x00\x00'); e.call(MEMZERO)
 e.emit(b'\x48\xb8\x03\x00\x00\x00\x01\x00\x00\x00\x48\x89\x44\x24\x30')
 e.emit(b'\xc7\x44\x24\x48\x01\x00\x00\x00')
 e.emit(b'\x48\x89\xd9\x48\x8b\x03\x48\x8d\x54\x24\x30')
 e.rip(b'\x4c\x8d\x05',PRIVATE_RS,7)
 e.emit(b'\xff\x90\xb0\x00\x00\x00')
 e.emit(b'\x85\xc0'); e.jcc('s','fail_release')
 e.rip(b'\x48\x83\x3d',PRIVATE_RS,8); e.emit(b'\x00'); e.jcc('e','fail_release')
 e.rip(b'\x48\x89\x1d',PRIVATE_OWNER,7)
 e.emit(b'\xb8\x01\x00\x00\x00'); e.jmp('done')
 e.label('fail_release')
 e.emit(b'\x48\x8d\x4c\x24\x28'); e.call(COM_RELEASE_SLOT)
 e.rip(b'\x48\x8d\x0d',PRIVATE_VS,7); e.call(COM_RELEASE_SLOT)
 e.rip(b'\x48\x8d\x0d',PRIVATE_RS,7); e.call(COM_RELEASE_SLOT)
 e.rip(b'\x48\xc7\x05',PRIVATE_OWNER,11); e.emit(b'\x00\x00\x00\x00')
 e.label('orig_fail'); e.emit(b'\x31\xc0')
 e.label('done')
 e.emit(b'\x48\x81\xc4\x80\x00\x00\x00\x5f\x5e\x5b\xc3')
 return e.finish()

def patch_rip_disp(out,base,site,ilen,disp_off,target):
 o=roff(base,site); struct.pack_into('<i',out,o+disp_off,target-(site+ilen))

def main():
 if len(sys.argv)!=3: raise SystemExit('usage: patch_sp3.py SAFEPOINT2_DLL OUT_DLL')
 src,dst=map(Path,sys.argv[1:]); base=src.read_bytes()
 if len(base)!=SIZE or sha(base)!=BASE_SHA: raise RuntimeError('exact SAFEPOINT2 required: '+sha(base))
 if call_target(base,INIT_CALL_SITE)!=ORIG_INIT: raise RuntimeError('init call lock')
 locks=[(VS_PRECOND_SITE,8,4,SHARED_VS),(VS_BIND_SITE,7,3,SHARED_VS),(RS_PRECOND_SITE,8,3,SHARED_RS),(RS_BIND_SITE,7,3,SHARED_RS)]
 for s,l,d,t in locks:
  if rip_target(base,s,l,d)!=t: raise RuntimeError('shared state lock %#x got %#x'%(s,rip_target(base,s,l,d)))
 cave=base[roff(base,CAVE_START):roff(base,CAVE_END)]
 if cave!=b'\xcc'*len(cave):
  i=next(i for i,x in enumerate(cave) if x!=0xcc);raise RuntimeError('cave not free at %#x byte=%02x'%(CAVE_START+i,cave[i]))
 w=build_wrapper(); print('WRAPPER_SIZE',len(w))
 if INIT_WRAPPER+len(w)>CAVE_END: raise RuntimeError('wrapper too large')
 out=bytearray(base)
 # private globals start clear
 out[roff(base,PRIVATE_OWNER):roff(base,PRIVATE_RS)+8]=b'\x00'*24
 out[roff(base,INIT_WRAPPER):roff(base,INIT_WRAPPER)+len(w)]=w
 # recorder init now guarantees recorder-local VS/RS on the exact active submit device
 o=roff(base,INIT_CALL_SITE); out[o:o+5]=b'\xe8'+rel32(INIT_CALL_SITE,5,INIT_WRAPPER)
 patch_rip_disp(out,base,VS_PRECOND_SITE,8,4,PRIVATE_VS)
 patch_rip_disp(out,base,VS_BIND_SITE,7,3,PRIVATE_VS)
 patch_rip_disp(out,base,RS_PRECOND_SITE,8,3,PRIVATE_RS)
 patch_rip_disp(out,base,RS_BIND_SITE,7,3,PRIVATE_RS)
 opt,_=parse(out);csoff=opt+64;struct.pack_into('<I',out,csoff,0);struct.pack_into('<I',out,csoff,checksum(out,csoff))
 h=sha(out)
 if h!=FINAL_SHA: raise RuntimeError('unexpected SAFEPOINT3 output SHA: '+h)
 dst.write_bytes(out)
 print('BASE_SHA256='+BASE_SHA);print('OUT_SHA256='+h);print('INIT_WRAPPER_RVA=0x%X'%INIT_WRAPPER);print('PRIVATE_OWNER_RVA=0x%X'%PRIVATE_OWNER);print('PRIVATE_VS_RVA=0x%X'%PRIVATE_VS);print('PRIVATE_RS_RVA=0x%X'%PRIVATE_RS)
 print('DELTA=B18K18_RECORDER_LOCAL_VS_RS_BOUND_TO_ACTIVE_DEVICE')
if __name__=='__main__': main()

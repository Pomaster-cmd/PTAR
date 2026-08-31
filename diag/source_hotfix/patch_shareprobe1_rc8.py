#!/usr/bin/env python3
import hashlib, struct, sys
BASE_SHA='f26bbf5919ddeeb39937e5258d6c56f3196936e7d9a755099d55404693326d98'
IMAGE_BASE=0x180000000
DIAG_RVA=0x34FD000
DIAG_NAME='.fgdia'
SUCCESS_RVA=0x221EE
FAILURE_RVA=0x222B3
LAST_STATUS_RVA=0x2CCE64C
GAME_DEVICE_RVA=0x4B118
TARGETS={'SUCCESS_TARGET':SUCCESS_RVA,'FAILURE_TARGET':FAILURE_RVA,'LAST_STATUS':LAST_STATUS_RVA,'GAME_DEVICE':GAME_DEVICE_RVA}

def sha(b): return hashlib.sha256(b).hexdigest()
def checksum(blob,off):
 b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
 for i in range(0,len(b)-1,2):
  s += b[i] | (b[i+1]<<8); s=(s&0xffff)+(s>>16)
 if len(b)&1: s+=b[-1]
 s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
 return (s+len(b))&0xffffffff

def parse_pe(b):
 e=struct.unpack_from('<I',b,0x3c)[0]; assert b[e:e+4]==b'PE\0\0'; coff=e+4
 machine,nsec,_,_,_,optsz,_=struct.unpack_from('<HHIIIHH',b,coff); assert machine==0x8664
 opt=coff+20; assert struct.unpack_from('<H',b,opt)[0]==0x20b
 sh=opt+optsz; secs=[]
 for i in range(nsec):
  o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
  vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8)
  secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
 return dict(opt=opt,secs=secs,ib=struct.unpack_from('<Q',b,opt+24)[0],cks_off=opt+64)

def coff_text(obj):
 b=open(obj,'rb').read(); machine,nsec,_,symptr,nsym,optsz,_=struct.unpack_from('<HHIIIHH',b,0)
 assert machine==0x8664 and optsz==0
 sec=None
 for i in range(nsec):
  o=20+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
  _,_,rawsz,rawptr,relptr,_,nrel,_,_=struct.unpack_from('<IIIIIIHHI',b,o+8)
  if name=='.text': sec=(rawsz,rawptr,relptr,nrel)
 assert sec
 rawsz,rawptr,relptr,nrel=sec; code=bytearray(b[rawptr:rawptr+rawsz])
 strtab_off=symptr+nsym*18; slen=struct.unpack_from('<I',b,strtab_off)[0]; stab=b[strtab_off:strtab_off+slen]
 def sname(idx):
  o=symptr+idx*18; rn=b[o:o+8]; z,so=struct.unpack('<II',rn)
  if z==0:
   end=stab.find(b'\0',so); end=len(stab) if end<0 else end
   return stab[so:end].decode('ascii')
  return rn.rstrip(b'\0').decode('ascii')
 rel=[]
 for i in range(nrel):
  o=relptr+i*10; roff,sidx,typ=struct.unpack_from('<IIH',b,o)
  if typ!=4: raise RuntimeError(f'unexpected relocation type {typ:#x}')
  rel.append((roff,sname(sidx)))
 return code,rel

def patch(base,obj,outp):
 src=bytearray(open(base,'rb').read())
 if sha(src)!=BASE_SHA: raise RuntimeError('wrong RC8 base hash '+sha(src))
 pe=parse_pe(src)
 if pe['ib']!=IMAGE_BASE: raise RuntimeError('unexpected image base')
 d=next((s for s in pe['secs'] if s['name']==DIAG_NAME),None)
 if not d or d['va']!=DIAG_RVA or d['rs']<0x200: raise RuntimeError('unexpected .fgdia')
 code,rel=coff_text(obj); code_va=IMAGE_BASE+DIAG_RVA
 counts={}
 for roff,name in rel:
  if name not in TARGETS: raise RuntimeError('unknown symbol '+name)
  target=IMAGE_BASE+TARGETS[name]
  addend=struct.unpack_from('<i',code,roff)[0]
  disp=target+addend-(code_va+roff+4)
  if not -(1<<31)<=disp<(1<<31): raise RuntimeError('rel32 overflow '+name)
  struct.pack_into('<i',code,roff,disp)
  counts[name]=counts.get(name,0)+1
 if counts.get('SUCCESS_TARGET')!=1 or counts.get('GAME_DEVICE')!=1 or counts.get('LAST_STATUS')!=2 or counts.get('FAILURE_TARGET')!=2:
  raise RuntimeError('unexpected relocation counts '+repr(counts))
 if len(code)>d['rs']: raise RuntimeError('new code exceeds .fgdia raw capacity')
 out=bytearray(src)
 out[d['rp']:d['rp']+d['rs']]=b'\xCC'*d['rs']
 out[d['rp']:d['rp']+len(code)]=code
 # Expand only VirtualSize of existing last section. SizeOfRawData/RVA/file size stay fixed.
 struct.pack_into('<I',out,d['off']+8,len(code))
 struct.pack_into('<I',out,pe['cks_off'],0); c=checksum(out,pe['cks_off']); struct.pack_into('<I',out,pe['cks_off'],c)
 open(outp,'wb').write(out)
 print('BASE_SHA256='+BASE_SHA)
 print('OUTPUT_SHA256='+sha(out))
 print('SHAREPROBE1_CODE_SIZE=0x%X'%len(code))
 print('PE_CHECKSUM=0x%X'%c)

if __name__=='__main__':
 if len(sys.argv)!=4: raise SystemExit('usage: patch_shareprobe1_rc8.py RC8_DLL OBJ OUT_DLL')
 patch(*sys.argv[1:])

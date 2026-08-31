#!/usr/bin/env python3
import hashlib, struct, sys

EXPECTED_BASE='309f82ea51cb549a594dfe584b3d9d9039b3b25265af604e6d063cabadff0a00'
IMAGE_BASE=0x180000000
PATCH_RVA=0x221E6
PATCH_EXPECTED=bytes.fromhex('85 c0 0f 88 c5 00 00 00')
SUCCESS_RVA=0x221EE
FAILURE_RVA=0x222B3
TARGETS_RVA={
 'TEX_BASE':0x2C939A0,
 'HANDLE_BASE':0x2C939C0,
 'PROD_MUTEX_BASE':0x2C93980,
 'CONS_TEX_BASE':0x2C93960,
 'CONS_MUTEX_BASE':0x2C93940,
 'SRV_BASE':0x2C93920,
 'LAST_STATUS':0x2CCE64C,
 'SUCCESS_TARGET':SUCCESS_RVA,
 'FAILURE_TARGET':FAILURE_RVA,
}
SEC_NAME=b'.fgdia\0\0'
SEC_CHARS=0x60000020

def align(v,a): return (v+a-1)&~(a-1)
def sha(b): return hashlib.sha256(b).hexdigest()
def checksum(blob,off):
 b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
 for i in range(0,len(b)-1,2):
  s += b[i] | b[i+1]<<8; s=(s&0xffff)+(s>>16)
 if len(b)&1: s+=b[-1]
 s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
 return (s+len(b))&0xffffffff

def parse_pe(b):
 e=struct.unpack_from('<I',b,0x3c)[0]
 assert b[e:e+4]==b'PE\0\0'
 coff=e+4; machine,nsec,_,_,_,optsz,_=struct.unpack_from('<HHIIIHH',b,coff)
 assert machine==0x8664
 opt=coff+20; assert struct.unpack_from('<H',b,opt)[0]==0x20b
 ib=struct.unpack_from('<Q',b,opt+24)[0]
 sa=struct.unpack_from('<I',b,opt+32)[0]; fa=struct.unpack_from('<I',b,opt+36)[0]
 size_headers=struct.unpack_from('<I',b,opt+60)[0]; csum_off=opt+64
 shoff=opt+optsz; secs=[]
 for i in range(nsec):
  o=shoff+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
  vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8)
  secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
 return locals()

def coff_text(obj):
 b=open(obj,'rb').read(); machine,nsec,_,symptr,nsym,optsz,_=struct.unpack_from('<HHIIIHH',b,0)
 assert machine==0x8664 and optsz==0
 shoff=20; sec=None
 for i in range(nsec):
  o=shoff+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
  vs,va,rawsz,rawptr,relptr,_,nrel,_,_=struct.unpack_from('<IIIIIIHHI',b,o+8)
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
  if typ!=4: raise RuntimeError(f'unexpected reloc type {typ:#x}')
  rel.append((roff,sname(sidx)))
 return code,rel

def build(base,obj,outp):
 src=bytearray(open(base,'rb').read())
 if sha(src)!=EXPECTED_BASE: raise RuntimeError('wrong RC5 base hash')
 pe=parse_pe(src)
 if pe['ib']!=IMAGE_BASE or pe['nsec']!=7: raise RuntimeError('unexpected RC5 PE')
 if any(s['name']=='.fgdia' for s in pe['secs']): raise RuntimeError('already patched')
 text=next(s for s in pe['secs'] if s['name']=='.text')
 poff=text['rp']+(PATCH_RVA-text['va'])
 if bytes(src[poff:poff+8])!=PATCH_EXPECTED:
  raise RuntimeError('patch site mismatch: '+src[poff:poff+8].hex())
 code,rel=coff_text(obj)
 last=max(pe['secs'],key=lambda s:s['va'])
 sec_va=align(last['va']+max(last['vs'],last['rs']),pe['sa'])
 sec_raw=align(len(src),pe['fa']); sec_vs=len(code); sec_rs=align(sec_vs,pe['fa'])
 code_va=pe['ib']+sec_va
 seen=set()
 for roff,name in rel:
  if name not in TARGETS_RVA: raise RuntimeError('unknown symbol '+name)
  target=pe['ib']+TARGETS_RVA[name]
  addend=struct.unpack_from('<i',code,roff)[0]
  disp=target+addend-(code_va+roff+4)
  if not -(1<<31)<=disp<(1<<31): raise RuntimeError('rel32 overflow')
  struct.pack_into('<i',code,roff,disp); seen.add(name)
 if set(TARGETS_RVA)!=seen: raise RuntimeError('missing relocs '+repr(set(TARGETS_RVA)-seen))
 out=bytearray(src)
 if len(out)<sec_raw: out.extend(b'\0'*(sec_raw-len(out)))
 out.extend(b'\0'*(sec_raw+sec_rs-len(out)))
 out[sec_raw:sec_raw+len(code)]=code
 # Redirect only the original TEST/JS gate; success/failure semantics live in leaf stub.
 disp=code_va-(pe['ib']+PATCH_RVA+5)
 out[poff:poff+8]=b'\xE9'+struct.pack('<i',disp)+b'\x90\x90\x90'
 # Add one section header.
 sho=pe['shoff']+pe['nsec']*40
 if sho+40>pe['size_headers'] or any(out[sho:sho+40]): raise RuntimeError('no clean header slack')
 out[sho:sho+40]=SEC_NAME+struct.pack('<IIIIIIHHI',sec_vs,sec_va,sec_rs,sec_raw,0,0,0,0,SEC_CHARS)
 struct.pack_into('<H',out,pe['coff']+2,pe['nsec']+1)
 struct.pack_into('<I',out,pe['opt']+4,struct.unpack_from('<I',out,pe['opt']+4)[0]+sec_rs)
 struct.pack_into('<I',out,pe['opt']+56,align(sec_va+sec_vs,pe['sa']))
 struct.pack_into('<I',out,pe['csum_off'],0); c=checksum(out,pe['csum_off']); struct.pack_into('<I',out,pe['csum_off'],c)
 open(outp,'wb').write(out)
 print('BASE_SHA256='+EXPECTED_BASE)
 print('OUTPUT_SHA256='+sha(out))
 print(f'FGDIA_RVA=0x{sec_va:X}')
 print(f'FGDIA_SIZE=0x{sec_vs:X}')
 print(f'PATCH_RVA=0x{PATCH_RVA:X}')
 print(f'PE_CHECKSUM=0x{c:X}')

if __name__=='__main__':
 if len(sys.argv)!=4: raise SystemExit('usage: builder RC5_DLL DIAG_OBJ OUT_DLL')
 build(*sys.argv[1:])

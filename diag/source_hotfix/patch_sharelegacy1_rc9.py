#!/usr/bin/env python3
import hashlib, struct, sys
BASE_SHA='9d84bb1978a8463df2498455b180125f9aa6a9a2f6ee390cab1382cec44f1d59'
IMAGE_BASE=0x180000000
DIAG_RVA=0x34FD000
DATA_RVA=0x34FC000
SUCCESS_RVA=0x221EE
FAILURE_RVA=0x222B3
LAST_STATUS_RVA=0x2CCE64C
SHARED_FUNC_RVA=0x22D80
SHARED_FLUSH_CONFIG_RVA=0x4B064
PRODUCER_TEX_ARRAY_RVA=0x2C939A0
PRODUCER_MUTEX_ARRAY_RVA=0x2C93980
CONSUMER_TEX_ARRAY_RVA=0x2C93960
CONSUMER_MUTEX_ARRAY_RVA=0x2C93940
CONSUMER_SRV_ARRAY_RVA=0x2C93920
SHARED_HANDLE_ARRAY_RVA=0x2C939C0
FALLBACK_FLAG_RVA=DATA_RVA+0x20
FAKE_VTABLE_RVA=DATA_RVA+0x40
FAKE_OBJECT_RVA=DATA_RVA+0x90
DATA_VSIZE=0x98
DESC_SITE_RVA=0x22E39
PROD_QI_SITE_RVA=0x22EFC
CONS_QI_SITE_RVA=0x22F64
TARGETS={
 'SUCCESS_TARGET':SUCCESS_RVA,
 'FAILURE_TARGET':FAILURE_RVA,
 'LAST_STATUS':LAST_STATUS_RVA,
 'SHARED_FUNC':SHARED_FUNC_RVA,
 'SHARED_FLUSH_CONFIG':SHARED_FLUSH_CONFIG_RVA,
 'PRODUCER_TEX_ARRAY':PRODUCER_TEX_ARRAY_RVA,
 'PRODUCER_MUTEX_ARRAY':PRODUCER_MUTEX_ARRAY_RVA,
 'CONSUMER_TEX_ARRAY':CONSUMER_TEX_ARRAY_RVA,
 'CONSUMER_MUTEX_ARRAY':CONSUMER_MUTEX_ARRAY_RVA,
 'CONSUMER_SRV_ARRAY':CONSUMER_SRV_ARRAY_RVA,
 'SHARED_HANDLE_ARRAY':SHARED_HANDLE_ARRAY_RVA,
 'FALLBACK_FLAG':FALLBACK_FLAG_RVA,
 'FAKE_VTABLE':FAKE_VTABLE_RVA,
 'FAKE_OBJECT':FAKE_OBJECT_RVA,
}
ORIG_DESC=bytes.fromhex('48 b8 00 00 00 00 00 01 00 00 48 89 44 24 64')
ORIG_QI=bytes.fromhex('48 89 ea 4d 89 e0 ff 10')

def sha(b): return hashlib.sha256(b).hexdigest()

def checksum(blob,off):
 b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
 for i in range(0,len(b)-1,2):
  s += b[i] | (b[i+1]<<8); s=(s&0xffff)+(s>>16)
 if len(b)&1: s+=b[-1]
 s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
 return (s+len(b))&0xffffffff

def parse_pe(b):
 e=struct.unpack_from('<I',b,0x3c)[0]
 if b[e:e+4]!=b'PE\0\0': raise RuntimeError('not PE')
 coff=e+4; machine,nsec,_,_,_,optsz,_=struct.unpack_from('<HHIIIHH',b,coff)
 if machine!=0x8664: raise RuntimeError('not AMD64')
 opt=coff+20
 if struct.unpack_from('<H',b,opt)[0]!=0x20b: raise RuntimeError('not PE32+')
 sh=opt+optsz; secs=[]
 for i in range(nsec):
  o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
  vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8)
  secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
 return dict(opt=opt,secs=secs,ib=struct.unpack_from('<Q',b,opt+24)[0],cks_off=opt+64,sizeimg=struct.unpack_from('<I',b,opt+56)[0])

def rva_to_off(pe,rva):
 for s in pe['secs']:
  if s['va']<=rva<s['va']+max(s['vs'],s['rs']): return s['rp']+(rva-s['va'])
 raise RuntimeError('RVA not mapped '+hex(rva))

def coff_text(objpath):
 b=open(objpath,'rb').read()
 machine,nsec,_,symptr,nsym,optsz,_=struct.unpack_from('<HHIIIHH',b,0)
 if machine!=0x8664 or optsz!=0: raise RuntimeError('unexpected COFF')
 sec=None; sec_index=None
 for i in range(nsec):
  o=20+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
  _,_,rawsz,rawptr,relptr,_,nrel,_,_=struct.unpack_from('<IIIIIIHHI',b,o+8)
  if name=='.text': sec=(rawsz,rawptr,relptr,nrel); sec_index=i+1
 if not sec: raise RuntimeError('no .text')
 strtab_off=symptr+nsym*18; slen=struct.unpack_from('<I',b,strtab_off)[0]; stab=b[strtab_off:strtab_off+slen]
 def sym_name(i):
  o=symptr+i*18; rn=b[o:o+8]; z,so=struct.unpack('<II',rn)
  if z==0:
   end=stab.find(b'\0',so); end=len(stab) if end<0 else end
   return stab[so:end].decode('ascii')
  return rn.rstrip(b'\0').decode('ascii')
 symbols={}; i=0
 while i<nsym:
  o=symptr+i*18; name=sym_name(i); val,secno,typ,sc,naux=struct.unpack_from('<IhHBB',b,o+8)
  if secno==sec_index: symbols[name]=val
  i += 1+naux
 rawsz,rawptr,relptr,nrel=sec; code=bytearray(b[rawptr:rawptr+rawsz]); rel=[]
 for j in range(nrel):
  o=relptr+j*10; roff,sidx,rtyp=struct.unpack_from('<IIH',b,o)
  if rtyp!=4: raise RuntimeError(f'unexpected relocation {rtyp:#x}')
  rel.append((roff,sym_name(sidx)))
 return code,rel,symbols

def make_call(src_rva,target_rva,total_len):
 disp=(IMAGE_BASE+target_rva)-(IMAGE_BASE+src_rva+5)
 if not -(1<<31)<=disp<(1<<31): raise RuntimeError('call rel32 overflow')
 return b'\xE8'+struct.pack('<i',disp)+b'\x90'*(total_len-5)

def patch(base,obj,outp):
 src=bytearray(open(base,'rb').read())
 if sha(src)!=BASE_SHA: raise RuntimeError('wrong RC9 base hash '+sha(src))
 pe=parse_pe(src)
 if pe['ib']!=IMAGE_BASE: raise RuntimeError('unexpected image base')
 diag=next(s for s in pe['secs'] if s['name']=='.fgdia')
 dat=next(s for s in pe['secs'] if s['name']=='.fgdat')
 if (diag['va'],diag['rs'])!=(DIAG_RVA,0x200): raise RuntimeError('unexpected .fgdia geometry')
 if (dat['va'],dat['rs'])!=(DATA_RVA,0x200): raise RuntimeError('unexpected .fgdat geometry')
 if dat['vs']!=0x20: raise RuntimeError('unexpected RC9 .fgdat virtual size')
 if any(src[dat['rp']+0x20:dat['rp']+DATA_VSIZE]): raise RuntimeError('reserved .fgdat range not zero')
 code,rel,syms=coff_text(obj)
 for req in ('shared_transport_legacy_gate','shared_desc_misc_helper','shared_mutex_qi_helper'):
  if req not in syms: raise RuntimeError('missing symbol '+req)
 if syms['shared_transport_legacy_gate']!=0: raise RuntimeError('gate must start at .fgdia RVA')
 if len(code)>diag['rs']: raise RuntimeError(f'code {len(code):#x} exceeds .fgdia raw capacity')
 code_va=IMAGE_BASE+DIAG_RVA; counts={}
 for roff,name in rel:
  if name not in TARGETS: raise RuntimeError('unknown external symbol '+name)
  target=IMAGE_BASE+TARGETS[name]; addend=struct.unpack_from('<i',code,roff)[0]
  disp=target+addend-(code_va+roff+4)
  if not -(1<<31)<=disp<(1<<31): raise RuntimeError('rel32 overflow '+name)
  struct.pack_into('<i',code,roff,disp); counts[name]=counts.get(name,0)+1
 out=bytearray(src)
 # Replace post-constructor gate code only; tail is trapped.
 out[diag['rp']:diag['rp']+diag['rs']]=b'\xCC'*diag['rs']
 out[diag['rp']:diag['rp']+len(code)]=code
 struct.pack_into('<I',out,diag['off']+8,len(code))
 # Expand only virtualized zero data inside existing .fgdat raw/page.
 struct.pack_into('<I',out,dat['off']+8,DATA_VSIZE)
 # Conditionalize descriptor and two QI sites.
 do=rva_to_off(pe,DESC_SITE_RVA); po=rva_to_off(pe,PROD_QI_SITE_RVA); co=rva_to_off(pe,CONS_QI_SITE_RVA)
 if bytes(src[do:do+len(ORIG_DESC)])!=ORIG_DESC: raise RuntimeError('descriptor site mismatch '+src[do:do+15].hex())
 if bytes(src[po:po+8])!=ORIG_QI: raise RuntimeError('producer QI site mismatch '+src[po:po+8].hex())
 if bytes(src[co:co+8])!=ORIG_QI: raise RuntimeError('consumer QI site mismatch '+src[co:co+8].hex())
 out[do:do+15]=make_call(DESC_SITE_RVA,DIAG_RVA+syms['shared_desc_misc_helper'],15)
 out[po:po+8]=make_call(PROD_QI_SITE_RVA,DIAG_RVA+syms['shared_mutex_qi_helper'],8)
 out[co:co+8]=make_call(CONS_QI_SITE_RVA,DIAG_RVA+syms['shared_mutex_qi_helper'],8)
 struct.pack_into('<I',out,pe['cks_off'],0); c=checksum(out,pe['cks_off']); struct.pack_into('<I',out,pe['cks_off'],c)
 open(outp,'wb').write(out)
 print('BASE_SHA256='+BASE_SHA)
 print('OUTPUT_SHA256='+sha(out))
 print('SHARELEGACY1_CODE_SIZE=0x%X'%len(code))
 print('DESC_HELPER_RVA=0x%X'%(DIAG_RVA+syms['shared_desc_misc_helper']))
 print('MUTEX_HELPER_RVA=0x%X'%(DIAG_RVA+syms['shared_mutex_qi_helper']))
 print('FGDAT_VSIZE=0x%X'%DATA_VSIZE)
 print('PE_CHECKSUM=0x%X'%c)
 print('RELOC_COUNTS='+repr(counts))

if __name__=='__main__':
 if len(sys.argv)!=4: raise SystemExit('usage: patch_sharelegacy1_rc9.py RC9_DLL OBJ OUT_DLL')
 patch(*sys.argv[1:])

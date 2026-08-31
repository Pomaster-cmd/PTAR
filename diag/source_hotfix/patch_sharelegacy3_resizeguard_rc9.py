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
MAILBOX_FUNC_RVA=0x25370
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
LEGACY_ACTIVE_RVA=DATA_RVA+0x98
DATA_VSIZE=0x9C

PRIMARY_CALL_RVA=0x221E1
DESC_SITE_RVA=0x22E39
PROD_QI_SITE_RVA=0x22EFC
CONS_QI_SITE_RVA=0x22F64
MAILBOX_CALL_RVA=0x242F7
MAILBOX_DESC_RVA=0x25434
MAILBOX_PROD_QI_RVA=0x2552F
MAILBOX_DISP_QI_RVA=0x255CB
RESIZE_GUARD_SITE_RVA=0xCEC3
RESIZE_RETURN_EPILOGUE_RVA=0xD2CC
FG_SCHEDULER_RUNNING_RVA=0x2C93B94

TARGETS={
 'SUCCESS_TARGET':SUCCESS_RVA,
 'FAILURE_TARGET':FAILURE_RVA,
 'LAST_STATUS':LAST_STATUS_RVA,
 'SHARED_FUNC':SHARED_FUNC_RVA,
 'MAILBOX_FUNC':MAILBOX_FUNC_RVA,
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
 'LEGACY_ACTIVE':LEGACY_ACTIVE_RVA,
 'FG_SCHEDULER_RUNNING':FG_SCHEDULER_RUNNING_RVA,
 'RESIZE_RETURN_EPILOGUE':RESIZE_RETURN_EPILOGUE_RVA,
}
ORIG_PRIMARY_CALL=bytes.fromhex('e8 9a 0b 00 00')
ORIG_DESC=bytes.fromhex('48 b8 00 00 00 00 00 01 00 00 48 89 44 24 64')
ORIG_QI=bytes.fromhex('48 89 ea 4d 89 e0 ff 10')
ORIG_MAILBOX_CALL=bytes.fromhex('e8 74 10 00 00')
ORIG_MAILBOX_DESC=bytes.fromhex('c7 44 24 74 00 01 00 00')
ORIG_MAILBOX_PROD_QI=bytes.fromhex('4c 89 e2 4d 89 e8 ff 10')
ORIG_MAILBOX_DISP_QI=bytes.fromhex('48 89 da 4d 89 e8 ff 10')
ORIG_RESIZE_GUARD=bytes.fromhex('89 d5 44 8b bc 24 48 01 00 00')

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

def make_call(src_rva,target_rva,total_len=5):
 disp=(IMAGE_BASE+target_rva)-(IMAGE_BASE+src_rva+5)
 if not -(1<<31)<=disp<(1<<31): raise RuntimeError('call rel32 overflow')
 if total_len<5: raise RuntimeError('call site too short')
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
 reqs=('shared_transport_legacy_gate','shared_primary_call_wrapper','shared_desc_misc_helper','shared_mutex_qi_helper',
       'mailbox_call_wrapper','mailbox_desc_misc_helper','mailbox_producer_mutex_qi_helper','mailbox_display_mutex_qi_helper','ResizeBuffersFGGuard')
 for req in reqs:
  if req not in syms: raise RuntimeError('missing symbol '+req)
 if syms['shared_transport_legacy_gate']!=0: raise RuntimeError('gate must start at .fgdia RVA')
 if len(code)!=0x200: raise RuntimeError(f'unexpected SHARELEGACY3+RESIZEGUARD1 code size {len(code):#x}')
 code_va=IMAGE_BASE+DIAG_RVA; counts={}
 for roff,name in rel:
  if name not in TARGETS: raise RuntimeError('unknown external symbol '+name)
  target=IMAGE_BASE+TARGETS[name]; addend=struct.unpack_from('<i',code,roff)[0]
  disp=target+addend-(code_va+roff+4)
  if not -(1<<31)<=disp<(1<<31): raise RuntimeError('rel32 overflow '+name)
  struct.pack_into('<i',code,roff,disp); counts[name]=counts.get(name,0)+1
 out=bytearray(src)
 out[diag['rp']:diag['rp']+diag['rs']]=b'\xCC'*diag['rs']
 out[diag['rp']:diag['rp']+len(code)]=code
 struct.pack_into('<I',out,diag['off']+8,len(code))
 struct.pack_into('<I',out,dat['off']+8,DATA_VSIZE)

 def exact_patch(rva,original,target_symbol,total_len=None):
  off=rva_to_off(pe,rva); n=len(original) if total_len is None else total_len
  if bytes(src[off:off+len(original)])!=original:
   raise RuntimeError(f'site mismatch {rva:#x}: '+src[off:off+len(original)].hex())
  out[off:off+n]=make_call(rva,DIAG_RVA+syms[target_symbol],n)

 exact_patch(PRIMARY_CALL_RVA,ORIG_PRIMARY_CALL,'shared_primary_call_wrapper',5)
 exact_patch(DESC_SITE_RVA,ORIG_DESC,'shared_desc_misc_helper',15)
 exact_patch(PROD_QI_SITE_RVA,ORIG_QI,'shared_mutex_qi_helper',8)
 exact_patch(CONS_QI_SITE_RVA,ORIG_QI,'shared_mutex_qi_helper',8)
 exact_patch(MAILBOX_CALL_RVA,ORIG_MAILBOX_CALL,'mailbox_call_wrapper',5)
 exact_patch(MAILBOX_DESC_RVA,ORIG_MAILBOX_DESC,'mailbox_desc_misc_helper',8)
 exact_patch(MAILBOX_PROD_QI_RVA,ORIG_MAILBOX_PROD_QI,'mailbox_producer_mutex_qi_helper',8)
 exact_patch(MAILBOX_DISP_QI_RVA,ORIG_MAILBOX_DISP_QI,'mailbox_display_mutex_qi_helper',8)
 exact_patch(RESIZE_GUARD_SITE_RVA,ORIG_RESIZE_GUARD,'ResizeBuffersFGGuard',10)

 struct.pack_into('<I',out,pe['cks_off'],0); c=checksum(out,pe['cks_off']); struct.pack_into('<I',out,pe['cks_off'],c)
 open(outp,'wb').write(out)
 print('BASE_SHA256='+BASE_SHA)
 print('OUTPUT_SHA256='+sha(out))
 print('SHARELEGACY3_RESIZEGUARD1_CODE_SIZE=0x%X'%len(code))
 for s in reqs: print('%s_RVA=0x%X'%(s.upper(),DIAG_RVA+syms[s]))
 print('FGDAT_VSIZE=0x%X'%DATA_VSIZE)
 print('LEGACY_ACTIVE_RVA=0x%X'%LEGACY_ACTIVE_RVA)
 print('FG_SCHEDULER_RUNNING_RVA=0x%X'%FG_SCHEDULER_RUNNING_RVA)
 print('RESIZE_GUARD_SITE_RVA=0x%X'%RESIZE_GUARD_SITE_RVA)
 print('RESIZE_RETURN_EPILOGUE_RVA=0x%X'%RESIZE_RETURN_EPILOGUE_RVA)
 print('RESIZE_LOCK_HRESULT=0x887A0001')
 print('PE_CHECKSUM=0x%X'%c)
 print('RELOC_COUNTS='+repr(counts))

if __name__=='__main__':
 if len(sys.argv)!=4: raise SystemExit('usage: patch_sharelegacy2_rc9.py RC9_DLL OBJ OUT_DLL')
 patch(*sys.argv[1:])

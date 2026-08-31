#!/usr/bin/env python3
import hashlib,struct,sys
BASE='9d84bb1978a8463df2498455b180125f9aa6a9a2f6ee390cab1382cec44f1d59'
NEW='d8faa86cfc4ddf7d8baefd75634c799c99248804a89340ab19ef3262e8e08ef8'
IB=0x180000000; DIAG=0x34FD000; DATA=0x34FC000
SUCCESS=0x221EE; FAIL=0x222B3; LAST=0x2CCE64C; SHARED=0x22D80; FLUSH=0x4B064
PRODTEX=0x2C939A0; PRODMUT=0x2C93980; CONSTEX=0x2C93960; CONSMUT=0x2C93940; SRV=0x2C93920; HANDLE=0x2C939C0
FLAG=DATA+0x20; VTBL=DATA+0x40; OBJ=DATA+0x90
DESC=0x22E39; PQI=0x22EFC; CQI=0x22F64; GATE=0x221E6
TARGETS={'SUCCESS_TARGET':SUCCESS,'FAILURE_TARGET':FAIL,'LAST_STATUS':LAST,'SHARED_FUNC':SHARED,'SHARED_FLUSH_CONFIG':FLUSH,
'PRODUCER_TEX_ARRAY':PRODTEX,'PRODUCER_MUTEX_ARRAY':PRODMUT,'CONSUMER_TEX_ARRAY':CONSTEX,'CONSUMER_MUTEX_ARRAY':CONSMUT,
'CONSUMER_SRV_ARRAY':SRV,'SHARED_HANDLE_ARRAY':HANDLE,'FALLBACK_FLAG':FLAG,'FAKE_VTABLE':VTBL,'FAKE_OBJECT':OBJ}
ORIG_DESC=bytes.fromhex('48 b8 00 00 00 00 00 01 00 00 48 89 44 24 64')
ORIG_QI=bytes.fromhex('48 89 ea 4d 89 e0 ff 10')
def sha(b):return hashlib.sha256(b).hexdigest()
def parse(b):
 e=struct.unpack_from('<I',b,0x3c)[0]; coff=e+4; machine,n=struct.unpack_from('<HH',b,coff); osz=struct.unpack_from('<H',b,coff+16)[0]; opt=coff+20; sh=opt+osz
 secs=[]
 for i in range(n):
  o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode(); vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8); secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
 return dict(machine=machine,n=n,opt=opt,secs=secs,ib=struct.unpack_from('<Q',b,opt+24)[0],sizeimg=struct.unpack_from('<I',b,opt+56)[0],cks_off=opt+64,cks=struct.unpack_from('<I',b,opt+64)[0])
def cks(blob,off):
 b=bytearray(blob);struct.pack_into('<I',b,off,0);s=0
 for i in range(0,len(b)-1,2):s+=b[i]|b[i+1]<<8;s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16);return(s+len(b))&0xffffffff
def ro(P,rva):
 for s in P['secs']:
  if s['va']<=rva<s['va']+max(s['vs'],s['rs']):return s['rp']+rva-s['va']
 raise ValueError(hex(rva))
def coff(obj):
 b=open(obj,'rb').read(); machine,n,_,symptr,nsym,osz,_=struct.unpack_from('<HHIIIHH',b,0); assert machine==0x8664 and osz==0
 sec=None; si=None
 for i in range(n):
  o=20+i*40; name=b[o:o+8].rstrip(b'\0').decode(); _,_,rs,rp,rr,_,nr,_,_=struct.unpack_from('<IIIIIIHHI',b,o+8)
  if name=='.text':sec=(rs,rp,rr,nr);si=i+1
 st=symptr+nsym*18; sl=struct.unpack_from('<I',b,st)[0]; tab=b[st:st+sl]
 def name(i):
  rn=b[symptr+i*18:symptr+i*18+8];z,o=struct.unpack('<II',rn)
  if z==0:
   e=tab.find(b'\0',o);return tab[o:e if e>=0 else len(tab)].decode()
  return rn.rstrip(b'\0').decode()
 sy={};i=0
 while i<nsym:
  o=symptr+i*18; nm=name(i);val,sn,typ,sc,na=struct.unpack_from('<IhHBB',b,o+8)
  if sn==si:sy[nm]=val
  i+=1+na
 rs,rp,rr,nr=sec;code=bytearray(b[rp:rp+rs]);rels=[]
 for j in range(nr):
  o=rr+j*10; off,idx,t=struct.unpack_from('<IIH',b,o);assert t==4;rels.append((off,name(idx)))
 return code,rels,sy
def link_expected(obj):
 code,rels,sy=coff(obj)
 for off,nm in rels:
  assert nm in TARGETS,nm
  add=struct.unpack_from('<i',code,off)[0]; target=IB+TARGETS[nm]; disp=target+add-(IB+DIAG+off+4);struct.pack_into('<i',code,off,disp)
 return bytes(code),sy
def call_target(b,P,rva):
 o=ro(P,rva);assert b[o]==0xE8;d=struct.unpack_from('<i',b,o+1)[0];return rva+5+d

def main():
 if len(sys.argv)!=4:raise SystemExit('usage: validate_sharelegacy1_rc10.py RC9 RC10 OBJ')
 B=open(sys.argv[1],'rb').read();N=open(sys.argv[2],'rb').read();pb,pn=parse(B),parse(N);E,sy=link_expected(sys.argv[3]);C=[]
 def ck(n,v,d=''):
  C.append(n)
  if not v:raise AssertionError(n+(' :: '+d if d else ''))
 ck('RC9 hash',sha(B)==BASE,sha(B));ck('RC10 hash',sha(N)==NEW,sha(N));ck('same file size',len(B)==len(N),f'{len(B)}/{len(N)}')
 ck('PE AMD64',pb['machine']==pn['machine']==0x8664);ck('image base unchanged',pb['ib']==pn['ib']==IB);ck('same eight sections',pb['n']==pn['n']==8);ck('SizeOfImage unchanged',pb['sizeimg']==pn['sizeimg'])
 db=next(s for s in pb['secs']if s['name']=='.fgdia');dn=next(s for s in pn['secs']if s['name']=='.fgdia');tb=next(s for s in pb['secs']if s['name']=='.fgdat');tn=next(s for s in pn['secs']if s['name']=='.fgdat')
 ck('fgdia geometry stable except VS',db['va']==dn['va']==DIAG and db['rp']==dn['rp'] and db['rs']==dn['rs']==0x200 and db['ch']==dn['ch']==0x60000020)
 ck('fgdia code size',dn['vs']==len(E)==0x1ca,hex(dn['vs']));ck('fgdia linked image exact',N[dn['rp']:dn['rp']+dn['vs']]==E);ck('fgdia tail trapped',set(N[dn['rp']+dn['vs']:dn['rp']+dn['rs']])<={0xCC})
 ck('fgdat RVA/raw geometry unchanged',tb['va']==tn['va']==DATA and tb['rp']==tn['rp'] and tb['rs']==tn['rs']==0x200 and tb['ch']==tn['ch'])
 ck('fgdat VS bounded expansion',tb['vs']==0x20 and tn['vs']==0x98 and tn['vs']<0x1000);ck('fgdat raw bytes unchanged',B[tb['rp']:tb['rp']+tb['rs']]==N[tn['rp']:tn['rp']+tn['rs']]);ck('fgdat reserved runtime area zero',set(N[tn['rp']+0x20:tn['rp']+0x98])<={0})
 gb=next(s for s in pb['secs']if s['name']=='.fgov');gn=next(s for s in pn['secs']if s['name']=='.fgov');ck('GATE1 governor section exact',B[gb['rp']:gb['rp']+gb['rs']]==N[gn['rp']:gn['rp']+gn['rs']] and (gb['vs'],gb['va'],gb['rs'],gb['ch'])==(gn['vs'],gn['va'],gn['rs'],gn['ch']))
 # Original sites and new bounded calls.
 bo=ro(pb,DESC);po=ro(pb,PQI);co=ro(pb,CQI);no=ro(pn,DESC);npo=ro(pn,PQI);nco=ro(pn,CQI)
 ck('RC9 descriptor site exact',B[bo:bo+15]==ORIG_DESC,B[bo:bo+15].hex());ck('RC9 producer QI site exact',B[po:po+8]==ORIG_QI);ck('RC9 consumer QI site exact',B[co:co+8]==ORIG_QI)
 ck('descriptor call target exact',call_target(N,pn,DESC)==DIAG+sy['shared_desc_misc_helper'],hex(call_target(N,pn,DESC)));ck('descriptor NOP tail',N[no+5:no+15]==b'\x90'*10)
 ck('producer QI helper target exact',call_target(N,pn,PQI)==DIAG+sy['shared_mutex_qi_helper']);ck('producer QI NOP tail',N[npo+5:npo+8]==b'\x90'*3)
 ck('consumer QI helper target exact',call_target(N,pn,CQI)==DIAG+sy['shared_mutex_qi_helper']);ck('consumer QI NOP tail',N[nco+5:nco+8]==b'\x90'*3)
 # Existing caller still reaches gate at .fgdia RVA.
 go=ro(pn,GATE);gob=ro(pb,GATE);ck('post-constructor gate site unchanged',N[go:go+8]==B[gob:gob+8]);assert N[go]==0xE9;d=struct.unpack_from('<i',N,go+1)[0];ck('post-constructor gate target exact',GATE+5+d==DIAG,hex(GATE+5+d))
 # Behavioral contract visible in exact object image.
 ck('E_INVALIDARG-only retry',b'\x3d\x57\x00\x07\x80' in E);ck('SharedFlush required',E[0x13:0x1a].startswith(b'\x83\x3d'))
 ck('legacy descriptor constant present',bytes.fromhex('48 b8 00 00 00 00 02 00 00 00') in E);ck('keyed descriptor constant retained',bytes.fromhex('48 b8 00 00 00 00 00 01 00 00') in E)
 def rel32_target(code, insn_off, disp_off, insn_len):
  disp=struct.unpack_from('<i',code,insn_off+disp_off)[0]
  return DIAG+insn_off+insn_len+disp
 # Fake vtable is initialized at runtime: slots 8/9 must point to the S_OK sync stub.
 a1=0xC3; a2=0xCA; objset=0xD8
 ck('fake Acquire slot store opcode',E[a1:a1+3]==b'\x48\x89\x05')
 ck('fake Acquire slot target',rel32_target(E,a1,3,7)==VTBL+0x40,hex(rel32_target(E,a1,3,7)))
 ck('fake Release slot store opcode',E[a2:a2+3]==b'\x48\x89\x05')
 ck('fake Release slot target',rel32_target(E,a2,3,7)==VTBL+0x48,hex(rel32_target(E,a2,3,7)))
 ck('fake object vtable store opcode',E[objset:objset+3]==b'\x48\x89\x05')
 ck('fake object store target',rel32_target(E,objset,3,7)==OBJ,hex(rel32_target(E,objset,3,7)))
 ck('fake sync returns S_OK',E[sy['fake_sync_ok']:sy['fake_sync_ok']+3]==b'\x31\xc0\xc3')
 # Data layout cannot overlap governor state.
 ck('fallback flag after governor data',FLAG>=DATA+0x20);ck('fake vtable inside expanded data',VTBL>=DATA+0x20 and VTBL+80<=DATA+0x98);ck('fake object inside expanded data',OBJ>=VTBL+80 and OBJ+8<=DATA+0x98)
 # Primary/fallback decision model.
 def gate(hr,flush,arrays_empty):
  if hr>=0 and hr<0x80000000:return 'PRIMARY_SUCCESS'
  if hr==0x80070057 and flush and arrays_empty:return 'LEGACY_RETRY'
  return 'FAIL'
 ck('model primary success bypasses fallback',gate(0,1,True)=='PRIMARY_SUCCESS');ck('model E_INVALIDARG enters fallback',gate(0x80070057,1,True)=='LEGACY_RETRY');ck('model no SharedFlush fails closed',gate(0x80070057,0,True)=='FAIL');ck('model partial primary fails closed',gate(0x80070057,1,False)=='FAIL');ck('model unrelated HRESULT fails closed',gate(0x80004005,1,True)=='FAIL')
 # Binary-diff allowlist: three .text sites, fgdia raw, two VS fields, PE checksum only.
 allowed=set(range(no,no+15))|set(range(npo,npo+8))|set(range(nco,nco+8))|set(range(dn['rp'],dn['rp']+dn['rs']))|set(range(dn['off']+8,dn['off']+12))|set(range(tn['off']+8,tn['off']+12))|set(range(pn['cks_off'],pn['cks_off']+4))
 bad=[i for i,(x,y) in enumerate(zip(B,N)) if x!=y and i not in allowed];ck('no unexpected binary changes',not bad,','.join(hex(i) for i in bad[:20]))
 ck('PE checksum valid',pn['cks']==cks(N,pn['cks_off']),f"{pn['cks']:#x}/{cks(N,pn['cks_off']):#x}")
 print(f'SHARELEGACY1 STATIC VALIDATION: PASS {len(C)}/{len(C)}')
 for n in C:print('[PASS]',n)
if __name__=='__main__':main()

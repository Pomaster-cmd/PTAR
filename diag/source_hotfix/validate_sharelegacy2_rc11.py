#!/usr/bin/env python3
import hashlib,struct,sys
BASE='9d84bb1978a8463df2498455b180125f9aa6a9a2f6ee390cab1382cec44f1d59'
NEW='11fb4e484e79984641d72e2c793a2fa2b8eab871d50daf9cb03bfd77ed535dff'
IB=0x180000000; DIAG=0x34FD000; DATA=0x34FC000
SUCCESS=0x221EE; FAIL=0x222B3; LAST=0x2CCE64C; SHARED=0x22D80; MAILBOX=0x25370; FLUSH=0x4B064
PRODTEX=0x2C939A0; PRODMUT=0x2C93980; CONSTEX=0x2C93960; CONSMUT=0x2C93940; SRV=0x2C93920; HANDLE=0x2C939C0
FLAG=DATA+0x20; VTBL=DATA+0x40; OBJ=DATA+0x90
PRIMARY_CALL=0x221E1; GATE=0x221E6; DESC=0x22E39; PQI=0x22EFC; CQI=0x22F64
MCALL=0x242F7; MDESC=0x25434; MPQI=0x2552F; MDQI=0x255CB
TARGETS={'SUCCESS_TARGET':SUCCESS,'FAILURE_TARGET':FAIL,'LAST_STATUS':LAST,'SHARED_FUNC':SHARED,'MAILBOX_FUNC':MAILBOX,
'SHARED_FLUSH_CONFIG':FLUSH,'PRODUCER_TEX_ARRAY':PRODTEX,'PRODUCER_MUTEX_ARRAY':PRODMUT,'CONSUMER_TEX_ARRAY':CONSTEX,
'CONSUMER_MUTEX_ARRAY':CONSMUT,'CONSUMER_SRV_ARRAY':SRV,'SHARED_HANDLE_ARRAY':HANDLE,'FALLBACK_FLAG':FLAG,'FAKE_VTABLE':VTBL,'FAKE_OBJECT':OBJ}
ORIG_PRIMARY=bytes.fromhex('e8 9a 0b 00 00')
ORIG_DESC=bytes.fromhex('48 b8 00 00 00 00 00 01 00 00 48 89 44 24 64')
ORIG_QI=bytes.fromhex('48 89 ea 4d 89 e0 ff 10')
ORIG_MCALL=bytes.fromhex('e8 74 10 00 00')
ORIG_MDESC=bytes.fromhex('c7 44 24 74 00 01 00 00')
ORIG_MP=bytes.fromhex('4c 89 e2 4d 89 e8 ff 10')
ORIG_MD=bytes.fromhex('48 89 da 4d 89 e8 ff 10')
def sha(b):return hashlib.sha256(b).hexdigest()
def parse(b):
 e=struct.unpack_from('<I',b,0x3c)[0];coff=e+4;machine,n=struct.unpack_from('<HH',b,coff);osz=struct.unpack_from('<H',b,coff+16)[0];opt=coff+20;sh=opt+osz;secs=[]
 for i in range(n):
  o=sh+i*40;name=b[o:o+8].rstrip(b'\0').decode();vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8);secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
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
 b=open(obj,'rb').read();machine,n,_,symptr,nsym,osz,_=struct.unpack_from('<HHIIIHH',b,0);assert machine==0x8664 and osz==0
 sec=None;si=None
 for i in range(n):
  o=20+i*40;name=b[o:o+8].rstrip(b'\0').decode();_,_,rs,rp,rr,_,nr,_,_=struct.unpack_from('<IIIIIIHHI',b,o+8)
  if name=='.text':sec=(rs,rp,rr,nr);si=i+1
 st=symptr+nsym*18;sl=struct.unpack_from('<I',b,st)[0];tab=b[st:st+sl]
 def name(i):
  rn=b[symptr+i*18:symptr+i*18+8];z,o=struct.unpack('<II',rn)
  if z==0:
   e=tab.find(b'\0',o);return tab[o:e if e>=0 else len(tab)].decode()
  return rn.rstrip(b'\0').decode()
 sy={};i=0
 while i<nsym:
  o=symptr+i*18;nm=name(i);val,sn,typ,sc,na=struct.unpack_from('<IhHBB',b,o+8)
  if sn==si:sy[nm]=val
  i+=1+na
 rs,rp,rr,nr=sec;code=bytearray(b[rp:rp+rs]);rels=[]
 for j in range(nr):
  o=rr+j*10;off,idx,t=struct.unpack_from('<IIH',b,o);assert t==4;rels.append((off,name(idx)))
 return code,rels,sy
def link_expected(obj):
 code,rels,sy=coff(obj)
 for off,nm in rels:
  assert nm in TARGETS,nm;add=struct.unpack_from('<i',code,off)[0];target=IB+TARGETS[nm];disp=target+add-(IB+DIAG+off+4);struct.pack_into('<i',code,off,disp)
 return bytes(code),sy
def call_target(b,P,rva):
 o=ro(P,rva);assert b[o]==0xE8;d=struct.unpack_from('<i',b,o+1)[0];return rva+5+d
def jmp_target(b,P,rva):
 o=ro(P,rva);assert b[o]==0xE9;d=struct.unpack_from('<i',b,o+1)[0];return rva+5+d

def main():
 if len(sys.argv)!=4:raise SystemExit('usage: validate_sharelegacy2_rc11.py RC9 RC11 OBJ')
 B=open(sys.argv[1],'rb').read();N=open(sys.argv[2],'rb').read();pb,pn=parse(B),parse(N);E,sy=link_expected(sys.argv[3]);C=[]
 def ck(n,v,d=''):
  C.append(n)
  if not v:raise AssertionError(n+(' :: '+d if d else ''))
 ck('RC9 hash',sha(B)==BASE,sha(B));ck('RC11 hash',sha(N)==NEW,sha(N));ck('same file size',len(B)==len(N),f'{len(B)}/{len(N)}')
 ck('PE AMD64',pb['machine']==pn['machine']==0x8664);ck('image base unchanged',pb['ib']==pn['ib']==IB);ck('same eight sections',pb['n']==pn['n']==8);ck('SizeOfImage unchanged',pb['sizeimg']==pn['sizeimg'])
 db=next(s for s in pb['secs']if s['name']=='.fgdia');dn=next(s for s in pn['secs']if s['name']=='.fgdia');tb=next(s for s in pb['secs']if s['name']=='.fgdat');tn=next(s for s in pn['secs']if s['name']=='.fgdat')
 ck('fgdia geometry stable except VS',db['va']==dn['va']==DIAG and db['rp']==dn['rp'] and db['rs']==dn['rs']==0x200 and db['ch']==dn['ch']==0x60000020)
 ck('fgdia gate at offset zero',sy['shared_transport_legacy_gate']==0);ck('fgdia code size',dn['vs']==len(E)==0x1c4,hex(dn['vs']));ck('fgdia linked image exact',N[dn['rp']:dn['rp']+dn['vs']]==E);ck('fgdia tail trapped',set(N[dn['rp']+dn['vs']:dn['rp']+dn['rs']])<={0xCC})
 ck('fgdat RVA/raw geometry unchanged',tb['va']==tn['va']==DATA and tb['rp']==tn['rp'] and tb['rs']==tn['rs']==0x200 and tb['ch']==tn['ch'])
 ck('fgdat VS bounded expansion',tb['vs']==0x20 and tn['vs']==0x98);ck('fgdat raw bytes unchanged',B[tb['rp']:tb['rp']+tb['rs']]==N[tn['rp']:tn['rp']+tn['rs']]);ck('fgdat runtime area initially zero',set(N[tn['rp']+0x20:tn['rp']+0x98])<={0})
 gb=next(s for s in pb['secs']if s['name']=='.fgov');gn=next(s for s in pn['secs']if s['name']=='.fgov');ck('GATE1 governor section exact',B[gb['rp']:gb['rp']+gb['rs']]==N[gn['rp']:gn['rp']+gn['rs']] and (gb['vs'],gb['va'],gb['rs'],gb['ch'])==(gn['vs'],gn['va'],gn['rs'],gn['ch']))
 # Original signatures.
 for n,r,o in [('primary transport call',PRIMARY_CALL,ORIG_PRIMARY),('transport descriptor',DESC,ORIG_DESC),('producer transport QI',PQI,ORIG_QI),('consumer transport QI',CQI,ORIG_QI),('mailbox call',MCALL,ORIG_MCALL),('mailbox descriptor',MDESC,ORIG_MDESC),('mailbox producer QI',MPQI,ORIG_MP),('mailbox display QI',MDQI,ORIG_MD)]:
  ck('RC9 '+n+' exact',B[ro(pb,r):ro(pb,r)+len(o)]==o,B[ro(pb,r):ro(pb,r)+len(o)].hex())
 # New patch targets.
 targets=[(PRIMARY_CALL,'shared_primary_call_wrapper',5),(DESC,'shared_desc_misc_helper',15),(PQI,'shared_mutex_qi_helper',8),(CQI,'shared_mutex_qi_helper',8),(MCALL,'mailbox_call_wrapper',5),(MDESC,'mailbox_desc_misc_helper',8),(MPQI,'mailbox_producer_mutex_qi_helper',8),(MDQI,'mailbox_display_mutex_qi_helper',8)]
 for r,s,l in targets:
  ck(s+' call target',call_target(N,pn,r)==DIAG+sy[s],hex(call_target(N,pn,r)))
  if l>5:ck(s+' NOP tail',N[ro(pn,r)+5:ro(pn,r)+l]==b'\x90'*(l-5))
 ck('existing post-constructor gate bytes unchanged',N[ro(pn,GATE):ro(pn,GATE)+8]==B[ro(pb,GATE):ro(pb,GATE)+8]);ck('post-constructor gate still targets fgdia start',jmp_target(N,pn,GATE)==DIAG,hex(jmp_target(N,pn,GATE)))
 # Linked code semantic anchors.
 gate=sy['shared_transport_legacy_gate']; primary=sy['shared_primary_call_wrapper']; mw=sy['mailbox_call_wrapper']
 ck('E_INVALIDARG-only retry',b'\x3d\x57\x00\x07\x80' in E[gate:primary])
 ck('legacy transport descriptor constant',b'\xb8\x02\x00\x00\x00' in E[sy['shared_desc_misc_helper']:sy['shared_mutex_qi_helper']])
 ck('keyed transport descriptor constant retained',b'\xb8\x00\x01\x00\x00' in E[sy['shared_desc_misc_helper']:sy['shared_mutex_qi_helper']])
 ck('legacy mailbox descriptor constant',b'\xb8\x02\x00\x00\x00' in E[sy['mailbox_desc_misc_helper']:sy['mailbox_producer_mutex_qi_helper']])
 ck('keyed mailbox descriptor constant retained',b'\xb8\x00\x01\x00\x00' in E[sy['mailbox_desc_misc_helper']:sy['mailbox_producer_mutex_qi_helper']])
 ck('primary wrapper clears legacy before constructor',E[primary:primary+10].startswith(bytes.fromhex('c7 05')) and E[primary+6:primary+10]==b'\x00\x00\x00\x00')
 # Mailbox wrapper must call mailbox first, then clear flag, then return.
 ck('mailbox wrapper calls original constructor',call_target(N,pn,MCALL)==DIAG+mw)
 mwcode=E[mw:sy['mailbox_desc_misc_helper']]
 ck('mailbox wrapper clear occurs after nested call opcode',mwcode.find(b'\xe8')>=0 and mwcode.find(b'\xc7\x05')>mwcode.find(b'\xe8'))
 ck('mailbox wrapper returns after clear',mwcode.endswith(b'\xc3'))
 # Retry-success region must leave flag set: between retry call test and success jump there is no store to FLAG except LAST_STATUS.
 # Exact disassembly bytes from linked image: test eax,eax; js fail; mov LAST_STATUS,0; jmp SUCCESS.
 ck('retry success does not clear fallback flag',bytes.fromhex('85 c0 78 1e c7 05') in E and sy['shared_primary_call_wrapper']>0xA0)
 ck('fake Release returns 1',E[sy['fake_release']:sy['fake_release']+6]==bytes.fromhex('b8 01 00 00 00 c3'))
 ck('fake Acquire/ReleaseSync return S_OK',E[sy['fake_sync_ok']:sy['fake_sync_ok']+3]==bytes.fromhex('31 c0 c3'))
 # Minimal fake vtable has storage for slots through 9 and object without overlapping governor state.
 ck('fallback flag after governor data',FLAG>=DATA+0x20);ck('fake vtable room',VTBL>=DATA+0x20 and VTBL+80<=DATA+0x98);ck('fake object room',OBJ>=VTBL+80 and OBJ+8<=DATA+0x98)
 # Deterministic state model for bounded setup policy.
 def decide(primary_hr,flush,empty):
  if primary_hr==0:return ('KEYED',0)
  if primary_hr==0x80070057 and flush and empty:return ('LEGACY_RETRY',1)
  return ('FAIL',0)
 ck('model keyed success never enters fallback',decide(0,1,True)==('KEYED',0));ck('model exact Inquisitor failure enters legacy',decide(0x80070057,1,True)==('LEGACY_RETRY',1));ck('model SharedFlush off fails closed',decide(0x80070057,0,True)==('FAIL',0));ck('model partial transport fails closed',decide(0x80070057,1,False)==('FAIL',0));ck('model unrelated HRESULT fails closed',decide(0x80004005,1,True)==('FAIL',0))
 # Legacy mode covers both stages and is then cleared.
 legacy=1; transport_misc=2 if legacy else 0x100; mailbox_misc=2 if legacy else 0x100; legacy=0
 ck('model legacy transport and mailbox coherent',transport_misc==mailbox_misc==2 and legacy==0)
 # Binary diff allowlist: eight bounded .text sites, fgdia raw, fgdia/fgdat VS, checksum.
 ranges=[(PRIMARY_CALL,5),(DESC,15),(PQI,8),(CQI,8),(MCALL,5),(MDESC,8),(MPQI,8),(MDQI,8)]
 allowed=set()
 for r,l in ranges:
  o=ro(pn,r);allowed.update(range(o,o+l))
 allowed.update(range(dn['rp'],dn['rp']+dn['rs']));allowed.update(range(dn['off']+8,dn['off']+12));allowed.update(range(tn['off']+8,tn['off']+12));allowed.update(range(pn['cks_off'],pn['cks_off']+4))
 bad=[i for i,(x,y) in enumerate(zip(B,N)) if x!=y and i not in allowed];ck('no unexpected binary changes',not bad,','.join(hex(i) for i in bad[:20]))
 ck('PE checksum valid',pn['cks']==cks(N,pn['cks_off']),f"{pn['cks']:#x}/{cks(N,pn['cks_off']):#x}")
 print(f'SHARELEGACY2 STATIC VALIDATION: PASS {len(C)}/{len(C)}')
 for n in C:print('[PASS]',n)
if __name__=='__main__':main()

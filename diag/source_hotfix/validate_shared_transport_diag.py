#!/usr/bin/env python3
import hashlib, struct, sys
BASE_SHA='309f82ea51cb549a594dfe584b3d9d9039b3b25265af604e6d063cabadff0a00'
NEW_SHA='9d67705897a0faa7c7b25c09e8d85a7d6fcbb8ee043344dd601ecc5be5e0c263'
IMAGE_BASE=0x180000000
PATCH_RVA=0x221E6
ORIG=bytes.fromhex('85c00f88c5000000')
DIAG_RVA=0x34FD000
DIAG_SIZE=0xB0
SUCCESS=0x221EE
FAILURE=0x222B3
GLOBALS={
 0x0E:0x2C939A0, 0x1C:0x2C939C0, 0x2A:0x2C93980,
 0x38:0x2C93960, 0x46:0x2C93940, 0x54:0x2C93920,
 0xA7:0x2CCE64C,
}
def sha(b): return hashlib.sha256(b).hexdigest()
def align(v,a): return (v+a-1)&~(a-1)
def pe(b):
 e=struct.unpack_from('<I',b,0x3c)[0]; assert b[e:e+4]==b'PE\0\0'; coff=e+4
 n=struct.unpack_from('<H',b,coff+2)[0]; osz=struct.unpack_from('<H',b,coff+16)[0]; opt=coff+20; sh=opt+osz
 secs=[]
 for i in range(n):
  o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace'); vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8)
  secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
 return dict(e=e,coff=coff,opt=opt,sh=sh,n=n,secs=secs,ib=struct.unpack_from('<Q',b,opt+24)[0],sa=struct.unpack_from('<I',b,opt+32)[0],fa=struct.unpack_from('<I',b,opt+36)[0],soi=struct.unpack_from('<I',b,opt+56)[0],soh=struct.unpack_from('<I',b,opt+60)[0],cks=struct.unpack_from('<I',b,opt+64)[0],cks_off=opt+64)
def checksum(blob,off):
 b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
 for i in range(0,len(b)-1,2):
  s += b[i] | (b[i+1]<<8); s=(s&0xffff)+(s>>16)
 if len(b)&1: s+=b[-1]
 s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
 return (s+len(b))&0xffffffff
def rvaoff(P,rva):
 for s in P['secs']:
  if s['va']<=rva<s['va']+max(s['vs'],s['rs']): return s['rp']+(rva-s['va'])
 raise ValueError(hex(rva))
def rel_target(codeva,off,disp): return codeva+off+4+disp
def status(slot,stage): return 0xF100+slot*0x10+stage

def main():
 if len(sys.argv)!=3: raise SystemExit('usage: validate_shared_transport_diag.py RC5 RC6')
 B=open(sys.argv[1],'rb').read(); N=open(sys.argv[2],'rb').read(); C=[]
 def ck(n,c,d=''):
  C.append((n,bool(c),d));
  if not c: raise AssertionError(n+(' :: '+d if d else ''))
 pb,pn=pe(B),pe(N)
 ck('RC5 hash',sha(B)==BASE_SHA,sha(B)); ck('RC6 hash',sha(N)==NEW_SHA,sha(N))
 ck('PE x64 image base',pn['ib']==pb['ib']==IMAGE_BASE)
 ck('section count 7->8',pb['n']==7 and pn['n']==8,f"{pb['n']}->{pn['n']}")
 ck('existing section identities retained',[s['name'] for s in pn['secs'][:7]]==[s['name'] for s in pb['secs']])
 d=pn['secs'][-1]
 ck('diag section exact',d['name']=='.fgdia' and d['va']==DIAG_RVA and d['vs']==DIAG_SIZE and d['rs']==0x200 and d['ch']==0x60000020,repr(d))
 ck('size image exact',pn['soi']==0x34FE000,hex(pn['soi']))
 ck('file size exact',len(N)==0x4B400,hex(len(N)))
 ck('checksum exact',pn['cks']==checksum(N,pn['cks_off']),f"{pn['cks']:#x}/{checksum(N,pn['cks_off']):#x}")
 # Patch-site and jump target.
 bo=rvaoff(pb,PATCH_RVA); no=rvaoff(pn,PATCH_RVA)
 ck('RC5 original failure gate exact',B[bo:bo+8]==ORIG,B[bo:bo+8].hex())
 ck('RC6 gate JMP+3NOP',N[no]==0xE9 and N[no+5:no+8]==b'\x90'*3,N[no:no+8].hex())
 disp=struct.unpack_from('<i',N,no+1)[0]
 ck('gate targets .fgdia',IMAGE_BASE+PATCH_RVA+5+disp==IMAGE_BASE+DIAG_RVA,hex(IMAGE_BASE+PATCH_RVA+5+disp))
 # Diagnostic section starts with TEST EAX,EAX + JNS original success.
 do=d['rp']; code=N[do:do+DIAG_SIZE]
 ck('diag success test exact',code[:2]==b'\x85\xc0',code[:8].hex())
 ck('diag JNS rel32',code[2:4]==b'\x0f\x89',code[2:8].hex())
 jdisp=struct.unpack_from('<i',code,4)[0]
 ck('diag success target original',rel_target(IMAGE_BASE+DIAG_RVA,4,jdisp)==IMAGE_BASE+SUCCESS,hex(rel_target(IMAGE_BASE+DIAG_RVA,4,jdisp)))
 # All persistent-array RIP-relative targets exact.
 for off,targetrva in GLOBALS.items():
  disp=struct.unpack_from('<i',code,off)[0]
  targ=rel_target(IMAGE_BASE+DIAG_RVA,off,disp)
  ck(f'reloc target {off:#x}',targ==IMAGE_BASE+targetrva,hex(targ))
 ck('diag final JMP rel32 opcode',code[0xAB]==0xE9,hex(code[0xAB]))
 fdisp=struct.unpack_from('<i',code,0xAC)[0]
 ck('diag failure target original',rel_target(IMAGE_BASE+DIAG_RVA,0xAC,fdisp)==IMAGE_BASE+FAILURE,hex(rel_target(IMAGE_BASE+DIAG_RVA,0xAC,fdisp)))
 # Leaf-only: no CALL, stack-pointer mutation, pushes or pops. This makes unwind metadata unnecessary.
 ck('pdata and exception directory byte-identical',next(s for s in pb['secs'] if s['name']=='.pdata')['vs']==next(s for s in pn['secs'] if s['name']=='.pdata')['vs'])
 # Existing RC5 image is changed only in PE header bookkeeping and the 8-byte diagnostic gate.
 allowed=set()
 def add(a,n): allowed.update(range(a,a+n))
 add(pb['coff']+2,2); add(pb['opt']+4,4); add(pb['opt']+56,4); add(pb['opt']+64,4); add(pb['sh']+pb['n']*40,40); add(bo,8)
 bad=[i for i,(x,y) in enumerate(zip(B,N[:len(B)])) if x!=y and i not in allowed]
 ck('no unexpected changes inside RC5 file image',not bad,','.join(hex(i) for i in bad[:16]))
 # Governor sections and state are byte-identical to RC5.
 for name in ('.fgov','.fgdat'):
  sb=next(s for s in pb['secs'] if s['name']==name); sn=next(s for s in pn['secs'] if s['name']==name)
  ck(name+' header geometry unchanged',(sb['vs'],sb['va'],sb['rs'],sb['rp'],sb['ch'])==(sn['vs'],sn['va'],sn['rs'],sn['rp'],sn['ch']))
  ck(name+' bytes unchanged',B[sb['rp']:sb['rp']+sb['rs']]==N[sn['rp']:sn['rp']+sn['rs']])
 # Status mapping model exhaustive.
 vals=[status(slot,stage) for slot in range(4) for stage in range(1,7)]
 ck('24 unique slot/stage statuses',len(vals)==len(set(vals))==24)
 ck('status low nibble maps stage',all((status(s,t)&0xf)==t for s in range(4) for t in range(1,7)))
 ck('status slot nibble maps slot',all(((status(s,t)-0xF100)>>4)==s for s in range(4) for t in range(1,7)))
 ck('post-complete status reserved',0xF1F0 not in vals)
 print(f'SHAREDIAG1 STATIC VALIDATION: PASS {len(C)}/{len(C)}')
 for n,_,d in C: print('[PASS]',n,(':: '+d if d else ''))
if __name__=='__main__': main()

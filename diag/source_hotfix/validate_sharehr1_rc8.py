#!/usr/bin/env python3
import hashlib,struct,sys
BASE='f871c6a5e3a6a7cf1430d5fdd609d5e5f81a98f5c141559af81a5d51718cd2d2'
NEW='f26bbf5919ddeeb39937e5258d6c56f3196936e7d9a755099d55404693326d98'
IB=0x180000000; DIAG=0x34FD000; SUCCESS=0x221EE; FAIL=0x222B3; LAST=0x2CCE64C; GATE=0x221E6

def sha(b): return hashlib.sha256(b).hexdigest()
def pe(b):
 e=struct.unpack_from('<I',b,0x3c)[0]; coff=e+4; n=struct.unpack_from('<H',b,coff+2)[0]; osz=struct.unpack_from('<H',b,coff+16)[0]; opt=coff+20; sh=opt+osz
 secs=[]
 for i in range(n):
  o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode(); vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8); secs.append(dict(name=name,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
 return dict(coff=coff,opt=opt,n=n,secs=secs,ib=struct.unpack_from('<Q',b,opt+24)[0],cks_off=opt+64,cks=struct.unpack_from('<I',b,opt+64)[0])
def cks(blob,off):
 b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
 for i in range(0,len(b)-1,2): s=(s+(b[i]|b[i+1]<<8)); s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16); return (s+len(b))&0xffffffff
def ro(P,rva):
 for s in P['secs']:
  if s['va']<=rva<s['va']+max(s['vs'],s['rs']): return s['rp']+rva-s['va']
 raise ValueError(hex(rva))
def rt(base,off,disp): return base+off+4+disp

def main():
 if len(sys.argv)!=3: raise SystemExit('usage: validate_sharehr1_rc8.py RC7 RC8')
 B=open(sys.argv[1],'rb').read(); N=open(sys.argv[2],'rb').read(); C=[]
 def ck(n,v,d=''):
  C.append(n)
  if not v: raise AssertionError(n+(' :: '+d if d else ''))
 pb,pn=pe(B),pe(N)
 ck('RC7 hash',sha(B)==BASE,sha(B)); ck('RC8 hash',sha(N)==NEW,sha(N)); ck('same size',len(B)==len(N),f'{len(B)}/{len(N)}'); ck('same sections',pb['n']==pn['n']==8); ck('image base',pb['ib']==pn['ib']==IB)
 dg=next(s for s in pn['secs'] if s['name']=='.fgdia'); bg=next(s for s in pb['secs'] if s['name']=='.fgdia')
 ck('fgdia geometry unchanged',(dg['vs'],dg['va'],dg['rs'],dg['rp'],dg['ch'])==(bg['vs'],bg['va'],bg['rs'],bg['rp'],bg['ch']))
 code=N[dg['rp']:dg['rp']+dg['vs']]
 ck('raw HRESULT stub prefix',code[:2]==b'\x85\xc0' and code[2:4]==b'\x0f\x89' and code[8:10]==b'\x89\x05' and code[14]==0xE9,code[:19].hex())
 d=struct.unpack_from('<i',code,4)[0]; ck('success target',rt(IB+DIAG,4,d)==IB+SUCCESS,hex(rt(IB+DIAG,4,d)))
 d=struct.unpack_from('<i',code,10)[0]; ck('LAST_STATUS target',rt(IB+DIAG,10,d)==IB+LAST,hex(rt(IB+DIAG,10,d)))
 d=struct.unpack_from('<i',code,15)[0]; ck('failure target',rt(IB+DIAG,15,d)==IB+FAIL,hex(rt(IB+DIAG,15,d)))
 ck('unused old diagnostic body trapped',set(code[19:])<= {0xCC})
 # Existing call-site remains the exact JMP to .fgdia from RC7.
 go=ro(pn,GATE); ck('call-site unchanged',N[go:go+8]==B[ro(pb,GATE):ro(pb,GATE)+8]); disp=struct.unpack_from('<i',N,go+1)[0]; ck('call-site still targets fgdia',IB+GATE+5+disp==IB+DIAG)
 # Governor sections must be byte-identical.
 for name in ('.fgov','.fgdat'):
  sb=next(s for s in pb['secs'] if s['name']==name); sn=next(s for s in pn['secs'] if s['name']==name); ck(name+' geometry unchanged',(sb['vs'],sb['va'],sb['rs'],sb['rp'],sb['ch'])==(sn['vs'],sn['va'],sn['rs'],sn['rp'],sn['ch'])); ck(name+' bytes unchanged',B[sb['rp']:sb['rp']+sb['rs']]==N[sn['rp']:sn['rp']+sn['rs']])
 ck('checksum valid',pn['cks']==cks(N,pn['cks_off']),f"{pn['cks']:#x}/{cks(N,pn['cks_off']):#x}")
 # Only checksum + .fgdia bytes may differ.
 allowed=set(range(pn['cks_off'],pn['cks_off']+4))|set(range(dg['rp'],dg['rp']+dg['vs']))
 bad=[i for i,(x,y) in enumerate(zip(B,N)) if x!=y and i not in allowed]
 ck('no unexpected binary changes',not bad,','.join(hex(x) for x in bad[:16]))
 print(f'SHAREHR1 STATIC VALIDATION: PASS {len(C)}/{len(C)}')
 for n in C: print('[PASS]',n)
if __name__=='__main__': main()

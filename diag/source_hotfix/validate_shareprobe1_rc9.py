#!/usr/bin/env python3
import hashlib,struct,sys
BASE='f26bbf5919ddeeb39937e5258d6c56f3196936e7d9a755099d55404693326d98'
NEW='9d84bb1978a8463df2498455b180125f9aa6a9a2f6ee390cab1382cec44f1d59'
IB=0x180000000; DIAG=0x34FD000; SUCCESS=0x221EE; FAIL=0x222B3; LAST=0x2CCE64C; GAME=0x4B118; GATE=0x221E6
TABLE=[(0x28,0x000,0x01),(0x28,0x002,0x02),(0x28,0x100,0x04),(0x08,0x100,0x08),(0x20,0x100,0x10),(0x28,0x800,0x20),(0x28,0x900,0x40)]
def sha(b): return hashlib.sha256(b).hexdigest()
def parse(b):
 e=struct.unpack_from('<I',b,0x3c)[0]; coff=e+4; n=struct.unpack_from('<H',b,coff+2)[0]; osz=struct.unpack_from('<H',b,coff+16)[0]; opt=coff+20; sh=opt+osz
 secs=[]
 for i in range(n):
  o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode(); vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8); secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
 return dict(n=n,opt=opt,sh=sh,secs=secs,ib=struct.unpack_from('<Q',b,opt+24)[0],sizeimg=struct.unpack_from('<I',b,opt+56)[0],cks_off=opt+64,cks=struct.unpack_from('<I',b,opt+64)[0])
def cks(blob,off):
 b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
 for i in range(0,len(b)-1,2): s+=b[i]|(b[i+1]<<8); s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16); return (s+len(b))&0xffffffff
def ro(P,rva):
 for s in P['secs']:
  if s['va']<=rva<s['va']+max(s['vs'],s['rs']): return s['rp']+rva-s['va']
 raise ValueError(hex(rva))
def rt(base,off,disp): return base+off+4+disp

def main():
 if len(sys.argv)!=3: raise SystemExit('usage: validate_shareprobe1_rc9.py RC8 RC9')
 B=open(sys.argv[1],'rb').read(); N=open(sys.argv[2],'rb').read(); pb,pn=parse(B),parse(N); C=[]
 def ck(name,ok,detail=''):
  C.append(name)
  if not ok: raise AssertionError(name+(' :: '+detail if detail else ''))
 ck('RC8 hash',sha(B)==BASE,sha(B)); ck('RC9 hash',sha(N)==NEW,sha(N)); ck('same file size',len(B)==len(N),f'{len(B)}/{len(N)}'); ck('PE x64 image base',pb['ib']==pn['ib']==IB); ck('same 8 sections',pb['n']==pn['n']==8); ck('SizeOfImage unchanged',pb['sizeimg']==pn['sizeimg'])
 db=next(s for s in pb['secs'] if s['name']=='.fgdia'); dn=next(s for s in pn['secs'] if s['name']=='.fgdia')
 ck('fgdia RVA unchanged',db['va']==dn['va']==DIAG); ck('fgdia raw position unchanged',db['rp']==dn['rp']); ck('fgdia raw capacity unchanged',db['rs']==dn['rs']==0x200); ck('fgdia characteristics unchanged',db['ch']==dn['ch']==0x60000020); ck('fgdia virtual size expanded only within raw capacity',db['vs']==0xB0 and dn['vs']==0x168 and dn['vs']<=dn['rs'])
 code=N[dn['rp']:dn['rp']+dn['vs']]
 ck('gate starts test eax',code[0:2]==b'\x85\xc0'); ck('raw E_INVALIDARG compare present',b'\x3d\x57\x00\x07\x80' in code); ck('probe status marker present',b'\x0d\x00\x00\x00\xe3' in code); ck('stack allocation present',b'\x48\x83\xec\x70' in code); ck('stack restoration present',b'\x48\x83\xc4\x70' in code); ck('CreateTexture2D vtable slot used',b'\xff\x50\x28' in code); ck('IUnknown Release used',b'\xff\x50\x10' in code)
 table=b''.join(struct.pack('<III',*x) for x in TABLE); pos=code.find(table); ck('seven-probe table exact',pos>=0,hex(pos)); ck('probe table is inside code body',pos+len(table)<=len(code)); ck('unused raw tail trapped',set(N[dn['rp']+dn['vs']:dn['rp']+dn['rs']])<={0xCC})
 # Relocation target patterns after link.
 # First jns rel32 at offset 2 -> success.
 d=struct.unpack_from('<i',code,4)[0]; ck('success target exact',rt(IB+DIAG,4,d)==IB+SUCCESS,hex(rt(IB+DIAG,4,d)))
 # Non-invalid failure: LAST at reloc field offset 0x11, failure jump field 0x16.
 d=struct.unpack_from('<i',code,0x11)[0]; ck('raw LAST_STATUS target exact',rt(IB+DIAG,0x11,d)==IB+LAST,hex(rt(IB+DIAG,0x11,d)))
 d=struct.unpack_from('<i',code,0x16)[0]; ck('raw failure target exact',rt(IB+DIAG,0x16,d)==IB+FAIL,hex(rt(IB+DIAG,0x16,d)))
 # GAME_DEVICE RIP displacement field at 0xAF.
 d=struct.unpack_from('<i',code,0xAF)[0]; ck('GAME_DEVICE target exact',rt(IB+DIAG,0xAF,d)==IB+GAME,hex(rt(IB+DIAG,0xAF,d)))
 # Probe LAST and failure at 0x105 / 0x10E.
 d=struct.unpack_from('<i',code,0x105)[0]; ck('probe LAST_STATUS target exact',rt(IB+DIAG,0x105,d)==IB+LAST,hex(rt(IB+DIAG,0x105,d)))
 d=struct.unpack_from('<i',code,0x10E)[0]; ck('probe failure target exact',rt(IB+DIAG,0x10E,d)==IB+FAIL,hex(rt(IB+DIAG,0x10E,d)))
 go=ro(pn,GATE); gob=ro(pb,GATE); ck('existing call-site unchanged',N[go:go+8]==B[gob:gob+8]); disp=struct.unpack_from('<i',N,go+1)[0]; ck('call-site still targets fgdia',IB+GATE+5+disp==IB+DIAG)
 for name in ('.fgov','.fgdat'):
  sb=next(s for s in pb['secs'] if s['name']==name); sn=next(s for s in pn['secs'] if s['name']==name); ck(name+' geometry unchanged',(sb['vs'],sb['va'],sb['rs'],sb['rp'],sb['ch'])==(sn['vs'],sn['va'],sn['rs'],sn['rp'],sn['ch'])); ck(name+' bytes unchanged',B[sb['rp']:sb['rp']+sb['rs']]==N[sn['rp']:sn['rp']+sn['rs']])
 ck('PE checksum valid',pn['cks']==cks(N,pn['cks_off']),f"{pn['cks']:#x}/{cks(N,pn['cks_off']):#x}")
 allowed=set(range(pn['cks_off'],pn['cks_off']+4))|set(range(dn['rp'],dn['rp']+dn['rs']))|set(range(dn['off']+8,dn['off']+12))
 bad=[i for i,(x,y) in enumerate(zip(B,N)) if x!=y and i not in allowed]; ck('no unexpected binary changes',not bad,','.join(hex(x) for x in bad[:16]))
 print(f'SHAREPROBE1 STATIC VALIDATION: PASS {len(C)}/{len(C)}')
 for n in C: print('[PASS]',n)
if __name__=='__main__': main()

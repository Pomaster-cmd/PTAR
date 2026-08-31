#!/usr/bin/env python3
import hashlib,struct,sys
from pathlib import Path
RC14_SHA='3808fcb1681f00342b7ce07e39aebc2b1cabbed8aeb2fd244788744136d1868f'
RC15_SHA='2c978a59cc7053edc6669de18e0dcf2ae5755b9b96da74c452c9b19405c1d785'
RC16_SHA='f5873962a1add8e5a18dd2f0ee628d01700fb63a269dfe04016ef7104b83820b'
IMAGE_BASE=0x180000000
checks=[]
def ok(n,c): checks.append((n,bool(c))); print(('[PASS] ' if c else '[FAIL] ')+n)
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def pe(b):
 e=struct.unpack_from('<I',b,0x3c)[0]; co=e+4; machine,n,_,_,_,osz,_=struct.unpack_from('<HHIIIHH',b,co); opt=co+20; sh=opt+osz
 ss=[]
 for i in range(n):
  o=sh+i*40; nm=b[o:o+8].rstrip(b'\0').decode(); vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8); ss.append((nm,vs,va,rs,rp,ch,o))
 return e,machine,n,opt,ss
def roff(ss,r):
 for nm,vs,va,rs,rp,ch,o in ss:
  if va<=r<va+max(vs,rs): return rp+r-va
 raise Exception(hex(r))
def call_target(b,ss,r):
 o=roff(ss,r); okb=b[o]==0xE8
 if not okb:return None
 return r+5+struct.unpack_from('<i',b,o+1)[0]
def checksum(blob,off):
 b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
 for i in range(0,len(b)-1,2): s+=b[i]|b[i+1]<<8; s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16);return(s+len(b))&0xffffffff

def main(rc14,rc15,rc16,obj):
 b14=Path(rc14).read_bytes();b15=Path(rc15).read_bytes();b16=Path(rc16).read_bytes()
 ok('RC14 hash exact',hashlib.sha256(b14).hexdigest()==RC14_SHA)
 ok('RC15 rejected hash exact',hashlib.sha256(b15).hexdigest()==RC15_SHA)
 ok('RC16 hash exact',hashlib.sha256(b16).hexdigest()==RC16_SHA)
 e,m,n,opt,s14=pe(b14); e15,m15,n15,opt15,s15=pe(b15); e16,m16,n16,opt16,s16=pe(b16)
 ok('AMD64 PE preserved',m==m15==m16==0x8664)
 ok('section count 8 preserved',n==n15==n16==8)
 ok('image base preserved',struct.unpack_from('<Q',b16,opt16+24)[0]==IMAGE_BASE)
 names14=[x[0] for x in s14]; names16=[x[0] for x in s16]
 ok('section names preserved',names14==names16==['.text','.rdata','.data','.pdata','.reloc','.fgov','.fgdat','.fgdia'])
 # Exact RC14 startup parser preservation, and contrast RC15.
 psite=0x2B46; o14=roff(s14,psite);o15=roff(s15,psite);o16=roff(s16,psite)
 ok('RC14 parser target expected',call_target(b14,s14,psite)==0x34FD200)
 ok('RC15 parser was redirected',call_target(b15,s15,psite)==0x34FD600)
 ok('RC16 parser target restored to RC14',call_target(b16,s16,psite)==0x34FD200)
 ok('RC16 parser 22 bytes byte-identical RC14',b16[o16:o16+22]==b14[o14:o14+22])
 # New runtime-only hooks.
 ok('RC16 ME width wrapper target',call_target(b16,s16,0x220BD)==0x34FD600)
 ok('RC16 CTRL+F8 wrapper target',call_target(b16,s16,0x12225)==0x34FD61A)
 ok('RC16 F8 status wrapper target',call_target(b16,s16,0x12271)==0x34FD67C)
 # RC14 hooks not meant to change.
 for site,label in [(0x2C5D,'quality guard'),(0x220CC,'ME height')]:
  a=roff(s14,site);c=roff(s16,site)
  ok(f'{label} hook unchanged',b14[a:a+16]==b16[c:c+16])
 # Sections exact preservation.
 def secmap(ss): return {x[0]:x for x in ss}
 d14=secmap(s14);d16=secmap(s16)
 for nm in ['.fgov']:
  _,vs,va,rs,rp,_,_=d14[nm]; _,vs2,va2,rs2,rp2,_,_=d16[nm]
  ok(f'{nm} geometry unchanged',(vs,va,rs,rp)==(vs2,va2,rs2,rp2))
  ok(f'{nm} bytes unchanged',b14[rp:rp+rs]==b16[rp2:rp2+rs2])
 # fgdia old raw prefix and fgdat prefix.
 _,_,_,rs14,rp14,_,_=d14['.fgdia']; _,vs16,_,rs16,rp16,_,_=d16['.fgdia']
 ok('RC16 .fgdia first 0x600 bytes RC14-identical',b16[rp16:rp16+0x600]==b14[rp14:rp14+0x600])
 ok('RC16 .fgdia expanded within same page',vs16<0x1000 and rs16==0xC00)
 _,_,_,_,dp14,_,_=d14['.fgdat']; _,dvs16,_,drs16,dp16,_,_=d16['.fgdat']
 ok('RC16 .fgdat first 0xF0 bytes RC14-identical',b16[dp16:dp16+0xF0]==b14[dp14:dp14+0xF0])
 ok('ACTIVE profile dword zero seed',b16[dp16+0xF0:dp16+0xF4]==b'\0\0\0\0')
 ok('fgdat virtual size F4',dvs16==0xF4)
 # Text diffs only at three intended sites.
 _,tvs14,_,trs14,trp14,_,_=d14['.text']; _,tvs16,_,trs16,trp16,_,_=d16['.text']
 t14=b14[trp14:trp14+trs14];t16=b16[trp16:trp16+trs16]
 dif=[i for i,(x,y) in enumerate(zip(t14,t16)) if x!=y]
 allowed=set()
 for r,l in [(0x220BD,15),(0x12225,7),(0x12271,5)]:
  off=roff(s14,r)-trp14; allowed.update(range(off,off+l))
 ok('all .text diffs bounded to 3 runtime hooks',set(dif)<=allowed and len(dif)>0)
 ok('startup parser absent from .text diff',not any((o14-trp14)<=i<(o14-trp14+22) for i in dif))
 # HLSL source same length and only intended same-length edits within rdata area.
 hs=0x34960; he14=b14.find(b'\0',hs); he16=b16.find(b'\0',hs)
 ok('feedback HLSL length 6632 both',he14-hs==6632 and he16-hs==6632)
 hdiff=[i for i,(x,y) in enumerate(zip(b14[hs:he14],b16[hs:he16])) if x!=y]
 ok('feedback HLSL changed but bounded',0<len(hdiff)<64)
 # Runtime size and checksum.
 ok('RC16 file size expected',len(b16)==0x4BE00)
 stored=struct.unpack_from('<I',b16,opt16+64)[0]
 ok('PE checksum valid',stored==checksum(b16,opt16+64))
 ok('HUD2 object hash exact',sha(obj)=='c50d3949d0f6fa2b8392f47ad038e1e2bc43d894f666bae4f27512d35ebf7ec2')
 # String contracts present.
 for s in [b'P1FG7N QUALITY ACTIVE 3 CONSERVATIVE',b'P1FG7N QUALITY SELECTED 3 CONSERVATIVE',b'P1U46 HOTKEY FG QUALITY = CTRL+F8',b'RESTART PENDING YES']:
  ok('string present '+s[:35].decode('ascii','ignore'),s in b16)
 passed=sum(c for _,c in checks); total=len(checks)
 print(f'FGQUALITYHUD2 RC16 STATIC VALIDATION: {"PASS" if passed==total else "FAIL"} {passed}/{total}')
 if passed!=total: raise SystemExit(1)
if __name__=='__main__':
 if len(sys.argv)!=5: raise SystemExit('usage: validate_fgqualityhud2_rc16.py RC14 RC15 RC16 OBJ')
 main(*sys.argv[1:])

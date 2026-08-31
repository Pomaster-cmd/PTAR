#!/usr/bin/env python3
from pathlib import Path
import hashlib,struct,re,subprocess,sys
BASE=Path(sys.argv[1]) if len(sys.argv)>1 else Path('/mnt/data/ptar_rc14_build/win81_nis_dx11_x64.dll')
CUR=Path(sys.argv[2]) if len(sys.argv)>2 else Path('/mnt/data/ptar_rc15_work/rc15_a.dll')
BASE_SHA='3808fcb1681f00342b7ce07e39aebc2b1cabbed8aeb2fd244788744136d1868f'
CUR_SHA='2c978a59cc7053edc6669de18e0dcf2ae5755b9b96da74c452c9b19405c1d785'
EXPECTED_EXPORTS={'D3D11CoreCreateDevice','D3D11CoreCreateLayeredDevice','D3D11CoreGetLayeredDeviceSize','D3D11CoreRegisterLayers','D3D11CreateDevice','D3D11CreateDeviceAndSwapChain','D3D11On12CreateDevice'}
checks=[]
def ck(n,c,d=''):
 if not c: raise AssertionError(n+(' :: '+d if d else ''))
 checks.append(n)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def pe(b):
 e=struct.unpack_from('<I',b,0x3c)[0];co=e+4;machine,n,_,_,_,osz,_=struct.unpack_from('<HHIIIHH',b,co);opt=co+20;sh=opt+osz
 secs={}
 for i in range(n):
  o=sh+i*40;nm=b[o:o+8].rstrip(b'\0').decode();vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);secs[nm]=(o,vs,va,rs,rp)
 return {'e':e,'machine':machine,'n':n,'opt':opt,'magic':struct.unpack_from('<H',b,opt)[0],'ib':struct.unpack_from('<Q',b,opt+24)[0],'sizeimg':struct.unpack_from('<I',b,opt+56)[0],'cks':struct.unpack_from('<I',b,opt+64)[0],'sub':struct.unpack_from('<H',b,opt+68)[0],'subver':struct.unpack_from('<HH',b,opt+48),'secs':secs}
def off(b,rva):
 pi=pe(b)
 for nm,(o,vs,va,rs,rp) in pi['secs'].items():
  if va<=rva<va+max(vs,rs): return rp+rva-va
 raise ValueError(hex(rva))
def callt(b,rva):
 o=off(b,rva);ck('call opcode '+hex(rva),b[o]==0xE8,b[o:o+8].hex());return rva+5+struct.unpack_from('<i',b,o+1)[0]
def checksum(blob,coff):
 b=bytearray(blob);struct.pack_into('<I',b,coff,0);s=0
 for i in range(0,len(b)-1,2):s+=b[i]|b[i+1]<<8;s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16);return (s+len(b))&0xffffffff
B=BASE.read_bytes();C=CUR.read_bytes();pb=pe(B);pc=pe(C)
ck('RC14 base exact',sha(BASE)==BASE_SHA,sha(BASE));ck('RC15 runtime exact',sha(CUR)==CUR_SHA,sha(CUR))
ck('AMD64 PE32+',pc['machine']==0x8664 and pc['magic']==0x20b);ck('image base unchanged',pc['ib']==pb['ib']==0x180000000);ck('section count unchanged',pc['n']==pb['n']==8);ck('GUI subsystem unchanged',pc['sub']==pb['sub']==2);ck('SizeOfImage unchanged',pc['sizeimg']==pb['sizeimg'])
ck('PE checksum recomputes',checksum(C,pc['opt']+64)==pc['cks'],hex(pc['cks']))
for nm in ['.text','.rdata','.data','.pdata','.reloc','.fgov']:
 ck(nm+' geometry unchanged',pc['secs'][nm][1:]==pb['secs'][nm][1:],repr((pc['secs'][nm],pb['secs'][nm])))
ck('.fgdat geometry',pc['secs']['.fgdat'][1:]==(0xF4,0x34FC000,0x200,0x4B000),repr(pc['secs']['.fgdat']))
ck('.fgdia geometry',pc['secs']['.fgdia'][1:]==(0x954,0x34FD000,0xC00,0x4B200),repr(pc['secs']['.fgdia']))
ck('no overlay',len(C)==0x4B200+0xC00,str(len(C)))
# Entire unchanged sections except rdata text patch.
for nm in ['.data','.pdata','.reloc','.fgov']:
 rp=pc['secs'][nm][4];rs=pc['secs'][nm][3];rb=pb['secs'][nm][4];rsb=pb['secs'][nm][3]
 ck(nm+' bytes byte-identical',C[rp:rp+rs]==B[rb:rb+rsb])
# .text only the four call-site ranges differ.
tb=bytearray(B[pb['secs']['.text'][4]:pb['secs']['.text'][4]+pb['secs']['.text'][3]])
tc=bytearray(C[pc['secs']['.text'][4]:pc['secs']['.text'][4]+pc['secs']['.text'][3]])
for rva,n in [(0x2B46,22),(0x220BD,15),(0x12225,7),(0x12271,5)]:
 baseva=pb['secs']['.text'][2];i=rva-baseva;tb[i:i+n]=tc[i:i+n]
ck('.text differs only four wrapper hooks',tb==tc)
# Frozen custom sections.
drb=pb['secs']['.fgdat'][4];drc=pc['secs']['.fgdat'][4]
ck('.fgdat previous bytes 0..EF identical',C[drc:drc+0xF0]==B[drb:drb+0xF0]);ck('new active profile seed zero',C[drc+0xF0:drc+0xF4]==b'\0'*4)
frb=pb['secs']['.fgdia'][4];frc=pc['secs']['.fgdia'][4]
ck('RC14 .fgdia first 0x600 byte-identical',C[frc:frc+0x600]==B[frb:frb+0x600]);ck('new .fgdia tail nonempty',any(x!=0xCC for x in C[frc+0x600:frc+0x954]))
# Existing GATE1 and legacy transport are thereby included in byte-identical sections.
# Hook topology.
for site,target in [(0x2B46,0x34FD600),(0x220BD,0x34FD61F),(0x12225,0x34FD643),(0x12271,0x34FD6A5),(0x2C5D,0x34FD24D),(0x220CC,0x34FD2CC)]:
 ck('hook target '+hex(site),callt(C,site)==target,hex(callt(C,site)))
# New runtime strings.
for s in [b'P1U46 HOTKEY FG QUALITY = CTRL+F8',b'P1FG7N QUALITY ACTIVE 0 LEGACY',b'P1FG7N QUALITY ACTIVE 1 BALANCED',b'P1FG7N QUALITY ACTIVE 2 QUALITY',b'P1FG7N QUALITY ACTIVE 3 CONSERVATIVE',b'P1FG7N QUALITY SELECTED 0 LEGACY',b'P1FG7N QUALITY SELECTED 3 CONSERVATIVE',b'P1FG7N QUALITY ME TIER DIV3',b'P1FG7N QUALITY ME TIER DIV2',b'P1FG7N QUALITY RESTART PENDING NO',b'P1FG7N QUALITY RESTART PENDING YES - CTRL+F6 OFF/ON TO APPLY ME TIER']:
 ck('runtime string '+s.decode(),C.count(s)==1,str(C.count(s)))
# New hotkey wrapper must emit notice id 8 and 3000 ms via existing notice function.
q=C[off(C,0x34FD643):off(C,0x34FD6A5)]
ck('notice id 8 immediate',bytes.fromhex('b9 08 00 00 00') in q);ck('notice 3000ms immediate',bytes.fromhex('c7 44 24 20 b8 0b 00 00') in q);ck('notice function call target',callt(C,0x34FD691)==0x16270,hex(callt(C,0x34FD691)))
# Feedback HLSL same allocation, exact semantic glyph substitutions only.
hs=0x34960;eb=B.find(b'\0',hs);ec=C.find(b'\0',hs);ck('feedback source length 6632 unchanged',eb-hs==ec-hs==6632,repr((eb-hs,ec-hs)))
old=B[hs:eb];new=C[hs:ec]
patches={
 b'm=max(m,Cx(p,o,0,84));m=max(m,Cx(p,o,1,69));m=max(m,Cx(p,o,2,83));m=max(m,Cx(p,o,3,84));':b'm=max(m,Cx(p,o,0,67));m=max(m,Cx(p,o,1,85));m=max(m,Cx(p,o,2,82));m=max(m,Cx(p,o,3,82));',
 b'm=max(m,Cx(p,o,7,79));m=max(m,Cx(p,o,8,70));m=max(m,Cx(p,o,9,52));':b'm=max(m,Cx(p,o,7,78));m=max(m,Cx(p,o,8,79));m=max(m,Cx(p,o,9,87));',
 b'm=max(m,Cx(p,o,0,82));m=max(m,Cx(p,o,1,85));m=max(m,Cx(p,o,2,78));':b'm=max(m,Cx(p,o,0,83));m=max(m,Cx(p,o,1,69));m=max(m,Cx(p,o,2,76));',
 b'm=max(m,Cx(p,o,8,88));':b'm=max(m,Cx(p,o,8,80));'}
exp=old
for a,z in patches.items():ck('old HLSL pattern unique '+a.decode(),old.count(a)==1);exp=exp.replace(a,z,1);ck('new HLSL glyph pattern '+z.decode(),new.count(z)==1)
ck('feedback HLSL exact expected diff only',new==exp)
# .rdata differs only HLSL substitutions.
rb=pb['secs']['.rdata'][4];rr=pb['secs']['.rdata'][3];rc=pc['secs']['.rdata'][4]
rbase=bytearray(B[rb:rb+rr]);rcur=bytearray(C[rc:rc+rr]);i=hs-rb;rbase[i:i+6632]=rcur[i:i+6632];ck('.rdata differs only feedback HLSL glyph constants',rbase==rcur)
# ABI unchanged.
objdump='/usr/local/swift/usr/bin/llvm-objdump'
out=subprocess.check_output([objdump,'-p',str(CUR)],text=True,errors='replace');dlls=re.findall(r'DLL Name:\s*(\S+)',out);ck('imports only KERNEL32 USER32',dlls==['KERNEL32.dll','USER32.dll'],repr(dlls));ex=set(re.findall(r'\b(D3D11(?:Core|On12|Create)[A-Za-z0-9_]*)\s*$',out,re.M));ck('seven D3D11 exports unchanged',ex==EXPECTED_EXPORTS,repr(sorted(ex)))
print('RC15 FGQUALITYHUD1 BINARY VALIDATION: PASS %d/%d'%(len(checks),len(checks)))
for n in checks:print('[PASS]',n)

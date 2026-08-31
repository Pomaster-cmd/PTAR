#!/usr/bin/env python3
import hashlib,struct,sys,re
from pathlib import Path
RC16='f5873962a1add8e5a18dd2f0ee628d01700fb63a269dfe04016ef7104b83820b'
RC17='5bdff6f5ecce928cb62f5dbbfe0cd3562eff455054620223cc4ba268c12c92fc'
OBJ='2e0b377631a25b716a811036171af64941efb05e1dd580195224cac3e733049c'
FGSRC='f62f1a91d1e7d2528126704d73cf83437d15ef30cf03edb6b0d2e33d412537f3'
HUDSRC='6d0cf734ade85c7444f1388daa93cbcbfa4258be785bc58feb85a3be9f0e1645'
IMAGE_BASE=0x180000000
FG_OFF=0x34210;FG_LEN=1197;HUD_OFF=0x34960;HUD_LEN=6632
checks=[]
def ck(n,c): checks.append((n,bool(c))); print(('[PASS] ' if c else '[FAIL] ')+n)
def sha_bytes(b):return hashlib.sha256(b).hexdigest()
def sha_path(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def pe(b):
 e=struct.unpack_from('<I',b,0x3c)[0];co=e+4;machine,n,_,_,_,osz,_=struct.unpack_from('<HHIIIHH',b,co);opt=co+20;sh=opt+osz
 ss=[]
 for i in range(n):
  o=sh+i*40;nm=b[o:o+8].rstrip(b'\0').decode();vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8);ss.append((nm,vs,va,rs,rp,ch,o))
 return e,machine,n,opt,ss
def sm(ss):return {x[0]:x for x in ss}
def roff(ss,r):
 for nm,vs,va,rs,rp,ch,o in ss:
  if va<=r<va+max(vs,rs):return rp+r-va
 raise RuntimeError(hex(r))
def call_target(b,ss,r):
 o=roff(ss,r)
 if b[o]!=0xe8:return None
 return r+5+struct.unpack_from('<i',b,o+1)[0]
def checksum(blob,off):
 b=bytearray(blob);struct.pack_into('<I',b,off,0);s=0
 for i in range(0,len(b)-1,2):s+=b[i]|b[i+1]<<8;s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16);return(s+len(b))&0xffffffff

def rip_target(code_rva,off,disp_off,insn_len):
 d=struct.unpack_from('<i',code,off+disp_off)[0]
 return code_rva+off+insn_len+d

def main(rc16,rc17,obj,fgsrc,hudsrc):
 global code
 b16=Path(rc16).read_bytes();b17=Path(rc17).read_bytes()
 ck('RC16 exact base hash',sha_bytes(b16)==RC16)
 ck('RC17 exact runtime hash',sha_bytes(b17)==RC17)
 ck('RC17 differs from RC16',b17!=b16)
 e16,m16,n16,opt16,s16=pe(b16);e17,m17,n17,opt17,s17=pe(b17)
 ck('AMD64 PE preserved',m16==m17==0x8664)
 ck('PE32+ preserved',struct.unpack_from('<H',b17,opt17)[0]==0x20b)
 ck('image base preserved',struct.unpack_from('<Q',b17,opt17+24)[0]==IMAGE_BASE)
 ck('section count 8',n16==n17==8)
 ck('section names unchanged',[x[0] for x in s16]==[x[0] for x in s17]==['.text','.rdata','.data','.pdata','.reloc','.fgov','.fgdat','.fgdia'])
 d16=sm(s16);d17=sm(s17)
 ck('file size RC16 expected',len(b16)==0x4BE00)
 ck('file size RC17 expected',len(b17)==0x4C200)
 ck('SizeOfImage unchanged',struct.unpack_from('<I',b16,opt16+56)[0]==struct.unpack_from('<I',b17,opt17+56)[0]==0x34FE000)
 ck('fgdia RC16 geometry',(d16['.fgdia'][1],d16['.fgdia'][2],d16['.fgdia'][3],d16['.fgdia'][4])==(0x940,0x34FD000,0xC00,0x4B200))
 ck('fgdia RC17 raw expanded one aligned block',(d17['.fgdia'][2],d17['.fgdia'][3],d17['.fgdia'][4])==(0x34FD000,0x1000,0x4B200))
 ck('fgdia RC17 virtual size bounded',0xD00<d17['.fgdia'][1]<0xE00)
 ck('fgdia first 0xC00 bytes byte-identical RC16',b17[0x4B200:0x4BE00]==b16[0x4B200:0x4BE00])
 ck('fgov geometry unchanged',d16['.fgov'][1:5]==d17['.fgov'][1:5])
 ck('fgov/GATE1 bytes byte-identical',b16[d16['.fgov'][4]:d16['.fgov'][4]+d16['.fgov'][3]]==b17[d17['.fgov'][4]:d17['.fgov'][4]+d17['.fgov'][3]])
 ck('fgdat geometry unchanged',d16['.fgdat'][1:5]==d17['.fgdat'][1:5])
 ck('fgdat state bytes unchanged',b16[d16['.fgdat'][4]:d16['.fgdat'][4]+d16['.fgdat'][3]]==b17[d17['.fgdat'][4]:d17['.fgdat'][4]+d17['.fgdat'][3]])
 # Startup and critical RC16 hooks.
 ck('startup parser target RC14/RC16 path retained',call_target(b17,s17,0x2B46)==0x34FD200)
 o16=roff(s16,0x2B46);o17=roff(s17,0x2B46)
 ck('startup parser 22 bytes byte-identical RC16',b16[o16:o16+22]==b17[o17:o17+22])
 ck('ME width HUD2 wrapper unchanged',call_target(b17,s17,0x220BD)==0x34FD600)
 ck('F8 HUD2 status wrapper unchanged',call_target(b17,s17,0x12271)==0x34FD67C)
 ck('CTRL+F8 now targets QUALITYMENU1',call_target(b17,s17,0x12225)==0x34FDC00)
 # Text section changes only call displacement at CTRL+F8 hook.
 t16=d16['.text'];t17=d17['.text'];a=b16[t16[4]:t16[4]+t16[3]];z=b17[t17[4]:t17[4]+t17[3]]
 dif=[i for i,(x,y) in enumerate(zip(a,z)) if x!=y]
 hs=roff(s16,0x12225)-t16[4]
 ck('text diffs bounded to CTRL+F8 call',set(dif)<=set(range(hs,hs+7)) and len(dif)>0)
 # Rdata differences only the two embedded HLSL slots.
 r16=d16['.rdata'];r17=d17['.rdata'];a=b16[r16[4]:r16[4]+r16[3]];z=b17[r17[4]:r17[4]+r17[3]]
 rd=[i for i,(x,y) in enumerate(zip(a,z)) if x!=y]
 allowed=set(range(FG_OFF-r16[4],FG_OFF-r16[4]+FG_LEN))|set(range(HUD_OFF-r16[4],HUD_OFF-r16[4]+HUD_LEN))
 ck('rdata diffs bounded to FG+quality-HUD HLSL slots',set(rd)<=allowed and len(rd)>0)
 # FG shader contract.
 fg17=b17[FG_OFF:FG_OFF+FG_LEN].rstrip(b' ')
 fg16=b16[FG_OFF:FG_OFF+FG_LEN]
 ck('RC16 FG source historical length',b16.find(b'\0',FG_OFF)-FG_OFF==FG_LEN)
 ck('RC17 FG source slot still NUL terminated at same offset',b17[FG_OFF+FG_LEN]==0)
 ck('FG source file hash exact',sha_path(fgsrc)==FGSRC)
 srcfg=Path(fgsrc).read_bytes()
 ck('embedded FG source equals packaged source plus space pad',fg17==srcfg)
 ck('Q3-only discontinuity branch present',b'if(G<.3)' in fg17)
 ck('motion-neighbor +X probe present',b'V(c+int2(1,0),n)-v' in fg17)
 ck('motion-neighbor +Y probe present',b'V(c+int2(0,1),n)-v' in fg17)
 ck('confidence squared stabilization present',b'k*=k' in fg17)
 ck('photometric trust fallback retained',b'saturate(1-d/max(G,.02))*k' in fg17)
 ck('bidirectional warp retained',b'p+v*(1-H)' in fg17 and b'p-v*H' in fg17)
 ck('truth blend fallback retained',b'lerp(P.Load' in fg17 and b'C.Load' in fg17)
 ck('Q0-Q2 gate threshold excludes Guard35+',b'if(G<.3)' in fg17)
 # HUD contract.
 ck('HUD slot same NUL endpoint',b17.find(b'\0',HUD_OFF)-HUD_OFF==HUD_LEN)
 ck('HUD source file hash exact',sha_path(hudsrc)==HUDSRC)
 hudbytes=bytes(b17[HUD_OFF:HUD_OFF+HUD_LEN]);hud=hudbytes.decode('ascii')
 ck('embedded HUD source equals packaged source plus space pad',hudbytes.rstrip(b' ')==Path(hudsrc).read_bytes())
 ck('HUD G glyph added','if(c==71)return 27470' in hud)
 ck('HUD Q glyph added','if(c==81)return 20335' in hud)
 ck('HUD packed LEGACY present','0x4147454c' in hud and '0x5943' in hud)
 ck('HUD packed BALANCED present','0x414c4142' in hud and '0x4445434e' in hud)
 ck('HUD packed QUALITY present','0x4c415551' in hud and '0x595449' in hud)
 ck('HUD packed CONSERVATIVE present','0x534e4f43' in hud and '0x41565245' in hud and '0x45564954' in hud)
 ck('notice 8 renders name only line1','if(t==8)return Nm(p,a)' in hud)
 ck('notice 8 second line disabled','if(t<9||t>=14)return 0' in hud)
 ck('old CURR/NOW numeric type8 removed','if(t==8){m=max(m,Cx(p,o,7,78))' not in hud)
 ck('type8 DLine2 excluded before all numeric branches','float DLine2(float2 p,uint t,uint b,uint c){if(t<9||t>=14)return 0' in hud)
 # New machine code contract from relocated code.
 code_rva=0x34FDC00;coff=roff(s17,code_rva);code=bytes(b17[coff:coff+0x157])
 ck('new menu code size exact sentinel',code[-1:]==b'\xc3')
 ck('exact CTRL/F8/SHIFT/ALT virtual keys present',all(x in code for x in [b'\xb9\x11\x00\x00\x00',b'\xb9\x77\x00\x00\x00',b'\xb9\x10\x00\x00\x00',b'\xb9\x12\x00\x00\x00']))
 ck('notice type 8 comparison present',b'\x83\x3d' in code and b'\x08' in code)
 ck('3000ms notice duration present',b'\xb8\x0b\x00\x00' in code)
 ck('profile wrap mask 3 present',b'\x83\xe0\x03' in code)
 ck('QPC IAT address referenced',any((code_rva+i+6+struct.unpack_from('<i',code,i+2)[0])==0x49F18 for i in range(len(code)-6) if code[i:i+2]==b'\xff\x15'))
 ck('GetAsyncKeyState IAT address referenced',sum(1 for i in range(len(code)-6) if code[i:i+2]==b'\xff\x15' and code_rva+i+6+struct.unpack_from('<i',code,i+2)[0]==0x49FB8)==4)
 ck('notice function called',any(code_rva+i+5+struct.unpack_from('<i',code,i+1)[0]==0x16270 for i in range(len(code)-5) if code[i]==0xe8))
 ck('quality guard called',any(code_rva+i+5+struct.unpack_from('<i',code,i+1)[0]==0x34FD24D for i in range(len(code)-5) if code[i]==0xe8))
 ck('action0 hotkey function called',any(code_rva+i+5+struct.unpack_from('<i',code,i+1)[0]==0x167D0 for i in range(len(code)-5) if code[i]==0xe8))
 ck('menu object hash exact',sha_path(obj)==OBJ)
 # PE checksum.
 stored=struct.unpack_from('<I',b17,opt17+64)[0]
 ck('PE checksum valid',stored==checksum(b17,opt17+64))
 passed=sum(c for _,c in checks);total=len(checks)
 print(f'FGQUALITY3+QUALITYMENU1 RC17 STATIC VALIDATION: {"PASS" if passed==total else "FAIL"} {passed}/{total}')
 if passed!=total:sys.exit(1)
if __name__=='__main__':
 if len(sys.argv)!=6:raise SystemExit('usage: validate_fgquality3_menuhud1_rc17.py RC16 RC17 OBJ FG_HLSL HUD_HLSL')
 main(*sys.argv[1:])

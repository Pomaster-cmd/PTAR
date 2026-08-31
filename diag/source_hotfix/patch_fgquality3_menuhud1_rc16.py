#!/usr/bin/env python3
import hashlib,re,struct,sys
from pathlib import Path
BASE_SHA='f5873962a1add8e5a18dd2f0ee628d01700fb63a269dfe04016ef7104b83820b'
IMAGE_BASE=0x180000000
DIAG_RVA=0x34FD000
NEW_CODE_RVA=0x34FDC00
FG_SHADER_OFF=0x34210
FG_SHADER_LEN=1197
HUD_SHADER_OFF=0x34960
HUD_SHADER_LEN=6632
HOTKEY_SITE=0x12225
OLD_HOTKEY_TARGET=0x34FD61A
TARGETS={
 'QUALITY_PROFILE':0x34FC09C,
 'QUALITY_HOTKEY_LATCH':0x34FC0A0,
 'QUALITY_APPLIED_TIER':0x34FC0A4,
 'QUALITY_PENDING_ME':0x34FC0A8,
 'QUALITY_CHANGES':0x34FC0AC,
 'QUALITY_ACTIVE_PROFILE':0x34FC0F0,
 'FG_SCHEDULER_RUNNING':0x2C93B94,
 'GET_ASYNC_KEYSTATE_IAT':0x49FB8,
 'QPC_IAT':0x49F18,
 'NOTICE_TYPE':0x2C7E16C,
 'NOTICE_DEADLINE':0x2C7E170,
 'QUALITY_APPLY_GUARD':0x34FD24D,
 'HOTKEY_CHECK_FUNC':0x167D0,
 'NOTICE_FUNC':0x16270,
}
REQ=('quality_hotkey_menu1',)
def sha(b): return hashlib.sha256(b).hexdigest()
def pe(b):
 e=struct.unpack_from('<I',b,0x3c)[0];co=e+4;machine,n,_,_,_,osz,_=struct.unpack_from('<HHIIIHH',b,co);opt=co+20;sh=opt+osz
 ss=[]
 for i in range(n):
  o=sh+i*40;nm=b[o:o+8].rstrip(b'\0').decode();vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8);ss.append({'name':nm,'off':o,'vs':vs,'va':va,'rs':rs,'rp':rp,'ch':ch})
 return {'opt':opt,'ib':struct.unpack_from('<Q',b,opt+24)[0],'cks':opt+64,'sizeimg':opt+56,'secs':ss}
def roff(pi,r):
 for s in pi['secs']:
  if s['va']<=r<s['va']+max(s['vs'],s['rs']):return s['rp']+r-s['va']
 raise RuntimeError('RVA '+hex(r))
def ct(b,pi,r):
 o=roff(pi,r)
 if b[o]!=0xe8:raise RuntimeError('not call '+hex(r))
 return r+5+struct.unpack_from('<i',b,o+1)[0]
def mkcall(src,t,n): return b'\xe8'+struct.pack('<i',t-(src+5))+b'\x90'*(n-5)
def cks(blob,o):
 b=bytearray(blob);struct.pack_into('<I',b,o,0);s=0
 for i in range(0,len(b)-1,2):s+=b[i]|b[i+1]<<8;s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16);return(s+len(b))&0xffffffff
def objtext(path):
 b=Path(path).read_bytes();mach,n,_,sp,ns,osz,_=struct.unpack_from('<HHIIIHH',b,0)
 if mach!=0x8664 or osz:raise RuntimeError('COFF')
 sec=None;si=None
 for i in range(n):
  o=20+i*40;nm=b[o:o+8].rstrip(b'\0').decode();_,_,rs,rp,relp,_,nr,_,_=struct.unpack_from('<IIIIIIHHI',b,o+8)
  if nm=='.text':sec=(rs,rp,relp,nr);si=i+1
 st=sp+ns*18;sl=struct.unpack_from('<I',b,st)[0];stab=b[st:st+sl]
 def sn(i):
  o=sp+i*18;raw=b[o:o+8];z,x=struct.unpack('<II',raw)
  if z==0:
   e=stab.find(b'\0',x);return stab[x:e if e>=0 else len(stab)].decode()
  return raw.rstrip(b'\0').decode()
 sy={};i=0
 while i<ns:
  o=sp+i*18;name=sn(i);val,secno,typ,sc,na=struct.unpack_from('<IhHBB',b,o+8)
  if secno==si:sy[name]=val
  i+=1+na
 rs,rp,relp,nr=sec;code=bytearray(b[rp:rp+rs]);rels=[]
 for j in range(nr):
  o=relp+j*10;r,sidx,typ=struct.unpack_from('<IIH',b,o)
  if typ!=4:raise RuntimeError('unsupported reloc '+str(typ))
  rels.append((r,sn(sidx)))
 return code,rels,sy

def make_hud(src):
 s=src.decode('ascii')
 # Real 3x5 G and Q glyphs for LEGACY / QUALITY.
 a='if(c==70)return 4815u;if(c==72)return 23533u;'
 z='if(c==70)return 4815u;if(c==71)return 27470u;if(c==72)return 23533u;'
 if s.count(a)!=1: raise RuntimeError('HUD G insertion anchor')
 s=s.replace(a,z,1)
 a='if(c==80)return 4843u;if(c==82)return 23275u;'
 z='if(c==80)return 4843u;if(c==81)return 20335u;if(c==82)return 23275u;'
 if s.count(a)!=1: raise RuntimeError('HUD Q insertion anchor')
 s=s.replace(a,z,1)
 helper='float Nm(float2 p,uint a){uint x=a==0?0x4147454c:a==1?0x414c4142:a==2?0x4c415551:0x534e4f43,y=a==0?0x5943:a==1?0x4445434e:a==2?0x595449:0x41565245,z=a<3?0:0x45564954;float m=0;for(uint i=0;i<12;i++){uint w=i<4?x:i<8?y:z,c=(w>>(8*(i&3)))&255;m=max(m,Cx(p,float2(8,8),i,c));}return m;}'
 anchor='float DLine1(float2 p,uint t,uint a)'
 if s.count(anchor)!=1: raise RuntimeError('HUD DLine1 anchor')
 s=s.replace(anchor,helper+anchor,1)
 a='float DLine1(float2 p,uint t,uint a){if(t<8u)return 0;float2 o=float2(8,8);float m=0;'
 z='float DLine1(float2 p,uint t,uint a){if(t<8u)return 0;if(t==8u)return Nm(p,a);float2 o=float2(8,8);float m=0;'
 if s.count(a)!=1: raise RuntimeError('HUD type8 early anchor')
 s=s.replace(a,z,1)
 a='m=max(m,Cx(p,o,0,67));m=max(m,Cx(p,o,1,85));m=max(m,Cx(p,o,2,82));m=max(m,Cx(p,o,3,82));m=max(m,Cx(p,o,5,48u+a));if(t==8u){m=max(m,Cx(p,o,7,78));m=max(m,Cx(p,o,8,79));m=max(m,Cx(p,o,9,87));}else if(t==9u){'
 z='m=max(m,Cx(p,o,0,67));m=max(m,Cx(p,o,1,85));m=max(m,Cx(p,o,2,82));m=max(m,Cx(p,o,3,82));m=max(m,Cx(p,o,5,48u+a));if(t==9u){'
 if s.count(a)!=1: raise RuntimeError('HUD old numeric type8 branch')
 s=s.replace(a,z,1)
 a='float DLine2(float2 p,uint t,uint b,uint c){if(t<8u||t>=14u)return 0;float2 o=float2(8,24);float m=0;'
 z='float DLine2(float2 p,uint t,uint b,uint c){if(t<9u||t>=14u)return 0;float2 o=float2(8,24);float m=0;'
 if s.count(a)!=1: raise RuntimeError('HUD DLine2 anchor')
 s=s.replace(a,z,1)
 a='if(t==8u){m=max(m,Cx(p,o,0,83));m=max(m,Cx(p,o,1,69));m=max(m,Cx(p,o,2,76));m=max(m,Dig3(p,float2(40,24),b));m=max(m,Cx(p,o,8,80));m=max(m,Dig3(p,float2(80,24),c));}else if(t==9u){'
 z='if(t==9u){'
 if s.count(a)!=1: raise RuntimeError('HUD second numeric line')
 s=s.replace(a,z,1)
 # Syntax-neutral minification to stay in the exact historic embedded source slot.
 s=re.sub(r'(?<=\d)u\b','',s)
 s=s.replace('float2 o=float2(8,8);','float2 o=8;',1)
 s=s.replace('float3 fg=float3(1,1,1);','float3 fg=1;',1)
 out=s.encode('ascii')
 if len(out)>HUD_SHADER_LEN: raise RuntimeError('HUD shader too large '+str(len(out)))
 return out+b' '*(HUD_SHADER_LEN-len(out))

def main(base,obj,fgshader,out):
 src=bytearray(Path(base).read_bytes())
 if sha(src)!=BASE_SHA:raise RuntimeError('wrong RC16 base '+sha(src))
 pi=pe(src);diag=next(s for s in pi['secs'] if s['name']=='.fgdia')
 if pi['ib']!=IMAGE_BASE:raise RuntimeError('image base')
 if (diag['va'],diag['vs'],diag['rs'],diag['rp'])!=(DIAG_RVA,0x940,0xC00,0x4B200):raise RuntimeError('RC16 fgdia geometry')
 if len(src)!=0x4BE00:raise RuntimeError('RC16 file size')
 if ct(src,pi,HOTKEY_SITE)!=OLD_HOTKEY_TARGET:raise RuntimeError('RC16 hotkey target')
 # Freeze the startup parser and all other RC16 hooks before mutation.
 startup=bytes(src[roff(pi,0x2B46):roff(pi,0x2B46)+22])
 if ct(src,pi,0x2B46)!=0x34FD200:raise RuntimeError('startup parser target')
 if ct(src,pi,0x220BD)!=0x34FD600 or ct(src,pi,0x12271)!=0x34FD67C:raise RuntimeError('RC16 HUD2 hooks')
 code,rels,sy=objtext(obj)
 for n in REQ:
  if n not in sy:raise RuntimeError('missing symbol '+n)
 if len(code)>0x400:raise RuntimeError('menu code too large')
 for r,name in rels:
  if name not in TARGETS:raise RuntimeError('external '+name)
  add=struct.unpack_from('<i',code,r)[0]
  disp=(IMAGE_BASE+TARGETS[name])+add-(IMAGE_BASE+NEW_CODE_RVA+r+4)
  struct.pack_into('<i',code,r,disp)
 fg=Path(fgshader).read_bytes()
 if len(fg)>FG_SHADER_LEN:raise RuntimeError('FG shader too large '+str(len(fg)))
 fgpad=fg+b' '*(FG_SHADER_LEN-len(fg))
 # Ensure exact historic source slots are present and NUL terminated.
 if src.find(b'Texture2D<float4> PrevFrame:register(t0);')!=FG_SHADER_OFF or src.find(b'\0',FG_SHADER_OFF)-FG_SHADER_OFF!=FG_SHADER_LEN:raise RuntimeError('FG source slot')
 if src.find(b'cbuffer FeedbackParams:register(b0)')!=HUD_SHADER_OFF or src.find(b'\0',HUD_SHADER_OFF)-HUD_SHADER_OFF!=HUD_SHADER_LEN:raise RuntimeError('HUD source slot')
 hud=make_hud(bytes(src[HUD_SHADER_OFF:HUD_SHADER_OFF+HUD_SHADER_LEN]))
 dst=bytearray(src)
 dst.extend(b'\xcc'*0x400)
 dst[0x4BE00:0x4BE00+len(code)]=code
 struct.pack_into('<I',dst,diag['off']+8,0xC00+len(code))
 struct.pack_into('<I',dst,diag['off']+16,0x1000)
 dst[roff(pi,HOTKEY_SITE):roff(pi,HOTKEY_SITE)+7]=mkcall(HOTKEY_SITE,NEW_CODE_RVA+sy['quality_hotkey_menu1'],7)
 dst[FG_SHADER_OFF:FG_SHADER_OFF+FG_SHADER_LEN]=fgpad
 dst[HUD_SHADER_OFF:HUD_SHADER_OFF+HUD_SHADER_LEN]=hud
 # Startup/critical regression locks.
 if bytes(dst[roff(pi,0x2B46):roff(pi,0x2B46)+22])!=startup:raise RuntimeError('startup parser changed')
 if ct(dst,pe(dst),0x220BD)!=0x34FD600 or ct(dst,pe(dst),0x12271)!=0x34FD67C:raise RuntimeError('unintended RC16 hook change')
 struct.pack_into('<I',dst,pi['cks'],0);cs=cks(dst,pi['cks']);struct.pack_into('<I',dst,pi['cks'],cs)
 Path(out).write_bytes(dst)
 print('OUTPUT_SHA256='+sha(dst));print('PE_CHECKSUM=0x%X'%cs);print('MENU_CODE_RVA=0x%X'%NEW_CODE_RVA);print('MENU_CODE_SIZE=0x%X'%len(code));print('FG_SHADER_BYTES=%d'%len(fg));print('HUD_SHADER_BYTES=%d'%len(hud.rstrip(b' ')));print('STARTUP_PARSER_PRESERVED=YES')
if __name__=='__main__':
 if len(sys.argv)!=5:raise SystemExit('usage: patch_fgquality3_menuhud1_rc16.py RC16_DLL OBJ FG_HLSL OUT_DLL')
 main(*sys.argv[1:])

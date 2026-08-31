#!/usr/bin/env python3
import hashlib,struct,sys
from pathlib import Path
BASE_SHA='3808fcb1681f00342b7ce07e39aebc2b1cabbed8aeb2fd244788744136d1868f';IMAGE_BASE=0x180000000
DIAG_RVA=0x34FD000;DATA_RVA=0x34FC000;NEW_CODE_RVA=0x34FD600;ACTIVE_PROFILE_RVA=0x34FC0F0
TARGETS={'RC14_QUALITY_PARSE':0x34FD200,'RC14_QUALITY_CALC_WIDTH':0x34FD283,'RC14_QUALITY_HOTKEY_ACTION0':0x34FD2EF,'QUALITY_PROFILE':0x34FC09C,'QUALITY_APPLIED_TIER':0x34FC0A4,'QUALITY_PENDING_ME':0x34FC0A8,'QUALITY_ACTIVE_PROFILE':ACTIVE_PROFILE_RVA,'NOTICE_FUNC':0x16270,'STATUS_FUNC':0x16C80,'LOG_FUNC':0x1E50}
REQ=('quality_parse_hud1','quality_calc_width_hud1','quality_hotkey_hud1','quality_status_hud1')
HOOKS={0x2B46:(0x34FD200,'quality_parse_hud1',22),0x220BD:(0x34FD283,'quality_calc_width_hud1',15),0x12225:(0x34FD2EF,'quality_hotkey_hud1',7),0x12271:(0x34FD3CF,'quality_status_hud1',5)}
PATCHES={
b'm=max(m,Cx(p,o,0,84));m=max(m,Cx(p,o,1,69));m=max(m,Cx(p,o,2,83));m=max(m,Cx(p,o,3,84));':b'm=max(m,Cx(p,o,0,67));m=max(m,Cx(p,o,1,85));m=max(m,Cx(p,o,2,82));m=max(m,Cx(p,o,3,82));',
b'm=max(m,Cx(p,o,7,79));m=max(m,Cx(p,o,8,70));m=max(m,Cx(p,o,9,52));':b'm=max(m,Cx(p,o,7,78));m=max(m,Cx(p,o,8,79));m=max(m,Cx(p,o,9,87));',
b'm=max(m,Cx(p,o,0,82));m=max(m,Cx(p,o,1,85));m=max(m,Cx(p,o,2,78));':b'm=max(m,Cx(p,o,0,83));m=max(m,Cx(p,o,1,69));m=max(m,Cx(p,o,2,76));',
b'm=max(m,Cx(p,o,8,88));':b'm=max(m,Cx(p,o,8,80));'}
def sha(x):return hashlib.sha256(x).hexdigest()
def cks(blob,o):
 b=bytearray(blob);struct.pack_into('<I',b,o,0);s=0
 for i in range(0,len(b)-1,2):s+=b[i]|b[i+1]<<8;s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16);return(s+len(b))&0xffffffff
def pe(b):
 e=struct.unpack_from('<I',b,0x3c)[0];co=e+4;machine,n,_,_,_,osz,_=struct.unpack_from('<HHIIIHH',b,co);opt=co+20;sh=opt+osz
 if machine!=0x8664 or struct.unpack_from('<H',b,opt)[0]!=0x20b:raise RuntimeError('PE')
 ss=[]
 for i in range(n):
  o=sh+i*40;nm=b[o:o+8].rstrip(b'\0').decode();vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8);ss.append({'name':nm,'off':o,'vs':vs,'va':va,'rs':rs,'rp':rp,'ch':ch})
 return {'opt':opt,'ib':struct.unpack_from('<Q',b,opt+24)[0],'cks':opt+64,'secs':ss}
def roff(pi,r):
 for s in pi['secs']:
  if s['va']<=r<s['va']+max(s['vs'],s['rs']):return s['rp']+r-s['va']
 raise RuntimeError(hex(r))
def ct(b,pi,r):
 o=roff(pi,r)
 if b[o]!=0xe8:raise RuntimeError('not call '+hex(r))
 return r+5+struct.unpack_from('<i',b,o+1)[0]
def mkcall(src,t,n):return b'\xe8'+struct.pack('<i',t-(src+5))+b'\x90'*(n-5)
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
  if typ!=4:raise RuntimeError('reloc')
  rels.append((r,sn(sidx)))
 return code,rels,sy
def main(base,obj,out):
 src=bytearray(Path(base).read_bytes())
 if sha(src)!=BASE_SHA:raise RuntimeError('wrong base '+sha(src))
 pi=pe(src);diag=next(s for s in pi['secs'] if s['name']=='.fgdia');dat=next(s for s in pi['secs'] if s['name']=='.fgdat')
 if pi['ib']!=IMAGE_BASE or (diag['va'],diag['vs'],diag['rs'],diag['rp'])!=(DIAG_RVA,0x547,0x600,0x4b200) or (dat['va'],dat['vs'],dat['rs'],dat['rp'])!=(DATA_RVA,0xf0,0x200,0x4b000):raise RuntimeError('geometry')
 if len(src)!=0x4b800 or any(src[0x4b0f0:0x4b0f4]):raise RuntimeError('base layout')
 for site,(old,sym,n) in HOOKS.items():
  if ct(src,pi,site)!=old:raise RuntimeError('hook base '+hex(site))
 code,rels,sy=objtext(obj)
 for n in REQ:
  if n not in sy:raise RuntimeError('symbol '+n)
 if len(code)>0x600:raise RuntimeError('code too large')
 for r,name in rels:
  if name not in TARGETS:raise RuntimeError('external '+name)
  add=struct.unpack_from('<i',code,r)[0];disp=(IMAGE_BASE+TARGETS[name])+add-(IMAGE_BASE+NEW_CODE_RVA+r+4);struct.pack_into('<i',code,r,disp)
 dst=bytearray(src);dst.extend(b'\xcc'*0x600);dst[0x4b800:0x4b800+len(code)]=code
 struct.pack_into('<I',dst,diag['off']+8,0x600+len(code));struct.pack_into('<I',dst,diag['off']+16,0xc00);struct.pack_into('<I',dst,dat['off']+8,0xf4);struct.pack_into('<I',dst,0x4b0f0,0)
 for site,(old,sym,n) in HOOKS.items():dst[roff(pi,site):roff(pi,site)+n]=mkcall(site,NEW_CODE_RVA+sy[sym],n)
 hs=0x34960;he=src.find(b'\0',hs)
 if he-hs!=6632:raise RuntimeError('hlsl len')
 h=bytes(src[hs:he])
 for a,z in PATCHES.items():
  if len(a)!=len(z) or h.count(a)!=1:raise RuntimeError('hlsl pattern')
  h=h.replace(a,z,1)
 dst[hs:he]=h;struct.pack_into('<I',dst,pi['cks'],0);cs=cks(dst,pi['cks']);struct.pack_into('<I',dst,pi['cks'],cs);Path(out).write_bytes(dst)
 print('OUTPUT_SHA256='+sha(dst));print('PE_CHECKSUM=0x%X'%cs);print('HUD_CODE_RVA=0x%X'%NEW_CODE_RVA);print('HUD_CODE_SIZE=0x%X'%len(code))
if __name__=='__main__':
 if len(sys.argv)!=4:raise SystemExit('usage: patch_fgqualityhud1_rc14.py RC14_DLL OBJ OUT_DLL')
 main(*sys.argv[1:])

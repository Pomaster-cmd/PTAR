#!/usr/bin/env python3
"""Deterministic RC18 PRESENTDELIVERY1 -> DWMPHASE2 AUTOCOLLECT1 diagnostic lab patch.

No presentation policy changes. Adds a new .dwmlab section containing:
- passive DWM phase sampling every 8th GENERATED and REAL Present helper call;
- dynamic DwmGetCompositionTimingInfo(NULL, ...) resolution (Windows 8.1 contract);
- CTRL+F4 in-game snapshot to existing log + HUD notice;
- CTRL+F5 HUD feedback for the existing visible-marker verifier hotkey;
- every ordinary F8 status press also dumps the DWM phase histogram to the log.
"""
import hashlib, struct, sys
from pathlib import Path

BASE_SHA='2b6fafabeedc89857dbb6d5a318ca143ce8e30ec912050116f70a4a58ad55ad3'
IMAGE_BASE=0x180000000
NEW_RVA=0x34FE000
NEW_RAW=0x4C200
NEW_RAW_SIZE=0x2000
NEW_VSIZE=0x2000
CALL_SITE_RVA=0x25B92
DISPATCH_CALL_RVA=0x12225
PRESENT_HELPER_RVA=0x26540
ORIG_DIAG_RVA=0x34FDC00
NOTICE_RVA=0x16270
LOG_FUNC_RVA=0x1E50
LOG_UINT_RVA=0x8430
QPC_IAT_RVA=0x49F18
QPC_DELTA_US_RVA=0x229B0
LOADLIBRARYW_IAT_RVA=0x49F10
GETPROCADDRESS_IAT_RVA=0x49F00
GETASYNC_IAT_RVA=0x49FB8

DATA={
'DWM_MODULE_PTR':NEW_RVA+0x1000,
'DWM_TIMING_PTR':NEW_RVA+0x1008,
'G_TICK':NEW_RVA+0x1010,
'R_TICK':NEW_RVA+0x1014,
'DWM_CALLS':NEW_RVA+0x1018,
'DWM_FAILS':NEW_RVA+0x101C,
'QPC_FAILS':NEW_RVA+0x1020,
'NEG_START':NEW_RVA+0x1024,
'NEG_END':NEW_RVA+0x1028,
'G_SAMPLES':NEW_RVA+0x102C,
'R_SAMPLES':NEW_RVA+0x1030,
'LAST_PERIOD_TICKS':NEW_RVA+0x1038,
'BASE_SET':NEW_RVA+0x1040,
'BASE_DISPLAYED':NEW_RVA+0x1048,
'LAST_DISPLAYED':NEW_RVA+0x1050,
'BASE_DROPPED':NEW_RVA+0x1058,
'LAST_DROPPED':NEW_RVA+0x1060,
'BASE_LATE':NEW_RVA+0x1068,
'LAST_LATE':NEW_RVA+0x1070,
'G_START_HIST':NEW_RVA+0x1080,
'G_END_HIST':NEW_RVA+0x10A0,
'R_START_HIST':NEW_RVA+0x10C0,
'R_END_HIST':NEW_RVA+0x10E0,
'COMPOSE_HIST':NEW_RVA+0x1100,
'LATCH_F4':NEW_RVA+0x1120,
'LATCH_F5':NEW_RVA+0x1124,
'LATCH_F8':NEW_RVA+0x1128,
}
TARGETS={
'PRESENT_HELPER':PRESENT_HELPER_RVA,'ORIG_DIAG':ORIG_DIAG_RVA,'NOTICE_FUNC':NOTICE_RVA,
'LOG_FUNC':LOG_FUNC_RVA,'LOG_UINT':LOG_UINT_RVA,'QPC_IAT':QPC_IAT_RVA,'QPC_DELTA_US':QPC_DELTA_US_RVA,
'LOADLIBRARYW_IAT':LOADLIBRARYW_IAT_RVA,'GETPROCADDRESS_IAT':GETPROCADDRESS_IAT_RVA,'GETASYNC_IAT':GETASYNC_IAT_RVA,
**DATA,
}

OLD_HUD9_A=b'm=max(m,Cx(p,o,0,67));m=max(m,Cx(p,o,1,85));m=max(m,Cx(p,o,2,82));m=max(m,Cx(p,o,3,82));'
NEW_HUD9_A=b'm=max(m,Cx(p,o,0,68));m=max(m,Cx(p,o,1,87));m=max(m,Cx(p,o,2,77));m=max(m,Cx(p,o,3,32));'
OLD_HUD9_B=b'if(t==9){m=max(m,Cx(p,o,7,68));m=max(m,Cx(p,o,8,79));m=max(m,Cx(p,o,9,78));m=max(m,Cx(p,o,10,69));}'
NEW_HUD9_B=b'if(t==9){m=max(m,Cx(p,o,7,76));m=max(m,Cx(p,o,8,65));m=max(m,Cx(p,o,9,66));m=max(m,Cx(p,o,10,32));}'
OLD_HUD10=b'if(t==10){m=max(m,Cx(p,o,0,83));m=max(m,Cx(p,o,1,87));m=max(m,Cx(p,o,2,69));m=max(m,Cx(p,o,3,69));m=max(m,Cx(p,o,4,80));m=max(m,Cx(p,o,6,68));m=max(m,Cx(p,o,7,79));m=max(m,Cx(p,o,8,78));m=max(m,Cx(p,o,9,69));return m;}'
NEW_HUD10=b'if(t==10){m=max(m,Cx(p,o,0,86));m=max(m,Cx(p,o,1,73));m=max(m,Cx(p,o,2,83));m=max(m,Cx(p,o,3,73));m=max(m,Cx(p,o,4,66));m=max(m,Cx(p,o,6,84));m=max(m,Cx(p,o,7,69));m=max(m,Cx(p,o,8,83));m=max(m,Cx(p,o,9,84));return m;}'
for a,b in [(OLD_HUD9_A,NEW_HUD9_A),(OLD_HUD9_B,NEW_HUD9_B),(OLD_HUD10,NEW_HUD10)]: assert len(a)==len(b)

def sha(b): return hashlib.sha256(bytes(b)).hexdigest()
def align(x,a): return (x+a-1)&~(a-1)

def pe_checksum(blob,off):
    b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
    for i in range(0,len(b)-1,2):
        s += b[i] | (b[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(b)&1: s+=b[-1]; s=(s&0xffff)+(s>>16)
    s=(s&0xffff)+(s>>16); return (s+len(b))&0xffffffff

def parse_pe(b):
    e=struct.unpack_from('<I',b,0x3c)[0]; coff=e+4
    machine,nsec,_ts,_ps,_ns,optsz,_ch=struct.unpack_from('<HHIIIHH',b,coff)
    if machine!=0x8664: raise RuntimeError('not x64')
    opt=coff+20
    if struct.unpack_from('<H',b,opt)[0]!=0x20b: raise RuntimeError('not PE32+')
    if struct.unpack_from('<Q',b,opt+24)[0]!=IMAGE_BASE: raise RuntimeError('unexpected image base')
    file_align=struct.unpack_from('<I',b,opt+36)[0]; sec_align=struct.unpack_from('<I',b,opt+32)[0]; size_headers=struct.unpack_from('<I',b,opt+60)[0]
    sh=opt+optsz; secs=[]
    for i in range(nsec):
        o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace'); vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8)
        secs.append({'name':name,'va':va,'vs':vs,'rp':rp,'rs':rs,'off':o})
    return {'coff':coff,'opt':opt,'nsec':nsec,'sh':sh,'secs':secs,'file_align':file_align,'sec_align':sec_align,'size_headers':size_headers,'checksum_off':opt+64,'size_image_off':opt+56}

def rva_off(pe,r):
    for s in pe['secs']:
        if s['va']<=r<s['va']+max(s['vs'],s['rs']): return s['rp']+(r-s['va'])
    if NEW_RVA<=r<NEW_RVA+NEW_RAW_SIZE: return NEW_RAW+(r-NEW_RVA)
    raise RuntimeError(f'RVA not mapped {r:#x}')

def call_target(b,pe,rva):
    o=rva_off(pe,rva); raw=b[o:o+5]
    if len(raw)!=5 or raw[0]!=0xe8: raise RuntimeError(f'not call at {rva:#x}')
    return rva+5+struct.unpack_from('<i',raw,1)[0]

def mkcall(src,dst):
    v=dst-(src+5)
    return b'\xe8'+struct.pack('<i',v)

def objtext(path):
    b=Path(path).read_bytes(); mach,n,_ts,sp,ns,osz,_ch=struct.unpack_from('<HHIIIHH',b,0)
    if mach!=0x8664 or osz: raise RuntimeError('expected x64 COFF')
    sec=None; secnum=None
    for i in range(n):
        o=20+i*40; nm=b[o:o+8].rstrip(b'\0').decode('ascii','replace'); _vs,_va,rs,rp,relp,_ln,nr,_nl,_fl=struct.unpack_from('<IIIIIIHHI',b,o+8)
        if nm=='.text': sec=(rs,rp,relp,nr); secnum=i+1
    if not sec: raise RuntimeError('.text missing')
    strtab=sp+ns*18; slen=struct.unpack_from('<I',b,strtab)[0]; stab=b[strtab:strtab+slen]
    def symname(idx):
        o=sp+idx*18; raw=b[o:o+8]; z,x=struct.unpack('<II',raw)
        if z==0:
            end=stab.find(b'\0',x); return stab[x:end if end>=0 else len(stab)].decode('ascii')
        return raw.rstrip(b'\0').decode('ascii')
    syms={}; i=0
    while i<ns:
        o=sp+i*18; name=symname(i); val,secno,_typ,_sc,na=struct.unpack_from('<IhHBB',b,o+8)
        if secno==secnum: syms[name]=val
        i += 1+na
    rs,rp,relp,nr=sec; code=bytearray(b[rp:rp+rs]); rels=[]
    for j in range(nr):
        o=relp+j*10; rr,sidx,typ=struct.unpack_from('<IIH',b,o)
        if typ!=4: raise RuntimeError(f'unsupported relocation {typ}')
        rels.append((rr,symname(sidx)))
    return code,rels,syms

def add_section(out,pe):
    if len(out)!=NEW_RAW: raise RuntimeError(f'unexpected base file length {len(out):#x}')
    if pe['sh']+(pe['nsec']+1)*40 > pe['size_headers']: raise RuntimeError('no section header room')
    o=pe['sh']+pe['nsec']*40; hdr=bytearray(40); hdr[:8]=b'.dwmlab\0'
    struct.pack_into('<IIIIIIHHI',hdr,8,NEW_VSIZE,NEW_RVA,NEW_RAW_SIZE,NEW_RAW,0,0,0,0,0xE0000060)
    out[o:o+40]=hdr; struct.pack_into('<H',out,pe['coff']+2,pe['nsec']+1)
    struct.pack_into('<I',out,pe['size_image_off'],align(NEW_RVA+NEW_VSIZE,pe['sec_align']))
    out.extend(b'\x00'*NEW_RAW_SIZE)

def main():
    if len(sys.argv)!=4: raise SystemExit('usage: patch_dwmphase2_rc18.py RC18_DLL DWMPHASE2_OBJ OUT_DLL')
    src,obj,dst=map(Path,sys.argv[1:]); base=src.read_bytes()
    if sha(base)!=BASE_SHA: raise RuntimeError('input is not exact RC18 field base')
    pe=parse_pe(base)
    if call_target(base,pe,CALL_SITE_RVA)!=PRESENT_HELPER_RVA: raise RuntimeError('Present helper call site changed')
    if call_target(base,pe,DISPATCH_CALL_RVA)!=ORIG_DIAG_RVA: raise RuntimeError('diag dispatch call site changed')
    # exact RC18 presentation locks
    if base[rva_off(pe,0x265CF):rva_off(pe,0x265CF)+8] != bytes.fromhex('ba010000004531c0'): raise RuntimeError('RC18 Present(1,0) changed')
    for r in (0x259C9,0x25B58):
        if base[rva_off(pe,r):rva_off(pe,r)+5]!=b'\x90'*5: raise RuntimeError('RC18 prewait lock changed')
    out=bytearray(base); add_section(out,pe)
    code,rels,syms=objtext(obj)
    for sym in ('dwmphase_present_wrapper','dwmphase_snapshot','dwmphase_hotkey_wrapper'):
        if sym not in syms: raise RuntimeError(sym+' missing')
    if len(code)>0x0F00: raise RuntimeError(f'code too large {len(code):#x}')
    for rr,name in rels:
        if name not in TARGETS: raise RuntimeError('unmapped external '+name)
        add=struct.unpack_from('<i',code,rr)[0]
        disp=(IMAGE_BASE+TARGETS[name])+add-(IMAGE_BASE+NEW_RVA+rr+4)
        if not -(1<<31)<=disp<(1<<31): raise RuntimeError('rel32 out of range '+name)
        struct.pack_into('<i',code,rr,disp)
    sec=bytearray(b'\xcc'*NEW_RAW_SIZE); sec[:len(code)]=code
    # zero writable data region 0x1000..0x13ff
    sec[0x1000:0x1400]=b'\0'*0x400
    out[NEW_RAW:NEW_RAW+NEW_RAW_SIZE]=sec
    # call patches
    out[rva_off(pe,CALL_SITE_RVA):rva_off(pe,CALL_SITE_RVA)+5]=mkcall(CALL_SITE_RVA,NEW_RVA+syms['dwmphase_present_wrapper'])
    out[rva_off(pe,DISPATCH_CALL_RVA):rva_off(pe,DISPATCH_CALL_RVA)+5]=mkcall(DISPATCH_CALL_RVA,NEW_RVA+syms['dwmphase_hotkey_wrapper'])
    # HUD diagnostic notice strings, dormant types 9/10
    for old,new,label in [(OLD_HUD9_A,NEW_HUD9_A,'HUD9A'),(OLD_HUD9_B,NEW_HUD9_B,'HUD9B'),(OLD_HUD10,NEW_HUD10,'HUD10')]:
        n=out.count(old)
        if n!=1: raise RuntimeError(f'{label} expected once, found {n}')
        p=out.find(old); out[p:p+len(old)]=new
    # no presentation policy changes
    if out[rva_off(pe,0x265CF):rva_off(pe,0x265CF)+8] != bytes.fromhex('ba010000004531c0'): raise RuntimeError('instrumentation altered Present args')
    for r in (0x259C9,0x25B58):
        if out[rva_off(pe,r):rva_off(pe,r)+5]!=b'\x90'*5: raise RuntimeError('instrumentation restored prewait')
    struct.pack_into('<I',out,pe['checksum_off'],0); cs=pe_checksum(out,pe['checksum_off']); struct.pack_into('<I',out,pe['checksum_off'],cs)
    dst.write_bytes(out)
    print('BASE_SHA256='+sha(base)); print('OUT_SHA256='+sha(out)); print(f'PE_CHECKSUM=0x{cs:08x}')
    print(f'CODE_SIZE=0x{len(code):X}'); print(f'PRESENT_WRAPPER_RVA=0x{NEW_RVA+syms["dwmphase_present_wrapper"]:X}')
    print(f'SNAPSHOT_RVA=0x{NEW_RVA+syms["dwmphase_snapshot"]:X}'); print(f'HOTKEY_WRAPPER_RVA=0x{NEW_RVA+syms["dwmphase_hotkey_wrapper"]:X}')
    print('SAMPLE_DIVISOR=8'); print('PRESENT_POLICY=UNCHANGED_RC18_SYNC1_FLAGS0_NO_PREWAIT')

if __name__=='__main__': main()

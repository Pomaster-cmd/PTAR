#!/usr/bin/env python3
import hashlib, struct, sys
from pathlib import Path

BASE_SHA='2c8b77c540988c5671f604abeaf244c660e2ede11f5b74fb716c7e9a184affd2'
IMAGE_BASE=0x180000000
DIAG_RVA=0x34FD000
DATA_RVA=0x34FC000
QUALITY_CODE_RVA=DIAG_RVA+0x200

QUALITY_PROFILE_RVA=DATA_RVA+0x9C
QUALITY_HOTKEY_LATCH_RVA=DATA_RVA+0xA0
QUALITY_APPLIED_TIER_RVA=DATA_RVA+0xA4
QUALITY_PENDING_ME_RVA=DATA_RVA+0xA8
QUALITY_CHANGES_RVA=DATA_RVA+0xAC
QUALITY_KEY_RVA=DATA_RVA+0xC0
DATA_VSIZE=0xF0

BLEND_GUARD_GLOBAL_RVA=0x4B070
FG_SCHEDULER_RUNNING_RVA=0x2C93B94
GET_ASYNC_KEYSTATE_IAT_RVA=0x49FB8
HOTKEY_CHECK_FUNC_RVA=0x167D0
STATUS_FUNC_RVA=0x16C80
LOG_FUNC_RVA=0x1E50

PARSER_SITE_RVA=0x2B46
BLEND_STORE_RVA=0x2C5D
ME_WIDTH_RVA=0x220BD
ME_HEIGHT_RVA=0x220CC
HOTKEY_ACTION0_SITE_RVA=0x12225
STATUS_CALL_RVA=0x12271

ORIG_PARSER=bytes.fromhex('48 8d 15 f7 65 04 00 48 89 f9 41 b8 21 00 00 00 49 89 f1 41 ff d6')
ORIG_BLEND_STORE=bytes.fromhex('89 0d 0d 84 04 00')
ORIG_ME_WIDTH=bytes.fromhex('8d 43 01 d1 e8')+b'\x90'*10
ORIG_ME_HEIGHT=bytes.fromhex('41 8d 4e 01 d1 e9')+b'\x90'*10
ORIG_HOTKEY_ACTION0=bytes.fromhex('31 c9 e8 a4 45 00 00')
ORIG_STATUS_CALL=bytes.fromhex('e8 0a 4a 00 00')

TARGETS={
 'QUALITY_PROFILE':QUALITY_PROFILE_RVA,
 'QUALITY_HOTKEY_LATCH':QUALITY_HOTKEY_LATCH_RVA,
 'QUALITY_APPLIED_TIER':QUALITY_APPLIED_TIER_RVA,
 'QUALITY_PENDING_ME':QUALITY_PENDING_ME_RVA,
 'QUALITY_CHANGES':QUALITY_CHANGES_RVA,
 'QUALITY_KEY':QUALITY_KEY_RVA,
 'BLEND_GUARD_GLOBAL':BLEND_GUARD_GLOBAL_RVA,
 'FG_SCHEDULER_RUNNING':FG_SCHEDULER_RUNNING_RVA,
 'GET_ASYNC_KEYSTATE_IAT':GET_ASYNC_KEYSTATE_IAT_RVA,
 'HOTKEY_CHECK_FUNC':HOTKEY_CHECK_FUNC_RVA,
 'STATUS_FUNC':STATUS_FUNC_RVA,
 'LOG_FUNC':LOG_FUNC_RVA,
}

REQ_SYMS=('quality_parse','quality_apply_guard','quality_calc_width','quality_calc_height','quality_hotkey_and_action0','quality_status_wrapper')


def sha(b): return hashlib.sha256(b).hexdigest()

def checksum(blob,off):
    b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
    for i in range(0,len(b)-1,2):
        s += b[i] | (b[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(b)&1: s += b[-1]
    s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
    return (s+len(b))&0xffffffff

def parse_pe(b):
    e=struct.unpack_from('<I',b,0x3c)[0]
    if b[e:e+4]!=b'PE\0\0': raise RuntimeError('not PE')
    coff=e+4; machine,nsec,_,_,_,optsz,_=struct.unpack_from('<HHIIIHH',b,coff)
    if machine!=0x8664: raise RuntimeError('not AMD64')
    opt=coff+20
    if struct.unpack_from('<H',b,opt)[0]!=0x20b: raise RuntimeError('not PE32+')
    sh=opt+optsz; secs=[]
    for i in range(nsec):
        o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
        vs,va,rs,rp,_,_,_,_,ch=struct.unpack_from('<IIIIIIHHI',b,o+8)
        secs.append(dict(name=name,off=o,vs=vs,va=va,rs=rs,rp=rp,ch=ch))
    return dict(opt=opt,secs=secs,ib=struct.unpack_from('<Q',b,opt+24)[0],cks_off=opt+64,sizeimg=struct.unpack_from('<I',b,opt+56)[0])

def rva_to_off(pe,rva):
    for s in pe['secs']:
        if s['va']<=rva<s['va']+max(s['vs'],s['rs']): return s['rp']+(rva-s['va'])
    raise RuntimeError('RVA not mapped '+hex(rva))

def coff_text(objpath):
    b=Path(objpath).read_bytes()
    machine,nsec,_,symptr,nsym,optsz,_=struct.unpack_from('<HHIIIHH',b,0)
    if machine!=0x8664 or optsz!=0: raise RuntimeError('unexpected COFF')
    sec=None; sec_index=None
    for i in range(nsec):
        o=20+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace')
        _,_,rawsz,rawptr,relptr,_,nrel,_,_=struct.unpack_from('<IIIIIIHHI',b,o+8)
        if name=='.text': sec=(rawsz,rawptr,relptr,nrel); sec_index=i+1
    if not sec: raise RuntimeError('no .text')
    strtab_off=symptr+nsym*18; slen=struct.unpack_from('<I',b,strtab_off)[0]; stab=b[strtab_off:strtab_off+slen]
    def sym_name(i):
        o=symptr+i*18; rn=b[o:o+8]; z,so=struct.unpack('<II',rn)
        if z==0:
            end=stab.find(b'\0',so); end=len(stab) if end<0 else end
            return stab[so:end].decode('ascii')
        return rn.rstrip(b'\0').decode('ascii')
    symbols={}; i=0
    while i<nsym:
        o=symptr+i*18; name=sym_name(i); val,secno,typ,sc,naux=struct.unpack_from('<IhHBB',b,o+8)
        if secno==sec_index: symbols[name]=val
        i += 1+naux
    rawsz,rawptr,relptr,nrel=sec; code=bytearray(b[rawptr:rawptr+rawsz]); rel=[]
    for j in range(nrel):
        o=relptr+j*10; roff,sidx,rtyp=struct.unpack_from('<IIH',b,o)
        if rtyp!=4: raise RuntimeError(f'unexpected relocation {rtyp:#x}')
        rel.append((roff,sym_name(sidx)))
    return code,rel,symbols

def make_call(src_rva,target_rva,total_len=5):
    disp=target_rva-(src_rva+5)
    if not -(1<<31)<=disp<(1<<31): raise RuntimeError('call rel32 overflow')
    if total_len<5: raise RuntimeError('call site too short')
    return b'\xE8'+struct.pack('<i',disp)+b'\x90'*(total_len-5)

def patch(base,obj,outp):
    src=bytearray(Path(base).read_bytes())
    if sha(src)!=BASE_SHA: raise RuntimeError('wrong RC13 base hash '+sha(src))
    pe=parse_pe(src)
    if pe['ib']!=IMAGE_BASE: raise RuntimeError('unexpected image base')
    diag=next(s for s in pe['secs'] if s['name']=='.fgdia')
    dat=next(s for s in pe['secs'] if s['name']=='.fgdat')
    if (diag['va'],diag['vs'],diag['rs'])!=(DIAG_RVA,0x200,0x200): raise RuntimeError('unexpected RC13 .fgdia geometry')
    if (dat['va'],dat['vs'],dat['rs'])!=(DATA_RVA,0x9C,0x200): raise RuntimeError('unexpected RC13 .fgdat geometry')
    if len(src)!=diag['rp']+diag['rs']: raise RuntimeError('unexpected PE overlay/trailing data')
    if any(src[dat['rp']+0x9C:dat['rp']+0xC0]): raise RuntimeError('quality globals range not zero')
    if any(src[dat['rp']+0xC0:dat['rp']+DATA_VSIZE]): raise RuntimeError('quality key range not zero')

    code,rel,syms=coff_text(obj)
    for s in REQ_SYMS:
        if s not in syms: raise RuntimeError('missing symbol '+s)
    if len(code)>0x400: raise RuntimeError(f'quality object too large {len(code):#x}')
    code_va=IMAGE_BASE+QUALITY_CODE_RVA
    counts={}
    for roff,name in rel:
        if name not in TARGETS: raise RuntimeError('unknown external symbol '+name)
        target=IMAGE_BASE+TARGETS[name]; addend=struct.unpack_from('<i',code,roff)[0]
        disp=target+addend-(code_va+roff+4)
        if not -(1<<31)<=disp<(1<<31): raise RuntimeError('rel32 overflow '+name)
        struct.pack_into('<i',code,roff,disp); counts[name]=counts.get(name,0)+1

    out=bytearray(src)
    # Extend last executable .fgdia section by 0x400 raw bytes and place quality code at +0x200.
    out.extend(b'\xCC'*0x400)
    qoff=diag['rp']+0x200
    out[qoff:qoff+len(code)]=code
    struct.pack_into('<I',out,diag['off']+8,0x200+len(code))      # VirtualSize
    struct.pack_into('<I',out,diag['off']+16,0x600)             # SizeOfRawData

    # Extend .fgdat virtual size and seed UTF-16 key string.
    struct.pack_into('<I',out,dat['off']+8,DATA_VSIZE)
    key=('FrameGenerationQuality\0').encode('utf-16le')
    ko=dat['rp']+(QUALITY_KEY_RVA-DATA_RVA)
    out[ko:ko+len(key)]=key

    def exact_call(rva,orig,sym,total_len=None):
        off=rva_to_off(pe,rva); n=len(orig) if total_len is None else total_len
        if bytes(src[off:off+len(orig)])!=orig:
            raise RuntimeError(f'site mismatch {rva:#x}: '+src[off:off+len(orig)].hex())
        out[off:off+n]=make_call(rva,QUALITY_CODE_RVA+syms[sym],n)

    exact_call(PARSER_SITE_RVA,ORIG_PARSER,'quality_parse',len(ORIG_PARSER))
    exact_call(BLEND_STORE_RVA,ORIG_BLEND_STORE,'quality_apply_guard',len(ORIG_BLEND_STORE))
    exact_call(ME_WIDTH_RVA,ORIG_ME_WIDTH,'quality_calc_width',len(ORIG_ME_WIDTH))
    exact_call(ME_HEIGHT_RVA,ORIG_ME_HEIGHT,'quality_calc_height',len(ORIG_ME_HEIGHT))
    exact_call(HOTKEY_ACTION0_SITE_RVA,ORIG_HOTKEY_ACTION0,'quality_hotkey_and_action0',len(ORIG_HOTKEY_ACTION0))
    exact_call(STATUS_CALL_RVA,ORIG_STATUS_CALL,'quality_status_wrapper',len(ORIG_STATUS_CALL))

    struct.pack_into('<I',out,pe['cks_off'],0)
    c=checksum(out,pe['cks_off']); struct.pack_into('<I',out,pe['cks_off'],c)
    Path(outp).write_bytes(out)
    print('BASE_SHA256='+BASE_SHA)
    print('OUTPUT_SHA256='+sha(out))
    print('QUALITY_CODE_RVA=0x%X'%QUALITY_CODE_RVA)
    print('QUALITY_CODE_SIZE=0x%X'%len(code))
    for s in REQ_SYMS: print('%s_RVA=0x%X'%(s.upper(),QUALITY_CODE_RVA+syms[s]))
    print('FGDIA_VSIZE=0x%X'%(0x200+len(code)))
    print('FGDIA_RAWSIZE=0x600')
    print('FGDAT_VSIZE=0x%X'%DATA_VSIZE)
    print('QUALITY_PROFILE_RVA=0x%X'%QUALITY_PROFILE_RVA)
    print('QUALITY_KEY_RVA=0x%X'%QUALITY_KEY_RVA)
    print('PE_CHECKSUM=0x%X'%c)
    print('RELOC_COUNTS='+repr(counts))

if __name__=='__main__':
    if len(sys.argv)!=4: raise SystemExit('usage: patch_fgquality2_rc13.py RC13_DLL OBJ OUT_DLL')
    patch(*sys.argv[1:])

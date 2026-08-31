#!/usr/bin/env python3
import hashlib, struct, sys
from pathlib import Path

BASE_SHA='b253f0457f61a7c268ea3747864eb53e14ab113464f3c62f2a137da56bc18148'
IMAGE_BASE=0x180000000
ME_WIDTH_RVA=0x220BD
ME_HEIGHT_RVA=0x220CC
ORIG_WIDTH=bytes.fromhex('8d 43 02 0f b7 c0 69 c0 ab aa 00 00 c1 e8 11')
ORIG_HEIGHT=bytes.fromhex('41 8d 4e 02 0f b7 c9 69 c9 ab aa 00 00 c1 e9 11')
NEW_WIDTH=bytes.fromhex('8d 43 01 d1 e8') + b'\x90'*10
NEW_HEIGHT=bytes.fromhex('41 8d 4e 01 d1 e9') + b'\x90'*10
OLD_LOG=b'INFO: P1FG7N ME profile fixed at validated B14/B13/B12/B3 1/3 source dimensions;'
NEW_LOG=b'INFO: P1FG7N ME profile fixed at validated B14/B13/B12/B3 1/2 source dimensions;'


def sha(b): return hashlib.sha256(b).hexdigest()

def checksum(blob,off):
    b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
    for i in range(0,len(b)-1,2):
        s += b[i] | (b[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(b)&1: s+=b[-1]
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
        vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8)
        secs.append(dict(name=name,vs=vs,va=va,rs=rs,rp=rp))
    return dict(opt=opt,ib=struct.unpack_from('<Q',b,opt+24)[0],cks=opt+64,secs=secs)

def rva_off(pe,rva):
    for s in pe['secs']:
        if s['va']<=rva<s['va']+max(s['vs'],s['rs']): return s['rp']+(rva-s['va'])
    raise RuntimeError('unmapped RVA '+hex(rva))

def patch(inp,outp):
    src=Path(inp).read_bytes()
    if sha(src)!=BASE_SHA: raise RuntimeError('wrong RC12 base hash '+sha(src))
    pe=parse_pe(src)
    if pe['ib']!=IMAGE_BASE: raise RuntimeError('unexpected image base')
    out=bytearray(src)
    wo=rva_off(pe,ME_WIDTH_RVA); ho=rva_off(pe,ME_HEIGHT_RVA)
    if src[wo:wo+len(ORIG_WIDTH)]!=ORIG_WIDTH:
        raise RuntimeError('ME width site mismatch '+src[wo:wo+len(ORIG_WIDTH)].hex())
    if src[ho:ho+len(ORIG_HEIGHT)]!=ORIG_HEIGHT:
        raise RuntimeError('ME height site mismatch '+src[ho:ho+len(ORIG_HEIGHT)].hex())
    out[wo:wo+len(ORIG_WIDTH)]=NEW_WIDTH
    out[ho:ho+len(ORIG_HEIGHT)]=NEW_HEIGHT
    pos=src.find(OLD_LOG)
    if pos<0 or src.find(OLD_LOG,pos+1)>=0: raise RuntimeError('expected unique ME log string')
    if len(OLD_LOG)!=len(NEW_LOG): raise RuntimeError('log replacement length changed')
    out[pos:pos+len(OLD_LOG)]=NEW_LOG
    struct.pack_into('<I',out,pe['cks'],0)
    c=checksum(out,pe['cks']); struct.pack_into('<I',out,pe['cks'],c)
    Path(outp).write_bytes(out)
    print('BASE_SHA256='+BASE_SHA)
    print('OUTPUT_SHA256='+sha(out))
    print('ME_WIDTH_RVA=0x%X'%ME_WIDTH_RVA)
    print('ME_HEIGHT_RVA=0x%X'%ME_HEIGHT_RVA)
    print('ME_PROFILE=CEIL_SOURCE_DIV2_CLAMP64_ALIGN16')
    print('EXPECTED_ME_1280X720=640X368')
    print('PE_CHECKSUM=0x%X'%c)

if __name__=='__main__':
    if len(sys.argv)!=3: raise SystemExit('usage: patch_fgquality1_rc12.py RC12_DLL OUT_DLL')
    patch(sys.argv[1],sys.argv[2])

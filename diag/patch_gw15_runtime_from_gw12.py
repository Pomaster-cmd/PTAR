#!/usr/bin/env python3
"""Deterministic GW12 -> GW15 LOCK30 patch.

Field basis:
- GW12 F4 external verifier measured ~31.3 unique FPS but strongly uneven pacing.
- REAL dwell ~16.94 ms (1 VBlank) while GENERATED dwell ~49.63 ms (3 VBlanks).
- The pair arrives as a burst on consecutive refreshes, then idles ~2 refreshes.
- GW15 changes only isolated Flip3 Present SyncInterval 1 -> 2 to force one content
  every two 60-Hz refreshes (~33.3 ms), while keeping GW12 LOADSHEDSOFT1 OFF behavior.
"""
import hashlib, struct, sys
from pathlib import Path
BASE_SHA='50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59'
PRESENT_SYNC_RVA=0x265cf

def sha(b): return hashlib.sha256(bytes(b)).hexdigest()
def parse(b):
    e=struct.unpack_from('<I',b,0x3c)[0];coff=e+4
    n=struct.unpack_from('<H',b,coff+2)[0];optsz=struct.unpack_from('<H',b,coff+16)[0]
    opt=coff+20;sh=opt+optsz;secs=[]
    for i in range(n):
        o=sh+i*40;name=b[o:o+8].rstrip(b'\0').decode('ascii')
        vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);secs.append((name,vs,va,rs,rp))
    return opt,secs
def roff(b,rva):
    for _n,vs,va,rs,rp in parse(b)[1]:
        if va<=rva<va+max(vs,rs):return rp+(rva-va)
    raise RuntimeError('RVA not mapped')
def checksum(blob,off):
    b=bytearray(blob);struct.pack_into('<I',b,off,0);s=0
    for i in range(0,len(b)-1,2):
        s+=b[i]|(b[i+1]<<8);s=(s&0xffff)+(s>>16)
    if len(b)&1:s+=b[-1];s=(s&0xffff)+(s>>16)
    s=(s&0xffff)+(s>>16);return (s+len(b))&0xffffffff
def main():
    if len(sys.argv)!=3:raise SystemExit('usage: patch_gw13_pace2_from_gw12.py GW12_DLL OUT_DLL')
    src,dst=map(Path,sys.argv[1:])
    base=src.read_bytes()
    if sha(base)!=BASE_SHA:raise RuntimeError('exact GW12 runtime required')
    if len(base)!=320000:raise RuntimeError('size lock failed')
    out=bytearray(base);off=roff(base,PRESENT_SYNC_RVA)
    if base[off:off+5]!=bytes.fromhex('ba01000000'):raise RuntimeError('Present SyncInterval=1 lock failed')
    out[off:off+5]=bytes.fromhex('ba02000000')
    opt,_=parse(out);csoff=opt+64
    struct.pack_into('<I',out,csoff,0);struct.pack_into('<I',out,csoff,checksum(out,csoff))
    dst.write_bytes(out)
    print('BASE_SHA256='+BASE_SHA)
    print('OUT_SHA256='+sha(out))
    print('DELTA=Present SyncInterval 1 -> 2 only + PE checksum')
if __name__=='__main__':main()

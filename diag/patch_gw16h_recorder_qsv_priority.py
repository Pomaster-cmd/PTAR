#!/usr/bin/env python3
"""GW16H UNIFIEDREC3 SAFEPOINT5 / QSVPRIORITY1 surgical throughput patch.

Base: exact SAFEPOINT4/STATEGUARD1 runtime.

Field evidence PTAR_GW16H_RESULTS_20260906_113420 proves the FG presenter is
healthy (389 REAL + 389 GENERATED presents, no queue/mailbox/runtime failures),
while recorder profile 3 saturates its 32-frame encode queue: 480 QSV queue-full
drops in 12.557 s and only 241 unique frames reach the encoder.

This patch changes consumer scheduling only:
  * B18K18 encode/pipe writer thread: BELOW_NORMAL -> NORMAL;
  * FFmpeg/QSV child process: remove BELOW_NORMAL_PRIORITY_CLASS, retain
    CREATE_NO_WINDOW.

The GPU readback worker remains BELOW_NORMAL. Presenter/FG never waits. Recorder
timeline, ring/queue sizes, conversion, QSV command line, bitrate/profiles,
SAFEPOINT routing, local GPU state and native D3D11 state guard are unchanged.
"""
from pathlib import Path
import hashlib, struct, sys

BASE_SHA='1b8e8673ff2b046604b282c55d4d9bcfe053ad046fa2c30a69a6b6ff5ffe6e8b'
SIZE=320000
ENC_THREAD_PRIORITY_RVA=0x0001F679
FFMPEG_CREATE_FLAGS_RVA=0x00020687
OLD_THREAD=bytes.fromhex('BAFFFFFFFF')
NEW_THREAD=bytes.fromhex('BA00000000')
OLD_FLAGS=bytes.fromhex('C744242800400008')
NEW_FLAGS=bytes.fromhex('C744242800000008')

def sha(b): return hashlib.sha256(bytes(b)).hexdigest()

def parse(b):
    e=struct.unpack_from('<I',b,0x3c)[0]; coff=e+4
    n=struct.unpack_from('<H',b,coff+2)[0]; optsz=struct.unpack_from('<H',b,coff+16)[0]
    opt=coff+20; sh=opt+optsz; secs=[]
    for i in range(n):
        o=sh+i*40
        name=b[o:o+8].rstrip(b'\0').decode('ascii')
        vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8)
        secs.append((name,vs,va,rs,rp))
    return opt,secs

def roff(b,rva):
    for _n,vs,va,rs,rp in parse(b)[1]:
        if va<=rva<va+max(vs,rs): return rp+(rva-va)
    raise RuntimeError('RVA not mapped %#x'%rva)

def checksum(blob,off):
    x=bytearray(blob); struct.pack_into('<I',x,off,0); s=0
    for i in range(0,len(x)-1,2):
        s+=x[i]|(x[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(x)&1:s+=x[-1]
    s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
    return (s+len(x))&0xffffffff

def patch_exact(out,base,rva,old,new,label):
    o=roff(base,rva)
    if base[o:o+len(old)]!=old:
        raise RuntimeError('%s lock failed at RVA %#x: %s'%(label,rva,base[o:o+len(old)].hex()))
    out[o:o+len(new)]=new

def main():
    if len(sys.argv)!=3: raise SystemExit('usage: patch_gw16h_recorder_qsv_priority.py SAFEPOINT4_DLL OUT_DLL')
    src,dst=map(Path,sys.argv[1:]); base=src.read_bytes()
    if len(base)!=SIZE or sha(base)!=BASE_SHA: raise RuntimeError('exact SAFEPOINT4 required: '+sha(base))
    out=bytearray(base)
    patch_exact(out,base,ENC_THREAD_PRIORITY_RVA,OLD_THREAD,NEW_THREAD,'encode thread priority')
    patch_exact(out,base,FFMPEG_CREATE_FLAGS_RVA,OLD_FLAGS,NEW_FLAGS,'FFmpeg process flags')
    opt,_=parse(out); csoff=opt+64
    struct.pack_into('<I',out,csoff,0); struct.pack_into('<I',out,csoff,checksum(out,csoff))
    dst.write_bytes(out)
    print('BASE_SHA256='+BASE_SHA)
    print('OUT_SHA256='+sha(out))
    print('ENC_THREAD_PRIORITY_RVA=0x%X BELOW_NORMAL_TO_NORMAL'%ENC_THREAD_PRIORITY_RVA)
    print('FFMPEG_CREATE_FLAGS_RVA=0x%X 0x08004000_TO_0x08000000'%FFMPEG_CREATE_FLAGS_RVA)
    print('GPU_READBACK_WORKER_PRIORITY=UNCHANGED_BELOW_NORMAL')
    print('DELTA=CONSUMER_SCHEDULING_ONLY')
if __name__=='__main__': main()

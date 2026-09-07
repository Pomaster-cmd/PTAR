#!/usr/bin/env python3
"""GW16H UNIFIEDREC3 SAFEPOINT4 / STATEGUARD1 surgical binary patch.

Base: exact SAFEPOINT3/LOCALGPU1 runtime.

Field evidence from SAFEPOINT3 proved that native-USR recording now produces a
complete 1920x1080/60 H.264 stream, but the first recorder conversion mutates the
live game's D3D11 immediate-context pipeline state. The first video frame is normal,
the second is already red/black, and F9 screenshots remain corrupted even after
recording stops.

This patch redirects only the SAFEPOINT2 native-USR recorder submit call through a
native-context state guard. The guard snapshots every D3D11 pipeline state category
that B18K18 conversion changes, calls the original recorder submit, restores the exact
snapshot, releases getter-acquired COM references, and returns the original result.

FG-ON isolated presenter routing, recorder/QSV conversion logic, profiles, pacing,
hotkeys, resource guards and local VS/RS ownership are unchanged.
"""
from pathlib import Path
import hashlib, struct, sys

BASE_SHA='961b7c0211239364920516e990f9f5a6a3284709bcd4a2a17e6a21b1a76556f2'
FINAL_SHA='1b8e8673ff2b046604b282c55d4d9bcfe053ad046fa2c30a69a6b6ff5ffe6e8b'
SIZE=320000

ORIG_RECORDER_SUBMIT=0x0027760
NATIVE_SUBMIT_CALL=0x034FEA16
MAIN_RVA=0x034FE804
SAVE_RVA=0x034FECB3
RESTORE_RVA=0x034FEEC3
RELEASE_RVA=0x034FF67D

# Exact machine code generated and lab-disassembled for this locked PE.
MAIN=bytes.fromhex(
'5356574881ec500300004889cb4889d64c89c7488d4c2420ba28030000e85a73b0fc'
'4889f1488d542420e8800400004889d94889f24989f8e81f8fb2fc89842448030000'
'4889f1488d542420e86e060000488d4c2420e81e0e00008b8424480300004881c450'
'0300005f5e5bc3')
SAVE=bytes.fromhex(
'5356574883ec204889ce4889d3488b3e4889f131d241b8010000004c8d0bff9748020000'
'4889f1488d53084531c04531c9ff97500200004889f1488d53104531c04531c9ff976002'
'00004889f131d241b8010000004c8d4b18ff97680200004889f1488d5320ff9770020000'
'4889f1488d53284531c04531c9ff97900200004889f1488d9388000000ff979802000048'
'89f1ba080000004c8d43304c8d4b70ff97c80200004889f1488d53784c8d83980000004c'
'8d8b94000000ff97d80200004889f1488d9380000000ff97f0020000c7838c0000001000'
'00004889f1488d938c0000004c8d83a8000000ff97f8020000c783900000001000000048'
'89f1488d93900000004c8d8328020000ff97000300004883c4205f5e5bc3')
RESTORE=bytes.fromhex(
'5356574883ec204889ce4889d3488b3e4889f1ba080000004c8d43304c8b4b70ff970801'
'00004889f1488b53784c8d8398000000448b8b94000000ff97180100004889f1488b9380'
'000000ff97580100004889f18b938c0000004c8d83a8000000ff97600100004889f18b93'
'900000004c8d8328020000ff97680100004889f1488b5320ff97880000004889f18b9388'
'000000ff97c00000004889f1488b53104531c04531c9ff97580000004889f1488b532845'
'31c04531c9ff97b80000004889f131d241b8010000004c8d4b18ff97800000004889f131'
'd241b8010000004c8d0bff97400000004889f1488b53084531c04531c9ff974800000048'
'83c4205f5e5bc3')
RELEASE=bytes.fromhex(
'53564883ec284889cebb11000000488b0e4885c9740d488b01ff501048c7060000000048'
'83c608ffcb75e34883c4285e5bc3')

def sha(b): return hashlib.sha256(bytes(b)).hexdigest()

def parse(b):
    e=struct.unpack_from('<I',b,0x3c)[0]
    coff=e+4
    n=struct.unpack_from('<H',b,coff+2)[0]
    optsz=struct.unpack_from('<H',b,coff+16)[0]
    opt=coff+20
    sh=opt+optsz
    secs=[]
    for i in range(n):
        o=sh+i*40
        name=b[o:o+8].rstrip(b'\0').decode('ascii')
        vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8)
        ch=struct.unpack_from('<I',b,o+36)[0]
        secs.append((name,vs,va,rs,rp,ch))
    return opt,secs

def roff(b,rva):
    for _n,vs,va,rs,rp,_ch in parse(b)[1]:
        if va<=rva<va+max(vs,rs):
            return rp+(rva-va)
    raise RuntimeError('RVA not mapped %#x'%rva)

def rel32(src,ilen,dst):
    return struct.pack('<i',dst-(src+ilen))

def call_target(b,site):
    o=roff(b,site)
    x=b[o:o+5]
    if len(x)!=5 or x[0]!=0xe8:
        return None
    return site+5+struct.unpack_from('<i',x,1)[0]

def checksum(blob,off):
    x=bytearray(blob)
    struct.pack_into('<I',x,off,0)
    s=0
    for i in range(0,len(x)-1,2):
        s+=x[i]|(x[i+1]<<8)
        s=(s&0xffff)+(s>>16)
    if len(x)&1:
        s+=x[-1]
    s=(s&0xffff)+(s>>16)
    s=(s&0xffff)+(s>>16)
    return (s+len(x))&0xffffffff

def put(out,base,rva,code):
    o=roff(base,rva)
    if base[o:o+len(code)]!=b'\xcc'*len(code):
        raise RuntimeError('code cave not pristine at RVA %#x'%rva)
    out[o:o+len(code)]=code

def main():
    if len(sys.argv)!=3:
        raise SystemExit('usage: patch_gw16h_recorder_native_state_guard.py SAFEPOINT3_DLL OUT_DLL')
    src,dst=map(Path,sys.argv[1:])
    base=src.read_bytes()
    if len(base)!=SIZE or sha(base)!=BASE_SHA:
        raise RuntimeError('exact SAFEPOINT3 required: '+sha(base))
    if call_target(base,NATIVE_SUBMIT_CALL)!=ORIG_RECORDER_SUBMIT:
        raise RuntimeError('native submit call lock failed')

    out=bytearray(base)
    put(out,base,MAIN_RVA,MAIN)
    put(out,base,SAVE_RVA,SAVE)
    put(out,base,RESTORE_RVA,RESTORE)
    put(out,base,RELEASE_RVA,RELEASE)

    # Native-USR only: route submit through the state guard.
    o=roff(base,NATIVE_SUBMIT_CALL)
    out[o:o+5]=b'\xe8'+rel32(NATIVE_SUBMIT_CALL,5,MAIN_RVA)

    # Recompute PE checksum only after all surgical changes.
    opt,_=parse(out)
    csoff=opt+64
    struct.pack_into('<I',out,csoff,0)
    struct.pack_into('<I',out,csoff,checksum(out,csoff))

    h=sha(out)
    if h!=FINAL_SHA:
        raise RuntimeError('unexpected SAFEPOINT4 output SHA: '+h)
    dst.write_bytes(out)
    print('BASE_SHA256='+BASE_SHA)
    print('OUT_SHA256='+h)
    print('NATIVE_SUBMIT_CALL_RVA=0x%X'%NATIVE_SUBMIT_CALL)
    print('STATE_GUARD_MAIN_RVA=0x%X'%MAIN_RVA)
    print('STATE_SAVE_RVA=0x%X'%SAVE_RVA)
    print('STATE_RESTORE_RVA=0x%X'%RESTORE_RVA)
    print('STATE_RELEASE_RVA=0x%X'%RELEASE_RVA)
    print('DELTA=NATIVE_USR_B18K18_D3D11_IMMEDIATE_CONTEXT_STATE_SAVE_RESTORE')

if __name__=='__main__':
    main()

#!/usr/bin/env python3
"""Minimal GW16G unified-recorder patch.

Base: exact hardware-validated GW16G QUALITYSAFE1 runtime.
Changes only:
1) Correct HOTKEYSAFE2's CTRL+F8 consume action from VideoRecord ID 8 to Status ID 2,
   restoring the original CTRL+F9 VideoRecord action.
2) Remove the original B18K18 start-time FG-enabled rejection. The exact original
   recorder start/stop/HUD/QSV path is otherwise unchanged.
3) Restore the original action-8 VideoRecord dispatcher call directly to the original
   B18K18 recorder handler. GW16G had accidentally routed this call through the
   CTRL guard created for the F8 collision, so CTRL+F9 was suppressed while CTRL
   was physically held.
4) Remove the original B18K18 per-frame submission FG-enabled rejection. The exact
   same recorder submission function can therefore accept the already-final native
   presenter texture when FG is OFF as well as the REAL+GENERATED final texture when
   FG is ON. No external bridge or second recorder implementation exists.
"""
from pathlib import Path
import hashlib, struct, sys
BASE_SHA='8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d'
HOT_POLL_SAFE=0x34FDA10
HOT_ACTION_IMM_OFF=30
START_FG_JE_RVA=0x19EE3
SUBMIT_FG_JE_RVA=0x277AB
RECORDER_CALL_SITE=0x12304
RECORDER_HANDLER=0x19DC0
RECORDER_GW16G_GUARD=0x34FD9B0

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
    raise RuntimeError('RVA not mapped: %#x'%rva)
def checksum(blob,off):
    b=bytearray(blob);struct.pack_into('<I',b,off,0);s=0
    for i in range(0,len(b)-1,2):
        s+=b[i]|(b[i+1]<<8);s=(s&0xffff)+(s>>16)
    if len(b)&1:s+=b[-1]
    s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16)
    return (s+len(b))&0xffffffff

def main():
    if len(sys.argv)!=3:raise SystemExit('usage: patch_gw16g_unified_recorder.py GW16G_DLL OUT_DLL')
    src,dst=map(Path,sys.argv[1:]);base=src.read_bytes()
    if sha(base)!=BASE_SHA:raise RuntimeError('exact validated GW16G base required; got '+sha(base))
    if len(base)!=320000:raise RuntimeError('size lock failed')
    out=bytearray(base)
    hp=roff(base,HOT_POLL_SAFE)
    if base[hp+29:hp+34]!=bytes.fromhex('b908000000'):
        raise RuntimeError('HOTKEYSAFE2 action lock failed: '+base[hp+29:hp+34].hex())
    out[hp+HOT_ACTION_IMM_OFF]=2
    # HOTKEYMAPFIX2: action 8 is VideoRecord, and this dispatcher call must remain
    # the production direct call. GW16G mistakenly routed it through a CTRL guard,
    # which necessarily suppresses CTRL+F9 because CTRL is part of the chord.
    rc=roff(base,RECORDER_CALL_SITE)
    raw=base[rc:rc+5]
    if raw[0]!=0xE8 or RECORDER_CALL_SITE+5+struct.unpack_from('<i',raw,1)[0]!=RECORDER_GW16G_GUARD:
        raise RuntimeError('GW16G recorder dispatch guard lock failed: '+raw.hex())
    out[rc:rc+5]=b'\xE8'+struct.pack('<i',RECORDER_HANDLER-(RECORDER_CALL_SITE+5))
    s=roff(base,START_FG_JE_RVA)
    if base[s:s+2]!=bytes.fromhex('7455'):
        raise RuntimeError('recorder start FG gate lock failed: '+base[s:s+2].hex())
    out[s:s+2]=b'\x90\x90'
    q=roff(base,SUBMIT_FG_JE_RVA)
    if base[q:q+6]!=bytes.fromhex('0f8459040000'):
        raise RuntimeError('recorder submit FG gate lock failed: '+base[q:q+6].hex())
    out[q:q+6]=b'\x90'*6
    opt,_=parse(out);csoff=opt+64
    struct.pack_into('<I',out,csoff,0);struct.pack_into('<I',out,csoff,checksum(out,csoff))
    dst.write_bytes(out)
    print('BASE_SHA256='+BASE_SHA)
    print('OUT_SHA256='+sha(out))
    print('DELTA=HOTKEY_STATUS_CONSUME_2 + RESTORE_ACTION8_DIRECT_RECORDER_DISPATCH + NOP_ORIGINAL_RECORDER_FG_START_GATE + NOP_ORIGINAL_RECORDER_FG_SUBMIT_GATE + PE_CHECKSUM')
if __name__=='__main__':main()

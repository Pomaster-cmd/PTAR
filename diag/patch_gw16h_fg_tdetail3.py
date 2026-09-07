#!/usr/bin/env python3
import hashlib, struct, sys
from pathlib import Path
BASE_SHA='e7c1b14f263505a64442d16aa1088c43b04a81665641c159a3da1181267bac53'
OLD=b'k=saturate(1-max(e.x,e.y)/(9.5+.35*max(abs(v.x),abs(v.y))));k=k;;'
NEW=b'k=.8+saturate(1-max(e.x,e.y)/(9.5+.35*max(abs(v.x),abs(v.y))))/5;'

def sha(b): return hashlib.sha256(bytes(b)).hexdigest()
def parse(b):
    e=struct.unpack_from('<I',b,0x3c)[0]; coff=e+4
    n=struct.unpack_from('<H',b,coff+2)[0]; optsz=struct.unpack_from('<H',b,coff+16)[0]
    opt=coff+20; sh=opt+optsz; secs=[]
    for i in range(n):
        o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','ignore')
        vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8); secs.append((name,vs,va,rs,rp))
    return opt,secs
def checksum_offset(b): return parse(b)[0]+64
def checksum(blob,off):
    b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
    for i in range(0,len(b)-1,2):
        s+=b[i]|(b[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(b)&1:s+=b[-1]
    s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
    return (s+len(b))&0xffffffff

def main(src,dst):
    b=bytearray(Path(src).read_bytes())
    if sha(b)!=BASE_SHA: raise RuntimeError('base SHA lock failed: '+sha(b))
    if len(OLD)!=len(NEW): raise RuntimeError('source replacement length mismatch')
    cnt=bytes(b).count(OLD)
    if cnt!=1: raise RuntimeError(f'TDETAIL2 trust expression occurrence lock failed: {cnt}')
    p=bytes(b).find(OLD); b[p:p+len(OLD)]=NEW
    cso=checksum_offset(b); struct.pack_into('<I',b,cso,0); struct.pack_into('<I',b,cso,checksum(b,cso))
    Path(dst).write_bytes(b)
    print('OUT_SHA='+sha(b))
    print('TDETAIL3_TRUST_FILEOFF=%#x'%p)
    print('TDETAIL3_RULE=k_final=0.80+0.20*k_raw')
if __name__=='__main__': main(sys.argv[1],sys.argv[2])

#!/usr/bin/env python3
import hashlib, struct, sys
from pathlib import Path
BASE_SHA='50a329275f410bea032b3a50e81a7838f01f1ef22ee916ad4285df79d146ed92'
OLD_A=b'1.5+.35*max(abs(v.x),abs(v.y))'
NEW_A=b'9.5+.35*max(abs(v.x),abs(v.y))'
OLD_B=b'k*=k;'
NEW_B=b'k=k;;'
OLD_C=b'max(G,.02)'
NEW_C=b'max(G,.35)'

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

def one_replace(b,old,new,label):
    if len(old)!=len(new): raise RuntimeError(label+' length mismatch')
    cnt=bytes(b).count(old)
    if cnt!=1: raise RuntimeError(f'{label} occurrence lock failed: {cnt}')
    p=bytes(b).find(old); b[p:p+len(old)]=new; return p

def main(src,dst):
    b=bytearray(Path(src).read_bytes())
    if sha(b)!=BASE_SHA: raise RuntimeError('base SHA lock failed: '+sha(b))
    p1=one_replace(b,OLD_A,NEW_A,'Q3 denom')
    p2=one_replace(b,OLD_B,NEW_B,'Q3 square')
    p3=one_replace(b,OLD_C,NEW_C,'blend floor')
    cso=checksum_offset(b); struct.pack_into('<I',b,cso,0); struct.pack_into('<I',b,cso,checksum(b,cso))
    Path(dst).write_bytes(b)
    print('OUT_SHA='+sha(b))
    print('Q3_DENOM_FILEOFF=%#x'%p1)
    print('Q3_UNSQUARE_FILEOFF=%#x'%p2)
    print('BLEND_FLOOR_FILEOFF=%#x'%p3)
if __name__=='__main__': main(sys.argv[1],sys.argv[2])

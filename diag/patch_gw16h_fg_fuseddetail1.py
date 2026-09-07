#!/usr/bin/env python3
"""SAFEPOINT11/FUSEDDETAIL1: branch from SAFEPOINT8/TDETAIL4, replace only embedded FG HLSL source block + PE checksum."""
import hashlib,struct,sys
from pathlib import Path
BASE_SHA='613714f5ac70bc94867a3044dd067f4de18bb3ef65262c60f0febce2ca9d4c70'
OUT_SHA='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c'
BLOCK_OFF=0x34210; BLOCK_LEN=1200
NEXT_PREFIX=b'Texture2D<float4> Src:register'
def sha(b): return hashlib.sha256(bytes(b)).hexdigest()
def parse(b):
 e=struct.unpack_from('<I',b,0x3c)[0];coff=e+4;n=struct.unpack_from('<H',b,coff+2)[0];optsz=struct.unpack_from('<H',b,coff+16)[0];opt=coff+20;sh=opt+optsz;secs=[]
 for i in range(n):
  o=sh+i*40;name=b[o:o+8].rstrip(b'\0').decode('ascii','ignore');vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);secs.append((name,vs,va,rs,rp))
 return opt,secs
def csum(blob,off):
 b=bytearray(blob);struct.pack_into('<I',b,off,0);s=0
 for i in range(0,len(b)-1,2): s+=b[i]|(b[i+1]<<8);s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16);return (s+len(b))&0xffffffff
def main(src,dst):
 base=bytearray(Path(src).read_bytes()); new_src=(Path(__file__).with_name('FUSEDDETAIL1_SOURCE.hlsl')).read_bytes()
 if sha(base)!=BASE_SHA: raise RuntimeError('SAFEPOINT8 base SHA lock failed: '+sha(base))
 if len(new_src)>=BLOCK_LEN: raise RuntimeError('source too large: %d >= %d'%(len(new_src),BLOCK_LEN))
 if not bytes(base[BLOCK_OFF:BLOCK_OFF+64]).startswith(b'Texture2D<float4>P:register(t0);'): raise RuntimeError('FG source start lock failed')
 if bytes(base[BLOCK_OFF+BLOCK_LEN:BLOCK_OFF+BLOCK_LEN+len(NEXT_PREFIX)])!=NEXT_PREFIX: raise RuntimeError('next shader lock failed')
 old=bytes(base[BLOCK_OFF:BLOCK_OFF+BLOCK_LEN]).split(b'\0',1)[0]
 for token,n in [(b'x.Load(',4),(b'P.Load(',1),(b'C.Load(',1),(b'M.Load(',1)]:
  if old.count(token)!=n or new_src.count(token)!=n: raise RuntimeError('load-shape lock failed for %r'%token)
 if old.count(b'.Load(')!=7 or new_src.count(b'.Load(')!=7: raise RuntimeError('total Load token count changed')
 base[BLOCK_OFF:BLOCK_OFF+BLOCK_LEN]=new_src+b'\0'*(BLOCK_LEN-len(new_src))
 opt,_=parse(base);cso=opt+64;struct.pack_into('<I',base,cso,0);struct.pack_into('<I',base,cso,csum(base,cso))
 out=bytes(base)
 if sha(out)!=OUT_SHA: raise RuntimeError('output SHA mismatch: '+sha(out))
 Path(dst).write_bytes(out)
 print('OUT_SHA='+OUT_SHA);print('FG_SOURCE_BLOCK=0x%X+%d'%(BLOCK_OFF,BLOCK_LEN));print('SOURCE_BYTES=%d'%len(new_src));print('LOAD_TOKENS=7_UNCHANGED')
if __name__=='__main__': main(sys.argv[1],sys.argv[2])

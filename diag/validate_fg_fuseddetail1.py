#!/usr/bin/env python3
"""Static/reproducibility/semantic gate for SAFEPOINT11 FUSEDDETAIL1."""
from pathlib import Path
import hashlib,struct,subprocess,sys,tempfile,re
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve();D=ROOT/'diag';PAY=ROOT/'payload'
BASE_SHA='613714f5ac70bc94867a3044dd067f4de18bb3ef65262c60f0febce2ca9d4c70'; FINAL_SHA='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c'; BLOCK_OFF=0x34210;BLOCK_LEN=1200
checks=[]
def ck(n,c,d=''): checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(x): return hashlib.sha256(Path(x).read_bytes()).hexdigest() if isinstance(x,(str,Path)) else hashlib.sha256(bytes(x)).hexdigest()
def parse(b):
 e=struct.unpack_from('<I',b,0x3c)[0];coff=e+4;n=struct.unpack_from('<H',b,coff+2)[0];optsz=struct.unpack_from('<H',b,coff+16)[0];opt=coff+20;sh=opt+optsz;secs=[]
 for i in range(n):
  o=sh+i*40;name=b[o:o+8].rstrip(b'\0').decode('ascii','ignore');vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);secs.append((name,vs,va,rs,rp))
 return opt,secs
def csum(blob,off):
 b=bytearray(blob);struct.pack_into('<I',b,off,0);s=0
 for i in range(0,len(b)-1,2):s+=b[i]|(b[i+1]<<8);s=(s&0xffff)+(s>>16)
 if len(b)&1:s+=b[-1]
 s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16);return (s+len(b))&0xffffffff
base=ROOT/'diag/base/FUSEDDETAIL1_SAFEPOINT8_BASE.dll'; final=PAY/'win81_nis_dx11_x64.dll'; mirror=PAY/'d3d11.dll'; src=D/'FUSEDDETAIL1_SOURCE.hlsl'; patch=D/'patch_gw16h_fg_fuseddetail1.py'
ck('SAFEPOINT8 branch base present',base.is_file()); ck('SAFEPOINT8 branch base exact',base.is_file() and sha(base)==BASE_SHA,sha(base) if base.is_file() else 'missing')
ck('source present',src.is_file()); ck('patch present',patch.is_file()); ck('final runtime exact',final.is_file() and sha(final)==FINAL_SHA,sha(final) if final.is_file() else 'missing'); ck('mirror exact',mirror.is_file() and mirror.read_bytes()==final.read_bytes())
B=base.read_bytes();F=final.read_bytes();S=src.read_bytes(); ck('runtime size unchanged',len(B)==len(F)==320000,(len(B),len(F))); ck('source fits reserved block',len(S)<BLOCK_LEN,len(S))
# Reproducibility.
with tempfile.TemporaryDirectory() as td:
 out=Path(td)/'out.dll';cp=subprocess.run([sys.executable,str(patch),str(base),str(out)],capture_output=True,text=True);ck('patch executes',cp.returncode==0,(cp.stdout+cp.stderr).strip().replace('\n',' | '));ck('patch reproduces payload',out.is_file() and out.read_bytes()==F,sha(out) if out.is_file() else 'missing')
# PE/delta boundary.
opt0,sec0=parse(B);opt1,sec1=parse(F);ck('section topology unchanged',sec0==sec1);cso=opt1+64;stored=struct.unpack_from('<I',F,cso)[0];ck('PE checksum exact',stored==csum(F,cso),hex(stored))
changed={i for i,(a,b) in enumerate(zip(B,F)) if a!=b};func=changed-set(range(cso,cso+4));ck('all functional delta inside FG source block',bool(func) and all(BLOCK_OFF<=i<BLOCK_OFF+BLOCK_LEN for i in func),(len(func),hex(min(func)),hex(max(func))));ck('no delta outside FG block + PE checksum',all((BLOCK_OFF<=i<BLOCK_OFF+BLOCK_LEN) or (cso<=i<cso+4) for i in changed),len(changed))
ck('next embedded shader byte-identical',B[BLOCK_OFF+BLOCK_LEN:]==F[BLOCK_OFF+BLOCK_LEN:]); ck('pre-FG region byte-identical except checksum',B[cso+4:BLOCK_OFF]==F[cso+4:BLOCK_OFF])
# Source block and bindings.
emb=F[BLOCK_OFF:BLOCK_OFF+BLOCK_LEN].split(b'\0',1)[0];ck('embedded source exact',emb==S);ck('NUL terminator inside reservation',F[BLOCK_OFF+len(S)]==0);ck('next shader prefix intact',F[BLOCK_OFF+BLOCK_LEN:BLOCK_OFF+BLOCK_LEN+30].startswith(b'Texture2D<float4> Src:register'))
for tok in [b'P:register(t0)',b'C:register(t1)',b'M:register(t2)',b'O:register(u0)',b'X:register(b0)',b'[numthreads(8,8,1)]']: ck('binding/thread token '+tok.decode('ascii','ignore'),emb.count(tok)==1,emb.count(tok))
for tok,n in [(b'x.Load(',4),(b'P.Load(',1),(b'C.Load(',1),(b'M.Load(',1),(b'.Load(',7)]: ck('load token unchanged '+tok.decode(),emb.count(tok)==n,emb.count(tok))
old=B[BLOCK_OFF:BLOCK_OFF+BLOCK_LEN].split(b'\0',1)[0];ck('total Load tokens same as TDETAIL4',old.count(b'.Load(')==emb.count(b'.Load(')==7,(old.count(b'.Load('),emb.count(b'.Load(')))
ck('no Sample token added',b'.Sample' not in emb);ck('no Gather token added',b'.Gather' not in emb);ck('same resource declarations count',sum(emb.count(x) for x in [b'register(t0)',b'register(t1)',b'register(t2)',b'register(u0)',b'register(b0)'])==5)
# Existing TDETAIL4 behavior retained.
for tok in [b'9.5+.35*max(abs(v.x),abs(v.y))',b'k=.8+saturate(',b'max(G,.25)',b'if(G<.3)']: ck('TDETAIL4 token retained '+tok.decode(),tok in emb)
# Fused detail features.
for tok in [b'r.a=max(max(A.g,B.g),max(Q.g,D.g))-min(min(A.g,B.g),min(Q.g,D.g))',b'abs(o.g-f.g)',b'(a.a+b.a)*.38+.01+10*(G-.25)',b'o=lerp(f,o,.3+.7*z)',b'o.a=f.a']: ck('FUSEDDETAIL token '+tok.decode(),tok in emb)
# Mathematical safety probes for normalized channels.
def scale(g,ar,br,dev):
 z=min(1.0,((ar+br)*.38+.01+10*(g-.25))/(abs(dev)+1e-4));return .3+.7*z
# QUALITY: worst-case endpoint local range 0, max normalized green deviation 1.
for dev in [0,0.25,0.5,0.75,1.0]: ck('QUALITY bypass dev %.2f'%dev,abs(scale(.35,0,0,dev)-1)<1e-12,scale(.35,0,0,dev))
for g in [.50,.65]: ck('larger guard profiles bypass',abs(scale(g,0,0,1)-1)<1e-12,scale(g,0,0,1))
# Profile 3: scale bounded and monotone in local-detail budget; stable deviation gets no correction.
vals=[scale(.25,0,0,d) for d in [0,.005,.01,.05,.2,1]];ck('profile3 scale bounded 0.30..1',all(.3<=x<=1 for x in vals),vals);ck('stable profile3 dev zero bypass',abs(scale(.25,0,0,0)-1)<1e-12,scale(.25,0,0,0));ck('profile3 correction only attenuates',all(x<=1 for x in vals),vals);ck('more endpoint detail never increases attenuation',scale(.25,.2,.2,.5)>=scale(.25,0,0,.5),(scale(.25,.2,.2,.5),scale(.25,0,0,.5)))
# Source syntax sanity: balanced delimiters and no control bytes.
txt=emb.decode('ascii');ck('ASCII source',txt.encode('ascii')==emb);ck('balanced braces',txt.count('{')==txt.count('}'),(txt.count('{'),txt.count('}')));ck('balanced parens',txt.count('(')==txt.count(')'),(txt.count('('),txt.count(')')));ck('no newline/control bytes',all(ord(c)>=32 for c in txt))
# Documentation identity.
ver=(PAY/'win81_nis_version.txt').read_text(errors='replace');ini=(PAY/'win81_nis.ini').read_text(errors='replace');readme=(ROOT/'README_TEST.txt').read_text(errors='replace')
for tok in ['SAFEPOINT11=FUSEDDETAIL1','FG_FUSEDDETAIL1_TEXTURE_LOAD_TOKEN_COUNT=7_UNCHANGED','FG_FUSEDDETAIL1_NEW_RESOURCE_BINDINGS=ZERO','FG_FUSEDDETAIL1_NEW_DISPATCH=ZERO','FG_FUSEDDETAIL1_QUALITY_PROFILE2=MATHEMATICALLY_BYPASSED']:ck('version '+tok,tok in ver)
for tok in ['CONSERVATIVE/FUSEDDETAIL1','No added texture-load token','Profile 2 QUALITY is mathematically bypassed']:ck('INI '+tok,tok.lower() in ini.lower())
for tok in ['no new texture resource','no new Dispatch','source-level .Load token count remains exactly 7','QUALITY remains mathematically unchanged']:ck('README '+tok,tok.lower() in readme.lower())
failed=[x for x in checks if not x[1]];print('FG_FUSEDDETAIL1_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
 for n,_,d in failed:print('FAILED',n,d)
 raise SystemExit(1)

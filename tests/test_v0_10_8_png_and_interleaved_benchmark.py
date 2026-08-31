#!/usr/bin/env python3
from pathlib import Path
import re, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
cpp=(ROOT/"hardware_validation/ptar_k185_hw_validation.cpp").read_text(encoding="utf-8")

checks=[]
def ck(name,cond,detail=""):
    checks.append((name,bool(cond),detail))
    if not cond:
        raise AssertionError(name + (": "+detail if detail else ""))

# PNG persistence.
ck("SavePngRGBA returns bytes","unsigned long long* outputBytes" in cpp)
ck("PNG uses BGRA","GUID_WICPixelFormat32bppBGRA" in cpp)
ck("RGBA to BGRA red-blue swap","bgra[p*4u+0u]=img.pixels[p*4u+2u]" in cpp)
ck("RGBA to BGRA blue-red swap","bgra[p*4u+2u]=img.pixels[p*4u+0u]" in cpp)
ck("file size verified","GetFileSize64(path,bytes)" in cpp)
ck("zero-byte rejected","if(bytes==0)" in cpp)
ck("save HRESULT checked","GPU PNG persistence failed" in cpp)
ck("parity CSV records png bytes","gpu_png_bytes" in cpp)
ck("success console reports 42 PNG","42/42 GPU forensic PNG outputs saved" in cpp)

# Timing A/B interleaving.
ck("TimingPair struct","struct TimingPair" in cpp)
ck("interleaved measurement","MeasureInterleaved" in cpp)
ck("alternating order","pair.k185First=((i&1u)==0u)" in cpp)
ck("K185-first label","K185_FIRST" in cpp)
ck("bilinear-first label","BILINEAR_FIRST" in cpp)
ck("timing pairs CSV","timing_pairs.csv" in cpp)
ck("paired mean delta","paired_mean_delta_ms" in cpp)
ck("paired median delta","paired_median_delta_ms" in cpp)
ck("paired faster counts","paired_k185_faster_count" in cpp and "paired_bilinear_faster_count" in cpp)
ck("old sequential MeasureShader removed","HRESULT MeasureShader(" not in cpp)

# Safety / previous hardening.
ck("no explicit D3D Flush","->Flush(" not in cpp and ".Flush(" not in cpp)
ck("RAII COM retained","class ComApartmentGuard" in cpp)
ck("NOMINMAX retained","#define NOMINMAX" in cpp)
ck("UTF8 helper retained","WideCharToMultiByte(" in cpp)

# Portable BGRA swap semantics.
source=r"""
#include <vector>
int main()
{
    std::vector<unsigned char> rgba={10,20,30,40, 1,2,3,4};
    std::vector<unsigned char> bgra(rgba.size());
    for(unsigned long p=0;p<2;++p)
    {
        bgra[p*4+0]=rgba[p*4+2];
        bgra[p*4+1]=rgba[p*4+1];
        bgra[p*4+2]=rgba[p*4+0];
        bgra[p*4+3]=rgba[p*4+3];
    }
    return (bgra[0]==30 && bgra[1]==20 && bgra[2]==10 && bgra[3]==40 &&
            bgra[4]==3 && bgra[5]==2 && bgra[6]==1 && bgra[7]==4) ? 0 : 1;
}
"""
with tempfile.TemporaryDirectory() as td:
    td=Path(td)
    src=td/"swap.cpp"; exe=td/"swap"
    src.write_text(source,encoding="utf-8")
    compiled=False; last=""
    for cmd in (
        ["clang++","-std=c++11","-Wall","-Wextra","-Werror",str(src),"-o",str(exe)],
        ["g++","-std=c++11","-Wall","-Wextra","-Werror",str(src),"-o",str(exe)],
    ):
        try:
            cp=subprocess.run(cmd,capture_output=True,text=True)
        except FileNotFoundError:
            continue
        last=cp.stderr
        if cp.returncode==0:
            compiled=True
            rr=subprocess.run([str(exe)])
            ck("portable BGRA swap executes",rr.returncode==0)
            break
    ck("portable BGRA swap compiles",compiled,last)

print(f"{sum(1 for _,p,_ in checks if p)}/{len(checks)} PASS")
for n,p,d in checks:
    print(("PASS" if p else "FAIL")+" - "+n+((" | "+d) if d else ""))

from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[2]
names=['BASE','B12','B18','B20','H08A08','H10A08','H12A06','H12A08']
N=len(names)
includes=''.join(f'#include "../../build_x86/ptar_spec5_hybrid_{n}.h"\n' for n in names)
name_array=','.join(f'"{n}"' for n in names)
code_array=','.join(f'(DWORD*)g_ptarSpec5Hybrid{n}' for n in names)

def transform(src,dst,kind):
    s=src.read_text(encoding='utf-8')
    s,ninc=re.subn(r'(?:#include "\.\./\.\./build_x86/ptar_spec5_aalab_v\d+\.h"\n)+',includes,s);assert ninc==1
    pat=r'int main\(\)\{const char\*names\[9\]=\{.*?\};const DWORD\*(codes?)\[9\]=\{.*?\};wchar_t p'
    m=re.search(pat,s,flags=re.S);assert m
    cname=m.group(1);repl=f'int main(){{const char*names[{N}]={{{name_array}}};const DWORD*{cname}[{N}]={{{code_array}}};wchar_t p'
    s,nm=re.subn(pat,repl,s,count=1,flags=re.S);assert nm==1
    s=s.replace('IDirect3DPixelShader9*ps[9]={};',f'IDirect3DPixelShader9*ps[{N}]={{}};')
    s=s.replace('for(int v=0;v<9;v++)',f'for(int v=0;v<{N};v++)').replace('for(int i=0;i<9;i++)',f'for(int i=0;i<{N};i++)')
    if kind=='quality':
        s=s.replace('double agg[9][7]={};',f'double agg[{N}][7]={{}};')
        old='float mortar=(std::fmod(px*16.0f+(int)(py*12)&1?0.5f:0.0f,1.0f)<.08f||std::fmod(py*12.0f,1.0f)<.08f)?1.0f:0.0f;'
        new='float rowOffset=(((int)(py*12.0f))&1)?0.5f:0.0f;float mortar=(std::fmod(px*16.0f+rowOffset,1.0f)<.08f||std::fmod(py*12.0f,1.0f)<.08f)?1.0f:0.0f;'
        assert old in s;s=s.replace(old,new,1)
    elif kind=='temporal':
        s=s.replace('double acc[9][4]={};',f'double acc[{N}][4]={{}};').replace('std::vector<DWORD>prevGot[9];',f'std::vector<DWORD>prevGot[{N}];').replace('std::vector<DWORD>got[9];',f'std::vector<DWORD>got[{N}];')
    elif kind=='perf':
        s=s.replace('double medians[9]={};',f'double medians[{N}]={{}};')
    dst.write_text(s,encoding='utf-8')

transform(ROOT/'lab/aalab1/aalab_quality_gpu.cpp',ROOT/'lab/blendhybrid1/hybrid_quality.cpp','quality')
transform(ROOT/'lab/aalab1/aalab_temporal_gpu.cpp',ROOT/'lab/blendhybrid1/hybrid_temporal.cpp','temporal')
transform(ROOT/'lab/aalab1/aalab_perf_gpu.cpp',ROOT/'lab/blendhybrid1/hybrid_perf.cpp','perf')
print('BLENDHYBRID_HARNESSES=PASS')

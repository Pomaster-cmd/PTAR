from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
strengths=[0,40,60,80,100,120,140,160,180,200,240,280,320]
names=[f'B{s//10:02d}' for s in strengths]

includes=''.join(f'#include "../../build_x86/ptar_spec5_blend_{n}.h"\n' for n in names)
name_array=','.join(f'"{n}"' for n in names)
code_array=','.join(f'(DWORD*)g_ptarSpec5Blend{n}' for n in names)
N=len(names)


def transform(src_path:Path,dst_path:Path,temporal:bool):
    s=src_path.read_text(encoding='utf-8')
    s,ninc=re.subn(r'(?:#include "\.\./\.\./build_x86/ptar_spec5_aalab_v\d+\.h"\n)+',includes,s)
    assert ninc==1,ninc
    pattern=r'int main\(\)\{const char\*names\[9\]=\{.*?\};const DWORD\*codes?\[9\]=\{.*?\};wchar_t p'
    repl=f'int main(){{const char*names[{N}]={{{name_array}}};const DWORD*codes[{N}]={{{code_array}}};wchar_t p'
    s,nmain=re.subn(pattern,repl,s,count=1,flags=re.S)
    assert nmain==1,nmain
    s=s.replace('IDirect3DPixelShader9*ps[9]={};',f'IDirect3DPixelShader9*ps[{N}]={{}};')
    s=s.replace('for(int v=0;v<9;v++)',f'for(int v=0;v<{N};v++)')
    s=s.replace('for(int i=0;i<9;i++)',f'for(int i=0;i<{N};i++)')
    if temporal:
        s=s.replace('double acc[9][4]={};',f'double acc[{N}][4]={{}};')
        s=s.replace('std::vector<DWORD>prevGot[9];',f'std::vector<DWORD>prevGot[{N}];')
        s=s.replace('std::vector<DWORD>got[9];',f'std::vector<DWORD>got[{N}];')
    else:
        s=s.replace('double agg[9][7]={};',f'double agg[{N}][7]={{}};')
        # Fix the generator-only pavement expression exactly as AALAB1 did.
        old='float mortar=(std::fmod(px*16.0f+(int)(py*12)&1?0.5f:0.0f,1.0f)<.08f||std::fmod(py*12.0f,1.0f)<.08f)?1.0f:0.0f;'
        new='float rowOffset=(((int)(py*12.0f))&1)?0.5f:0.0f;float mortar=(std::fmod(px*16.0f+rowOffset,1.0f)<.08f||std::fmod(py*12.0f,1.0f)<.08f)?1.0f:0.0f;'
        assert old in s
        s=s.replace(old,new,1)
    assert '[9]' not in s
    dst_path.write_text(s,encoding='utf-8')

transform(ROOT/'lab/aalab1/aalab_quality_gpu.cpp',ROOT/'lab/blendrefine1/blend_quality_generated.cpp',False)
transform(ROOT/'lab/aalab1/aalab_temporal_gpu.cpp',ROOT/'lab/blendrefine1/blend_temporal_generated.cpp',True)
print(f'BLENDREFINE_HARNESSES=PASS variants={N}')

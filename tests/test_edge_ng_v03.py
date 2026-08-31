#!/usr/bin/env python3
import json,re,sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src/reference/edge_ng_v03'))
from edge_ng_v03_k185_renderer import W13,W23,A
checks=[]
def ck(n,c,d=''): checks.append((n,bool(c),d)); assert c,n+(': '+d if d else '')
ck('K185 a fixed',abs(A+1.85)<1e-15)
ck('W13 sum1',abs(float(W13.sum())-1.0)<1e-12)
ck('W23 sum1',abs(float(W23.sum())-1.0)<1e-12)
ck('phase mirror',np.max(np.abs(W13-W23[::-1]))<1e-15)
h=(ROOT/'src/hlsl/edge_ng_v03/ptar_edge_ng_v03_k185_ps.hlsl').read_text();u=h.upper()
ck('one GatherGreen',h.count('.GatherGreen(')==1)
ck('four SampleLevel',h.count('.SampleLevel(')==4)
ck('no NIS','NIS' not in u)
ck('no UAV','RWTEXTURE' not in u)
ck('one source Texture2D',h.count('Texture2D')==1)
ck('phase mod3','%3u' in h)
sel=json.loads((ROOT/'benchmarks/results/current/EDGE_NG_V03_K185/K185_SELECTION.json').read_text())
ck('selected -1.85',abs(sel['selected']['a']+1.85)<1e-12)
cmp=json.loads((ROOT/'benchmarks/results/current/EDGE_NG_V03_K185/EDGE_CORE_COMPARISON.json').read_text())
ck('beats G5 SSIM',cmp['delta_K185_vs_G5']['mean_ssim']>0)
ck('beats G5 PSNR',cmp['delta_K185_vs_G5']['mean_psnr_db']>0)
ck('beats cubic4 SSIM',cmp['delta_K185_vs_cubic4']['mean_ssim']>0)
ck('beats cubic4 PSNR',cmp['delta_K185_vs_cubic4']['mean_psnr_db']>0)
par=json.loads((ROOT/'benchmarks/results/current/EDGE_NG_V03_K185/CPU_SHADER_SEMANTIC_PARITY_SUMMARY.json').read_text())
ck('semantic <=1 LSB',par['max_abs_lsb']<=1)
ck('semantic PASS',par['software_gate']=='PASS')
t=(ROOT/'runtime_integration/d3d11/PTARD3D11GpuTimerRing.h').read_text()
ck('timestamp disjoint','D3D11_QUERY_TIMESTAMP_DISJOINT' in t)
ck('DONOTFLUSH','D3D11_ASYNC_GETDATA_DONOTFLUSH' in t)
ck('no Flush call','->Flush(' not in t and '.Flush(' not in t)
b=(ROOT/'build/windows/BUILD_EDGE_NG_V03_K185_SM5.bat').read_text()
ck('FXC ps5','/T ps_5_0' in b);ck('FXC vs5','/T vs_5_0' in b);ck('strict','/Ges /WX' in b)
ck('non destructive',not re.search(r'(?im)^\s*(del|erase|rd|rmdir)\b',b))
print(f'{sum(c for _,c,_ in checks)}/{len(checks)} PASS')
for n,c,d in checks: print(('PASS' if c else 'FAIL')+' - '+n+((' | '+d) if d else ''))

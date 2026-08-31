#!/usr/bin/env python3
from pathlib import Path
import csv,json,re
ROOT=Path(__file__).resolve().parents[1]
h=(ROOT/'src/hlsl/moe_ng_v01/ptar_moe_ng_v01_sf5_ps.hlsl').read_text(encoding='utf-8')
s=json.loads((ROOT/'benchmarks/results/current/MOE_NG_V01_SF5/SUMMARY.json').read_text())
p=list(csv.DictReader((ROOT/'benchmarks/results/current/MOE_NG_V01_SF5/CPU_FLOAT64_VS_SHADER_FLOAT32_PARITY.csv').open()))
checks=[]
def ck(n,c):
    checks.append((n,bool(c)))
    if not c: raise AssertionError(n)
ck('one GatherGreen',h.count('.GatherGreen(')==1)
ck('four SampleLevel',h.count('.SampleLevel(')==4)
ck('one source Texture2D',len(re.findall(r'\bTexture2D<',h))==1)
ck('no UAV','RWTexture' not in h and 'RWBuffer' not in h and 'AppendStructuredBuffer' not in h)
ck('no NIS symbols','NVScaler' not in h and 'NISConfig' not in h and 'NVScalerUpdateConfig' not in h)
ck('K185 exact coefficients','-0.274074074074074f' in h and '0.877777777777778f' in h)
ck('N70','lerp(bilinear,raster,0.70f)' in h)
ck('soft convex weights','naturalWeight' in h and 'edgeWeight' in h and 'rasterWeight' in h)
ck('42 float64 outputs',len(list((ROOT/'benchmarks/results/current/MOE_NG_V01_SF5/MOE_FLOAT64_OUTPUTS').glob('*.png')))==42)
ck('42 semantic outputs',len(list((ROOT/'benchmarks/results/current/MOE_NG_V01_SF5/MOE_SEMANTIC_FLOAT32_OUTPUTS').glob('*.png')))==42)
ck('semantic <=1 LSB',max(int(r['max_abs_lsb']) for r in p)<=1)
ck('full42 SSIM improves',s['full_42']['delta_ssim']>0.004)
ck('full42 PSNR improves',s['full_42']['delta_psnr_db']>0.4)
ck('EDGE_CORE improves',s['edge_core']['delta_ssim']>0.0005)
ck('RASTER improves',s['raster_scope']['delta_ssim']>0.01)
ck('NATURAL improves',s['natural_scope']['delta_ssim']>0.0015)
ck('historical equivalence false',s['historical_equivalence'] is False)
print(f'{sum(v for _,v in checks)}/{len(checks)} PASS')
for n,v in checks: print(('PASS' if v else 'FAIL')+' - '+n)

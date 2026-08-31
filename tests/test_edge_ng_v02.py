#!/usr/bin/env python3
import re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];p=ROOT/'src/hlsl/edge_ng_v02/ptar_edge_ng_v02_g5_ps.hlsl';s=p.read_text();checks=[]
def ck(n,c):checks.append((n,bool(c)));assert c,n
ck('one GatherGreen source call',s.count('.GatherGreen(')==1)
ck('four SampleLevel source calls',s.count('.SampleLevel(')==4)
ck('ps has SV_Target','SV_Target' in s)
ck('phase mod3','%3u' in s)
ck('no NIS token','NIS' not in s.upper())
ck('no UAV','RWTexture' not in s)
ck('no extra render resource','gSource' in s and s.count('Texture2D')==1)
bat=(ROOT/'build/windows/BUILD_EDGE_NG_V02_G5_SM5.bat').read_text();ck('FXC ps5','/T ps_5_0' in bat);ck('strict FXC','/Ges /WX' in bat);ck('non destructive',not re.search(r'(?im)^\s*(del|erase|rd|rmdir)\b',bat))
print(f'{sum(c for _,c in checks)}/{len(checks)} PASS')
for n,c in checks:print(('PASS' if c else 'FAIL')+' - '+n)

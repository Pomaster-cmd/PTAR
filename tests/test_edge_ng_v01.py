#!/usr/bin/env python3
import sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src/reference/edge_ng_v01'))
from edge_ng_v01_renderer import render_edge_ng_v01
checks=[]
def ck(n,c):
 checks.append((n,bool(c)))
 if not c: raise AssertionError(n)
a=np.full((12,12,3),0.37,dtype=np.float64)
for m in ('BASELINE','SW1','SW2','SW3'):
 for e in (1e-8,1e-3,1e-1):
  o,_=render_edge_ng_v01(a,m,e); ck(f'constant {m} {e}',np.max(np.abs(o-0.37))<1e-12); ck(f'shape {m} {e}',o.shape==(18,18,3))
rng=np.random.default_rng(0x50544152); a=rng.random((16,16,3));o,_=render_edge_ng_v01(a,'SW3',1e-3)
ck('finite',np.isfinite(o).all());ck('bounded',float(o.min())>=0 and float(o.max())<=1)
print(f'{sum(c for _,c in checks)}/{len(checks)} PASS')
for n,c in checks: print(('PASS' if c else 'FAIL')+' - '+n)

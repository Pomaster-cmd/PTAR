#!/usr/bin/env python3
import csv,json,hashlib,sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from ptar_grid_resampling_v1 import lanczos_downsample_matrix,downsample_grid_lanczos3,resize_nearest_grid,resize_bilinear_grid,resize_cubic4_separable_grid
C=ROOT/'corpus/current/PTAR_PERCEPTUAL_V1_B_GRID'
def sha(p):
 h=hashlib.sha256();
 with open(p,'rb') as f:
  for c in iter(lambda:f.read(1048576),b''):h.update(c)
 return h.hexdigest()
checks=[]
def ck(n,c):checks.append((n,bool(c)));assert c,n
meta=json.loads((C/'metadata.json').read_text());ck('not historical',meta['historical_equivalence'] is False);ck('42 cases',meta['case_count']==42);ck('scale 1.5',meta['scale']==1.5)
W=lanczos_downsample_matrix(288,192);ck('weight rows sum 1',np.max(np.abs(W.sum(1)-1))<1e-12)
a=np.full((288,288,3),0.37);lr=downsample_grid_lanczos3(a);ck('down constant',np.max(np.abs(lr-.37))<1e-12)
for fn,n in [(resize_nearest_grid,'near'),(resize_bilinear_grid,'bil'),(resize_cubic4_separable_grid,'cub')]:o=fn(np.full((192,192,3),.37));ck(n+' constant',np.max(np.abs(o-.37))<1e-12);ck(n+' shape',o.shape==(288,288,3))
rows=list(csv.DictReader((C/'CORPUS_MANIFEST.csv').open())) ;ck('manifest 42',len(rows)==42)
for r in rows:ck('ref hash '+r['case_id'],sha(C/r['reference_path'])==r['sha256_reference']);ck('input hash '+r['case_id'],sha(C/r['input_path'])==r['sha256_input'])
print(f'{sum(c for _,c in checks)}/{len(checks)} PASS')

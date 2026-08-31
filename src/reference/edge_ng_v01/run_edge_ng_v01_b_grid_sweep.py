#!/usr/bin/env python3
import csv,json,math,sys,shutil
from pathlib import Path
import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'src/reference/edge_ng_v01'))
from edge_ng_v01_renderer import render_edge_ng_v01
C=ROOT/'corpus/current/PTAR_PERCEPTUAL_V1_B_GRID';OUT=ROOT/'benchmarks/results/current/EDGE_NG_V01_B_GRID_SWEEP';FINAL=ROOT/'benchmarks/results/current/EDGE_NG_V01_B_GRID_FINAL'
for d in (OUT,FINAL):
 if d.exists():shutil.rmtree(d)
 d.mkdir(parents=True)
EDGE={'silhouette_edge','thin_oblique_edges','ui_text','natural_edges','curves_high_frequency'};EPS=[1e-6,1e-4,1e-3,1e-2,1e-1]
def load(p):return np.asarray(Image.open(p).convert('RGB'),float)/255
def metrics(a,b):
 mse=float(np.mean((a-b)**2));ps=10*math.log10(1/mse);ss=float(structural_similarity(a,b,data_range=1,channel_axis=2,gaussian_weights=True,sigma=1.5,use_sample_covariance=False));return ps,ss
allc=list(csv.DictReader((C/'CORPUS_MANIFEST.csv').open()));tune=[c for c in allc if c['family'] in EDGE];rows=[]
for c in tune:
 lr=load(C/c['input_path']);ref=load(C/c['reference_path'])
 for m,epses in [('BASELINE',[1e-3]),('SW1',EPS),('SW2',EPS),('SW3',EPS)]:
  for e in epses:
   o,meta=render_edge_ng_v01(lr,m,e);ps,ss=metrics(ref,o);rows.append({**meta,'case_id':c['case_id'],'family':c['family'],'mode':m,'epsilon':'NA' if m=='BASELINE' else e,'psnr_db':ps,'ssim':ss})
with (OUT/'per_case.csv').open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=list(rows[0].keys()));w.writeheader();w.writerows(rows)
agg=[]
for m,e in [('BASELINE','NA')]+[(m,e) for m in ('SW1','SW2','SW3') for e in EPS]:
 rr=[r for r in rows if r['mode']==m and str(r['epsilon'])==str(e)]
 if rr:agg.append({'mode':m,'epsilon':e,'cases':len(rr),'mean_psnr_db':sum(x['psnr_db'] for x in rr)/len(rr),'mean_ssim':sum(x['ssim'] for x in rr)/len(rr),'min_ssim':min(x['ssim'] for x in rr)})
best={m:max([r for r in agg if r['mode']==m],key=lambda r:(r['mean_ssim'],r['mean_psnr_db'])) for m in ('SW1','SW2','SW3')};base=next(r for r in agg if r['mode']=='BASELINE');rank=sorted([base,*best.values()],key=lambda r:(r['mean_ssim'],r['mean_psnr_db']),reverse=True)
summary={'protocol':'EDGE_NG_V01_B_GRID_SWEEP','historical_equivalence':False,'corpus_id':'PTAR-PERCEPTUAL-V1-B-GRID','tuning_case_count':len(tune),'epsilon_candidates':EPS,'best_per_mode':best,'ranking':rank}
(OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
# final all-case selected outputs
manifest=[]
for m in ('BASELINE','SW1','SW2','SW3'):
 e=1e-3 if m=='BASELINE' else float(best[m]['epsilon']);vd=FINAL/m;vd.mkdir(exist_ok=True)
 for c in allc:
  lr=load(C/c['input_path']);o,_=render_edge_ng_v01(lr,m,e);p=vd/f"{c['case_id']}.png";Image.fromarray(np.rint(o*255).astype(np.uint8),'RGB').save(p,compress_level=3);manifest.append({'case_id':c['case_id'],'series':c['family'],'reference_path':str((C/c['reference_path']).resolve()),'variant':f'EDGE_NG_V01_{m}_eps{e:g}','output_path':str(p.resolve())})
with (FINAL/'BENCHMARK_MANIFEST.csv').open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=list(manifest[0].keys()));w.writeheader();w.writerows(manifest)
(FINAL/'SELECTION.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))

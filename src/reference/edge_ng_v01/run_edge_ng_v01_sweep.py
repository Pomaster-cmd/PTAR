#!/usr/bin/env python3
import csv,json,math,sys,shutil
from pathlib import Path
import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'src/reference/edge_ng_v01'))
from edge_ng_v01_renderer import render_edge_ng_v01

CORPUS=ROOT/'corpus/current/PTAR_PERCEPTUAL_V1_A'
MANIFEST=CORPUS/'CORPUS_MANIFEST.csv'
OUT=ROOT/'benchmarks/results/current/EDGE_NG_V01_SWEEP'
FINAL=ROOT/'benchmarks/results/current/EDGE_NG_V01_FINAL'
for d in (OUT,FINAL):
    if d.exists(): shutil.rmtree(d)
    d.mkdir(parents=True,exist_ok=True)

EPS=[1e-6,1e-4,1e-3,1e-2,1e-1]
EDGE_FAMILIES={'silhouette_edge','thin_oblique_edges','ui_text','natural_edges','curves_high_frequency'}

def load(path):
    with Image.open(path) as im: return np.asarray(im.convert('RGB'),dtype=np.float64)/255.0

def psnr(a,b):
    mse=float(np.mean((a-b)**2))
    return float('inf') if mse==0 else 10*math.log10(1.0/mse)

def ssim(a,b):
    return float(structural_similarity(a,b,data_range=1.0,channel_axis=2,gaussian_weights=True,sigma=1.5,use_sample_covariance=False))

with MANIFEST.open(newline='',encoding='utf-8') as f: all_cases=list(csv.DictReader(f))
tune_cases=[c for c in all_cases if c['family'] in EDGE_FAMILIES]
if len(tune_cases)!=15:
    raise SystemExit(f'expected 15 EDGE_CORE cases, got {len(tune_cases)}')

rows=[]
for i,c in enumerate(tune_cases,1):
    lr=load(CORPUS/c['input_path']); ref=load(CORPUS/c['reference_path'])
    print(f'[TUNE {i:02d}/{len(tune_cases)}] {c["case_id"]}',flush=True)
    base,meta=render_edge_ng_v01(lr,'BASELINE',1e-3)
    rows.append({**meta,'case_id':c['case_id'],'family':c['family'],'mode':'BASELINE','epsilon':'NA',
                 'psnr_db':psnr(ref,base),'ssim':ssim(ref,base)})
    for mode in ('SW1','SW2','SW3'):
        for eps in EPS:
            out,meta=render_edge_ng_v01(lr,mode,eps)
            rows.append({**meta,'case_id':c['case_id'],'family':c['family'],'mode':mode,'epsilon':eps,
                         'psnr_db':psnr(ref,out),'ssim':ssim(ref,out)})

with (OUT/'per_case_edge_core.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0].keys()));w.writeheader();w.writerows(rows)

agg=[]
keys=[('BASELINE','NA')]+[(m,e) for m in ('SW1','SW2','SW3') for e in EPS]
for mode,eps in keys:
    rr=[r for r in rows if r['mode']==mode and str(r['epsilon'])==str(eps)]
    if not rr: continue
    agg.append({'scope':'EDGE_CORE','mode':mode,'epsilon':eps,'cases':len(rr),
                'mean_psnr_db':sum(r['psnr_db'] for r in rr)/len(rr),
                'mean_ssim':sum(r['ssim'] for r in rr)/len(rr),
                'min_ssim':min(r['ssim'] for r in rr),
                'mean_edge_confidence':sum(r['mean_edge_confidence'] for r in rr)/len(rr)})
with (OUT/'aggregate_edge_core.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=list(agg[0].keys()));w.writeheader();w.writerows(agg)

best={}
for mode in ('SW1','SW2','SW3'):
    rr=[r for r in agg if r['mode']==mode]
    best[mode]=max(rr,key=lambda r:(r['mean_ssim'],r['mean_psnr_db']))
base=next(r for r in agg if r['mode']=='BASELINE')
rank=sorted([base,best['SW1'],best['SW2'],best['SW3']],key=lambda r:(r['mean_ssim'],r['mean_psnr_db']),reverse=True)
summary={'protocol':'EDGE_NG_V01_SWEEP_V2','historical_equivalence':False,
         'tuning_scope':'EDGE_CORE','tuning_case_count':len(tune_cases),
         'edge_families':sorted(EDGE_FAMILIES),'epsilon_candidates':EPS,
         'best_per_mode':best,'ranking_edge_core':rank,
         'selection_rule':'highest mean SSIM on EDGE_CORE; mean PSNR tiebreak',
         'post_selection_validation':'render selected BASELINE/SW1/SW2/SW3 on all 42 PTAR-PERCEPTUAL-V1-A cases'}
(OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')

# Render selected variants on full 42-case corpus.
final_manifest=[]
for mode in ('BASELINE','SW1','SW2','SW3'):
    eps=1e-3 if mode=='BASELINE' else float(best[mode]['epsilon'])
    vdir=FINAL/mode;vdir.mkdir(exist_ok=True)
    for i,c in enumerate(all_cases,1):
        if i==1: print(f'[FINAL] {mode} epsilon={eps:g}',flush=True)
        lr=load(CORPUS/c['input_path'])
        out,_=render_edge_ng_v01(lr,mode,eps)
        p=vdir/f'{c["case_id"]}.png'
        Image.fromarray(np.rint(out*255).astype(np.uint8),'RGB').save(p,compress_level=3)
        final_manifest.append({'case_id':c['case_id'],'series':c['family'],
                               'reference_path':str((CORPUS/c['reference_path']).resolve()),
                               'variant':f'EDGE_NG_V01_{mode}_eps{eps:g}',
                               'output_path':str(p.resolve())})
with (FINAL/'BENCHMARK_MANIFEST.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=list(final_manifest[0].keys()));w.writeheader();w.writerows(final_manifest)
(FINAL/'SELECTION.json').write_text(json.dumps({'best_per_mode':best,'ranking_edge_core':rank},indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,indent=2))

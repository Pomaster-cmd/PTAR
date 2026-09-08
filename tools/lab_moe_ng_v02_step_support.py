#!/usr/bin/env python3
"""LAB13: step-support router for v01 <-> LAB07 MC.

LAB12 showed that coherence + diagonal geometry still lets MC leak onto some
hard pixel-art/UI transitions. LAB13 adds one feature derived only from the
already-fetched directional samples fm1/f0/f1/f2:

    central_share = |f1-f0| / (|f0-fm1| + |f1-f0| + |f2-f1| + eps)

An isolated hard step tends toward 1.0, while a supported ramp/texture tends to
share slope energy with its neighbours. No texture fetch is added.

Candidate parameters are selected on A+B only. Crop C is reported as a reused
diagnostic set, not as a pristine holdout: previous LAB10-12 observations have
already informed architecture design. Robustness selection therefore also uses
A/B, protocol, family and source-balanced constraints.
"""
from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
from collections import defaultdict
from pathlib import Path

import numpy as np

import lab_moe_ng_v02_hermite_sweep as h5
import lab_moe_ng_v02_v01_quality as q10
import lab_moe_ng_v02_hybrid_sweep as q11

EPS=np.float32(1e-6)
STRUCT=q11.STRUCT_FAMILIES


def psnr(a,b):
    d=np.asarray(a,dtype=np.float64)-np.asarray(b,dtype=np.float64)
    m=float(np.mean(d*d,dtype=np.float64))
    return float('inf') if m<=0 else 10.0*math.log10(1.0/m)


def sat(x): return np.clip(x,np.float32(0),np.float32(1))
def high(x,t,w): return sat((x-np.float32(t))/np.float32(max(w,1e-6)))
def low(x,t,w): return np.float32(1)-sat((x-np.float32(t))/np.float32(max(w,1e-6)))


def features13(lr):
    base=q11.features(lr)
    h,w,_=lr.shape
    oh,ow=(h*3)//2,(w*3)//2
    oy,ox=np.mgrid[0:oh,0:ow]
    sx=ox.astype(np.float32)*(np.float32(2)/3)
    sy=oy.astype(np.float32)*(np.float32(2)/3)
    fx=np.floor(sx).astype(np.int32); fy=np.floor(sy).astype(np.int32)
    x0=np.clip(fx,0,w-1); x1=np.clip(fx+1,0,w-1)
    y0=np.clip(fy,0,h-1); y1=np.clip(fy+1,0,h-1)
    tl=lr[y0,x0,1]; tr=lr[y0,x1,1]; bl=lr[y1,x0,1]; br=lr[y1,x1,1]
    gx=(tr+br)-(tl+bl); gy=(bl+br)-(tl+tr)
    usex=np.abs(gx)>=np.abs(gy)
    bx=np.where(usex,fx.astype(np.float32),sx)
    by=np.where(usex,sy,fy.astype(np.float32))
    ax=usex.astype(np.float32); ay=(~usex).astype(np.float32)
    fm1=h5.sample(lr,bx-ax,by-ay); f0=h5.sample(lr,bx,by)
    f1=h5.sample(lr,bx+ax,by+ay); f2=h5.sample(lr,bx+np.float32(2)*ax,by+np.float32(2)*ay)
    ym1=np.sum(fm1*q10.LUMA,axis=2,dtype=np.float32)
    y_0=np.sum(f0*q10.LUMA,axis=2,dtype=np.float32)
    y_1=np.sum(f1*q10.LUMA,axis=2,dtype=np.float32)
    y_2=np.sum(f2*q10.LUMA,axis=2,dtype=np.float32)
    d0=np.abs(y_0-ym1); d1=np.abs(y_1-y_0); d2=np.abs(y_2-y_1)
    central=d1/(d0+d1+d2+EPS)
    support=(d0+d2)/(np.float32(2)*d1+EPS)
    base['central']=central.astype(np.float32)
    base['support']=support.astype(np.float32)
    return base


def gate(feat,spec):
    step_ok=low(feat['central'],spec['step_t'],spec['step_w'])
    kind=spec['kind']
    if kind=='step': g=step_ok
    elif kind=='hc_step': g=high(feat['coh'],spec['coh_t'],spec['coh_w'])*step_ok
    elif kind=='ld_step': g=low(feat['diag'],spec['diag_t'],spec['diag_w'])*step_ok
    elif kind=='hc_ld_step':
        g=high(feat['coh'],spec['coh_t'],spec['coh_w'])*low(feat['diag'],spec['diag_t'],spec['diag_w'])*step_ok
    else: raise ValueError(kind)
    return (g*np.float32(spec['strength']))[...,None].astype(np.float32)


def candidates():
    out=[]
    strengths=[.25,.50,.75]
    step_t=[.40,.50,.60,.70,.80]; step_w=[.08,.15,.25]
    for st,sw,s in itertools.product(step_t,step_w,strengths):
        out.append({'kind':'step','step_t':st,'step_w':sw,'strength':s})
    for ct,cw,st,sw,s in itertools.product([.05,.10,.20,.30],[.10,.20],step_t,[.10,.20],strengths):
        out.append({'kind':'hc_step','coh_t':ct,'coh_w':cw,'step_t':st,'step_w':sw,'strength':s})
    for dt,dw,st,sw,s in itertools.product([.06,.10,.15,.20,.25],[.15,.30],step_t,[.10,.20],strengths):
        out.append({'kind':'ld_step','diag_t':dt,'diag_w':dw,'step_t':st,'step_w':sw,'strength':s})
    for ct,dt,st,s in itertools.product([.05,.10,.20,.30],[.08,.12,.18,.25],step_t,strengths):
        out.append({'kind':'hc_ld_step','coh_t':ct,'coh_w':.15,'diag_t':dt,'diag_w':.25,'step_t':st,'step_w':.15,'strength':s})
    return out


def prepare(corpus,row,proto):
    lr=h5.load(corpus/row['input_path']); ref=h5.load(corpus/row['reference_path'])
    v01,v02=q10.reconstruct_pair(lr)
    return {'protocol':proto,'case_id':row['case_id'],'source_id':row.get('source_id',''),'family':row['family'],'crop_tag':row.get('crop_tag',''),'ref':ref,'v01':v01,'v02':v02,'feat':features13(lr),'p01':psnr(v01,ref),'p02':psnr(v02,ref)}


def evaluate(cases,spec):
    rows=[]
    for c in cases:
        g=gate(c['feat'],spec)
        out=np.clip(c['v01']+(c['v02']-c['v01'])*g,np.float32(0),np.float32(1))
        p=psnr(out,c['ref'])
        rows.append({'protocol':c['protocol'],'case_id':c['case_id'],'source_id':c['source_id'],'family':c['family'],'crop_tag':c['crop_tag'],'v01_psnr':c['p01'],'v02_psnr':c['p02'],'hybrid_psnr':p,'gap_v01':p-c['p01'],'gap_v02':p-c['p02'],'gate_mean':float(np.mean(g,dtype=np.float64)),'central_mean':float(np.mean(c['feat']['central'],dtype=np.float64))})
    return rows


def avg(v): return float(sum(v)/len(v)) if v else float('nan')


def group_means(rows,key):
    d=defaultdict(list)
    for r in rows: d[r[key]].append(r['gap_v01'])
    return {k:avg(v) for k,v in d.items()}


def block(rows):
    gaps=[r['gap_v01'] for r in rows]
    struct=[r for r in rows if r['family'] in STRUCT]
    fam=group_means(rows,'family'); src=group_means(rows,'source_id')
    return {'cases':len(rows),'mean_gap_v01':avg(gaps),'worst_gap_v01':min(gaps),'wins_v01':sum(x>0 for x in gaps),'struct_mean_gap_v01':avg([r['gap_v01'] for r in struct]),'struct_worst_gap_v01':min(r['gap_v01'] for r in struct),'family_balanced_mean_gap_v01':avg(list(fam.values())),'worst_family_mean_gap_v01':min(fam.values()),'source_balanced_mean_gap_v01':avg(list(src.values())),'worst_source_mean_gap_v01':min(src.values()),'mean_gate':avg([r['gate_mean'] for r in rows])}


def summarize_train(rows,spec):
    splits={
        'all':rows,
        'A':[r for r in rows if r['crop_tag']=='A'],
        'B':[r for r in rows if r['crop_tag']=='B'],
        'B_GRID':[r for r in rows if r['protocol']=='B_GRID'],
        'V1_A':[r for r in rows if r['protocol']=='V1_A'],
    }
    s={'spec':spec,**{k:block(v) for k,v in splits.items()}}
    a=s['A']; b=s['B']; p=s['B_GRID']; q=s['V1_A']; x=s['all']
    feasible=(
        x['worst_gap_v01']>=-.15 and x['struct_worst_gap_v01']>=-.15
        and a['mean_gap_v01']>=.04 and b['mean_gap_v01']>=.04
        and p['mean_gap_v01']>=.04 and q['mean_gap_v01']>=.04
        and a['struct_mean_gap_v01']>=0.0 and b['struct_mean_gap_v01']>=0.0
        and x['worst_family_mean_gap_v01']>=-.05
        and x['worst_source_mean_gap_v01']>=-.05
    )
    robust=min(a['mean_gap_v01'],b['mean_gap_v01'],p['mean_gap_v01'],q['mean_gap_v01'])
    score=(.24*x['mean_gap_v01']+.18*x['family_balanced_mean_gap_v01']+.18*x['source_balanced_mean_gap_v01']+.18*x['struct_mean_gap_v01']+.14*robust+.04*x['worst_family_mean_gap_v01']+.04*x['worst_source_mean_gap_v01']-.01*x['mean_gate'])
    s['feasible']=feasible; s['score']=score
    return s


def write_csv(path,rows):
    keys=[]; seen=set()
    for r in rows:
        for k in r:
            if k not in seen: seen.add(k); keys.append(k)
    with path.open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=keys); w.writeheader(); w.writerows(rows)


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--bgrid',required=True); ap.add_argument('--v1a',required=True); ap.add_argument('--out',required=True); a=ap.parse_args()
    out=Path(a.out); out.mkdir(parents=True,exist_ok=True)
    train=[]; diag_c=[]
    for proto,p in [('B_GRID',Path(a.bgrid)),('V1_A',Path(a.v1a))]:
        rr=list(csv.DictReader((p/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for i,r in enumerate(rr,1):
            c=prepare(p,r,proto); (diag_c if r.get('crop_tag')=='C' else train).append(c); print(proto,i,len(rr),r['case_id'])
    specs=candidates(); ranked=[]
    for i,spec in enumerate(specs,1):
        ranked.append(summarize_train(evaluate(train,spec),spec))
        if i%100==0: print('candidates',i,'/',len(specs))
    feasible=[r for r in ranked if r['feasible']]
    selected=max(feasible,key=lambda r:(r['score'],r['all']['mean_gap_v01'],-r['all']['mean_gate'])) if feasible else max(ranked,key=lambda r:r['score'])
    train_rows=evaluate(train,selected['spec']); c_rows=evaluate(diag_c,selected['spec'])
    ranked.sort(key=lambda r:r['score'],reverse=True)
    write_csv(out/'TRAIN_SELECTED_PER_CASE.csv',train_rows)
    write_csv(out/'REUSED_C_DIAGNOSTIC_PER_CASE.csv',c_rows)
    flat=[]
    for r in ranked:
        z={'score':r['score'],'feasible':r['feasible'],**r['spec']}
        for name in ['all','A','B','B_GRID','V1_A']:
            for k,v in r[name].items(): z[f'{name}_{k}']=v
        flat.append(z)
    write_csv(out/'SWEEP.csv',flat)
    result={
        'protocol':'LAB13_STEP_SUPPORT_AB_SELECT_REUSED_C_DIAGNOSTIC',
        'selection_used_C':False,
        'C_is_pristine_holdout':False,
        'C_note':'LAB10-12 C observations informed LAB13 architecture design; C is diagnostic only.',
        'texture_fetch_delta':0,
        'candidate_count':len(specs),
        'feasible_count':len(feasible),
        'selected_train':selected,
        'reused_C_diagnostic':block(c_rows),
        'top':ranked[:40],
    }
    (out/'SUMMARY.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()

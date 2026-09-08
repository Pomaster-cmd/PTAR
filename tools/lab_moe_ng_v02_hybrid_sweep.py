#!/usr/bin/env python3
"""LAB11: non-leaky v01/MC hybrid gate sweep.

Goal: determine whether a very small feature gate can keep LAB07's average
quality gain while recovering its regressions on thin oblique lines, rings and
UI text. Selection uses A+B only across B_GRID and V1_A. Crop C is evaluated
once after selection. No runtime shader is changed by this diagnostic.
"""
from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
from pathlib import Path

import numpy as np

import lab_moe_ng_v02_hermite_sweep as h5
import lab_moe_ng_v02_v01_quality as q10

EPS=np.float32(1e-6)
STRUCT_FAMILIES={'curves_high_frequency','raster_repeated','thin_oblique_edges','ui_text','silhouette_edge','pixel_art'}


def psnr(a,b):
    d=np.asarray(a,dtype=np.float64)-np.asarray(b,dtype=np.float64)
    m=float(np.mean(d*d,dtype=np.float64))
    return float('inf') if m<=0 else 10.0*math.log10(1.0/m)


def features(lr):
    h,w,_=lr.shape
    oh,ow=(h*3)//2,(w*3)//2
    oy,ox=np.mgrid[0:oh,0:ow]
    sx=ox.astype(np.float32)*(np.float32(2)/3); sy=oy.astype(np.float32)*(np.float32(2)/3)
    fx=np.floor(sx).astype(np.int32); fy=np.floor(sy).astype(np.int32)
    x0=np.clip(fx,0,w-1); x1=np.clip(fx+1,0,w-1); y0=np.clip(fy,0,h-1); y1=np.clip(fy+1,0,h-1)
    tl=lr[y0,x0,1]; tr=lr[y0,x1,1]; bl=lr[y1,x0,1]; br=lr[y1,x1,1]
    gx=(tr+br)-(tl+bl); gy=(bl+br)-(tl+tr); agx=np.abs(gx); agy=np.abs(gy)
    local_range=np.maximum(np.maximum(tl,tr),np.maximum(bl,br))-np.minimum(np.minimum(tl,tr),np.minimum(bl,br))
    gradient=np.float32(.5)*np.maximum(agx,agy)
    diagonal=np.float32(.5)*np.abs((tl+br)-(tr+bl))
    coherence=np.abs(agx-agy)/(agx+agy+EPS)
    diag_ratio=diagonal/(gradient+EPS)

    usex=agx>=agy
    bx=np.where(usex,fx.astype(np.float32),sx); by=np.where(usex,sy,fy.astype(np.float32))
    ax=usex.astype(np.float32); ay=(~usex).astype(np.float32)
    fm1=h5.sample(lr,bx-ax,by-ay); f0=h5.sample(lr,bx,by); f1=h5.sample(lr,bx+ax,by+ay); f2=h5.sample(lr,bx+np.float32(2)*ax,by+np.float32(2)*ay)
    y=np.stack([np.sum(z*q10.LUMA,axis=2,dtype=np.float32) for z in (fm1,f0,f1,f2)],axis=2)
    ac=np.maximum(np.abs(y[:,:,0]-np.float32(2)*y[:,:,1]+y[:,:,2]),np.abs(y[:,:,1]-np.float32(2)*y[:,:,2]+y[:,:,3]))
    slope=np.maximum(np.abs(y[:,:,1]-y[:,:,0]),np.maximum(np.abs(y[:,:,2]-y[:,:,1]),np.abs(y[:,:,3]-y[:,:,2])))
    rel_curv=ac/(slope+EPS)
    return {'range':local_range.astype(np.float32),'coh':coherence.astype(np.float32),'diag':diag_ratio.astype(np.float32),'rc':rel_curv.astype(np.float32)}


def sat(x): return np.clip(x,np.float32(0),np.float32(1))


def gate(feat,kind,p0,p1,strength):
    width=max(float(p1),1e-6)
    if kind=='low_diag': g=np.float32(1)-sat((feat['diag']-np.float32(p0))/np.float32(width))
    elif kind=='low_rc': g=np.float32(1)-sat((feat['rc']-np.float32(p0))/np.float32(width))
    elif kind=='high_rc': g=sat((feat['rc']-np.float32(p0))/np.float32(width))
    elif kind=='low_coh': g=np.float32(1)-sat((feat['coh']-np.float32(p0))/np.float32(width))
    elif kind=='high_coh': g=sat((feat['coh']-np.float32(p0))/np.float32(width))
    elif kind=='high_range': g=sat((feat['range']-np.float32(p0))/np.float32(width))
    elif kind=='low_diag_high_range':
        a=np.float32(1)-sat((feat['diag']-np.float32(p0))/np.float32(width))
        b=sat((feat['range']-np.float32(.015))/np.float32(.08))
        g=a*b
    else: raise ValueError(kind)
    return (g*np.float32(strength))[...,None].astype(np.float32)


def candidates():
    specs=[]
    for kind in ['low_diag','low_rc','high_rc','low_coh','high_coh','high_range','low_diag_high_range']:
        if kind in ('low_diag','low_diag_high_range'):
            p0s=[.04,.08,.12,.18,.25]; widths=[.10,.20,.35,.50]
        elif kind in ('low_rc','high_rc'):
            p0s=[.02,.05,.08,.12,.20,.35]; widths=[.04,.08,.15,.30,.60]
        elif kind in ('low_coh','high_coh'):
            p0s=[.10,.20,.30,.45,.60]; widths=[.10,.20,.35,.50]
        else:
            p0s=[.005,.01,.02,.04,.08]; widths=[.02,.05,.10,.20]
        for p0,p1,s in itertools.product(p0s,widths,[.25,.50,.75,1.0]): specs.append((kind,p0,p1,s))
    return specs


def prepare(corpus,row,proto):
    lr=h5.load(corpus/row['input_path']); ref=h5.load(corpus/row['reference_path'])
    v01,v02=q10.reconstruct_pair(lr)
    return {'protocol':proto,'case_id':row['case_id'],'family':row['family'],'crop_tag':row.get('crop_tag',''),'ref':ref,'v01':v01,'v02':v02,'feat':features(lr),'p01':psnr(v01,ref),'p02':psnr(v02,ref)}


def evaluate(cases,cand):
    kind,p0,p1,s=cand; rows=[]
    for c in cases:
        g=gate(c['feat'],kind,p0,p1,s)
        out=np.clip(c['v01']+(c['v02']-c['v01'])*g,np.float32(0),np.float32(1))
        p=psnr(out,c['ref'])
        rows.append({'protocol':c['protocol'],'case_id':c['case_id'],'family':c['family'],'crop_tag':c['crop_tag'],'v01_psnr':c['p01'],'v02_psnr':c['p02'],'hybrid_psnr':p,'gap_v01':p-c['p01'],'gap_v02':p-c['p02'],'gate_mean':float(np.mean(g,dtype=np.float64))})
    return rows


def summary(rows,cand):
    vals=[r['hybrid_psnr'] for r in rows]; gaps=[r['gap_v01'] for r in rows]
    struct=[r for r in rows if r['family'] in STRUCT_FAMILIES]
    mean=lambda a:sum(a)/len(a)
    return {'kind':cand[0],'p0':cand[1],'width':cand[2],'strength':cand[3],'cases':len(rows),'mean_psnr':mean(vals),'mean_gap_v01':mean(gaps),'worst_gap_v01':min(gaps),'wins_v01':sum(x>0 for x in gaps),'struct_mean_gap_v01':mean([r['gap_v01'] for r in struct]),'struct_worst_gap_v01':min(r['gap_v01'] for r in struct),'mean_gate':mean([r['gate_mean'] for r in rows])}


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--bgrid',required=True); ap.add_argument('--v1a',required=True); ap.add_argument('--out',required=True); a=ap.parse_args()
    out=Path(a.out); out.mkdir(parents=True,exist_ok=True)
    train=[]; hold=[]
    for proto,p in [('B_GRID',Path(a.bgrid)),('V1_A',Path(a.v1a))]:
        rr=list(csv.DictReader((p/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for i,r in enumerate(rr,1):
            c=prepare(p,r,proto); (hold if r.get('crop_tag')=='C' else train).append(c); print(proto,i,len(rr),r['case_id'])
    C=candidates(); ranked=[]
    for i,c in enumerate(C,1):
        rows=evaluate(train,c); s=summary(rows,c)
        # Prefer real mean gain, but reject architectures that simply trade a
        # large structural regression for texture wins. Cost proxy favors a
        # smaller average MC gate when quality is otherwise close.
        penalty=18*max(0.0,-0.20-s['worst_gap_v01'])+10*max(0.0,-0.05-s['struct_mean_gap_v01'])
        s['score']=s['mean_psnr']-penalty-0.01*s['mean_gate']
        ranked.append(s)
        if i%100==0: print('candidates',i,'/',len(C))
    feasible=[r for r in ranked if r['worst_gap_v01']>=-.20 and r['struct_mean_gap_v01']>=-.05]
    selected=max(feasible,key=lambda x:(x['mean_psnr'],-x['mean_gate'])) if feasible else max(ranked,key=lambda x:x['score'])
    cand=(selected['kind'],selected['p0'],selected['width'],selected['strength'])
    train_rows=evaluate(train,cand); hold_rows=evaluate(hold,cand)
    train_sum=summary(train_rows,cand); hold_sum=summary(hold_rows,cand)
    ranked.sort(key=lambda x:x['score'],reverse=True)
    for fn,rows in [('TRAIN_SELECTED_PER_CASE.csv',train_rows),('HOLDOUT_C_PER_CASE.csv',hold_rows)]:
        with (out/fn).open('w',newline='',encoding='utf-8') as f: w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    with (out/'SWEEP.csv').open('w',newline='',encoding='utf-8') as f: w=csv.DictWriter(f,fieldnames=list(ranked[0])); w.writeheader(); w.writerows(ranked)
    result={'protocol':'LAB11_V01_MC_HYBRID_AB_SELECT_C_HOLDOUT','selection_used_holdout':False,'candidate_count':len(C),'feasible_count':len(feasible),'selected':selected,'train':train_sum,'holdout_C':hold_sum,'top':ranked[:30]}
    (out/'SUMMARY.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8'); print(json.dumps(result,indent=2))
if __name__=='__main__': main()

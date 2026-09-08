#!/usr/bin/env python3
"""LAB14: fixed directional-feature audit for v01 vs LAB07 failure analysis.

No parameter selection. The audit records distributions of slope topology
features obtainable from the existing fm1/f0/f1/f2 samples, plus the paired
LAB07-v01 PSNR delta. It exists to identify a structural discriminator before
another router sweep is attempted.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path

import numpy as np

import lab_moe_ng_v02_hermite_sweep as h5
import lab_moe_ng_v02_v01_quality as q10
import lab_moe_ng_v02_hybrid_sweep as q11

EPS=np.float32(1e-6)


def psnr(a,b):
    d=np.asarray(a,dtype=np.float64)-np.asarray(b,dtype=np.float64)
    m=float(np.mean(d*d,dtype=np.float64))
    return float('inf') if m<=0 else 10.0*math.log10(1.0/m)


def directional_slopes(lr):
    h,w,_=lr.shape; oh,ow=(h*3)//2,(w*3)//2
    oy,ox=np.mgrid[0:oh,0:ow]
    sx=ox.astype(np.float32)*(np.float32(2)/3); sy=oy.astype(np.float32)*(np.float32(2)/3)
    fx=np.floor(sx).astype(np.int32); fy=np.floor(sy).astype(np.int32)
    x0=np.clip(fx,0,w-1); x1=np.clip(fx+1,0,w-1); y0=np.clip(fy,0,h-1); y1=np.clip(fy+1,0,h-1)
    tl=lr[y0,x0,1]; tr=lr[y0,x1,1]; bl=lr[y1,x0,1]; br=lr[y1,x1,1]
    gx=(tr+br)-(tl+bl); gy=(bl+br)-(tl+tr); usex=np.abs(gx)>=np.abs(gy)
    bx=np.where(usex,fx.astype(np.float32),sx); by=np.where(usex,sy,fy.astype(np.float32))
    ax=usex.astype(np.float32); ay=(~usex).astype(np.float32)
    fm1=h5.sample(lr,bx-ax,by-ay); f0=h5.sample(lr,bx,by); f1=h5.sample(lr,bx+ax,by+ay); f2=h5.sample(lr,bx+np.float32(2)*ax,by+np.float32(2)*ay)
    y=[np.sum(z*q10.LUMA,axis=2,dtype=np.float32) for z in (fm1,f0,f1,f2)]
    d0=y[1]-y[0]; d1=y[2]-y[1]; d2=y[3]-y[2]
    return d0.astype(np.float32),d1.astype(np.float32),d2.astype(np.float32)


def features(lr):
    base=q11.features(lr)
    d0,d1,d2=directional_slopes(lr); a0=np.abs(d0); a1=np.abs(d1); a2=np.abs(d2); e=a0+a1+a2+EPS
    central=a1/e
    min_support=np.minimum(a0,a2)/(a1+EPS)
    max_support=np.maximum(a0,a2)/(a1+EPS)
    outer_balance=np.minimum(a0,a2)/(np.maximum(a0,a2)+EPS)
    curvature=(np.abs(d1-d0)+np.abs(d2-d1))/e
    mono=((d0*d1)>0).astype(np.float32)*((d1*d2)>0).astype(np.float32)
    turn=(((d0*d1)<0)|((d1*d2)<0)).astype(np.float32)
    outer_same=((d0*d2)>0).astype(np.float32)
    isolated=(central>.75).astype(np.float32)
    plateau=((np.minimum(a0,a2)<np.float32(.10)*(a1+EPS))&(a1>np.float32(.005))).astype(np.float32)
    return {**base,'central':central,'min_support':min_support,'max_support':max_support,'outer_balance':outer_balance,'curvature_topology':curvature,'mono3':mono,'turn':turn,'outer_same':outer_same,'isolated':isolated,'plateau':plateau}


def stats(x):
    a=np.asarray(x,dtype=np.float64).ravel(); q=np.quantile(a,[.10,.25,.50,.75,.90])
    return {'mean':float(np.mean(a)),'q10':float(q[0]),'q25':float(q[1]),'q50':float(q[2]),'q75':float(q[3]),'q90':float(q[4])}


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--bgrid',required=True); ap.add_argument('--v1a',required=True); ap.add_argument('--out',required=True); a=ap.parse_args()
    out=Path(a.out); out.mkdir(parents=True,exist_ok=True); rows=[]; detail={}
    for proto,p in [('B_GRID',Path(a.bgrid)),('V1_A',Path(a.v1a))]:
        rr=list(csv.DictReader((p/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for i,r in enumerate(rr,1):
            lr=h5.load(p/r['input_path']); ref=h5.load(p/r['reference_path']); v01,v02=q10.reconstruct_pair(lr); f=features(lr)
            key=f"{proto}:{r['case_id']}"
            rec={'protocol':proto,'case_id':r['case_id'],'source_id':r.get('source_id',''),'family':r['family'],'crop_tag':r.get('crop_tag',''),'v02_minus_v01_psnr':psnr(v02,ref)-psnr(v01,ref)}
            detail[key]={}
            for name,x in f.items():
                s=stats(x); detail[key][name]=s
                for k,v in s.items(): rec[f'{name}_{k}']=v
            rows.append(rec); print(proto,i,len(rr),r['case_id'],f"dPSNR={rec['v02_minus_v01_psnr']:+.4f}")
    with (out/'CASE_FEATURES.csv').open('w',newline='',encoding='utf-8') as fp:
        w=csv.DictWriter(fp,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    (out/'FEATURE_DISTRIBUTIONS.json').write_text(json.dumps(detail,indent=2)+'\n',encoding='utf-8')
    worst=sorted(rows,key=lambda r:r['v02_minus_v01_psnr'])[:20]; best=sorted(rows,key=lambda r:r['v02_minus_v01_psnr'],reverse=True)[:20]
    summary={'protocol':'LAB14_FIXED_DIRECTIONAL_FEATURE_AUDIT','selection_or_tuning':False,'texture_fetch_delta':0,'cases':len(rows),'worst':[{k:r[k] for k in ('protocol','case_id','family','crop_tag','v02_minus_v01_psnr')} for r in worst],'best':[{k:r[k] for k in ('protocol','case_id','family','crop_tag','v02_minus_v01_psnr')} for r in best]}
    (out/'SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__': main()

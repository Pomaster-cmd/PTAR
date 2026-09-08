#!/usr/bin/env python3
"""LAB15: outer-slope-balance router search for v01 <-> LAB07 MC.

LAB14 found a bounded structural signal that separates several major LAB07
wins (frequency mix, brick, retina) from the pixel-art/UI failures better than
central-step share: balance of the two slopes surrounding the reconstructed
interval.

    outer_balance = min(|d0|,|d2|) / (max(|d0|,|d2|) + eps)

All features are derived from the existing fm1/f0/f1/f2 samples, so this search
adds zero texture fetches. A+B alone select parameters. C is a reused
diagnostic set because earlier LAB10-14 results have already informed design.
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
import lab_moe_ng_v02_hybrid_sweep as q11
import lab_moe_ng_v02_step_support as q13
import lab_moe_ng_v02_feature_audit as q14

EPS=np.float32(1e-6)


def psnr(a,b):
    d=np.asarray(a,dtype=np.float64)-np.asarray(b,dtype=np.float64)
    m=float(np.mean(d*d,dtype=np.float64))
    return float('inf') if m<=0 else 10.0*math.log10(1.0/m)


def sat(x): return np.clip(x,np.float32(0),np.float32(1))
def high(x,t,w): return sat((x-np.float32(t))/np.float32(max(w,1e-6)))
def low(x,t,w): return np.float32(1)-sat((x-np.float32(t))/np.float32(max(w,1e-6)))


def features15(lr):
    base=q11.features(lr)
    d0,d1,d2=q14.directional_slopes(lr)
    a0=np.abs(d0); a1=np.abs(d1); a2=np.abs(d2); energy=a0+a1+a2+EPS
    base['balance']=(np.minimum(a0,a2)/(np.maximum(a0,a2)+EPS)).astype(np.float32)
    base['topocurv']=((np.abs(d1-d0)+np.abs(d2-d1))/energy).astype(np.float32)
    base['turn']=(((d0*d1)<0)|((d1*d2)<0)).astype(np.float32)
    return base


def gate(f,spec):
    g=high(f['balance'],spec['bal_t'],spec['bal_w'])
    k=spec['kind']
    if k in ('bal_coh','bal_coh_lcurv'):
        g=g*high(f['coh'],spec['coh_t'],spec['coh_w'])
    if k in ('bal_lcurv','bal_coh_lcurv'):
        g=g*low(f['topocurv'],spec['curv_t'],spec['curv_w'])
    if k=='bal_noturn':
        g=g*(np.float32(1)-f['turn'])
    return (g*np.float32(spec['strength']))[...,None].astype(np.float32)


def candidates():
    out=[]; bt=[.15,.20,.25,.30,.35,.40,.45,.50,.60]; bw=[.10,.20,.30]; ss=[.25,.50,.75]
    for t,w,s in itertools.product(bt,bw,ss): out.append({'kind':'balance','bal_t':t,'bal_w':w,'strength':s})
    for t,w,ct,cw,s in itertools.product(bt,bw,[.05,.10,.20,.30],[.10,.20],ss): out.append({'kind':'bal_coh','bal_t':t,'bal_w':w,'coh_t':ct,'coh_w':cw,'strength':s})
    for t,w,rt,rw,s in itertools.product(bt,bw,[.45,.60,.75,.90,1.10],[.15,.30],ss): out.append({'kind':'bal_lcurv','bal_t':t,'bal_w':w,'curv_t':rt,'curv_w':rw,'strength':s})
    for t,ct,rt,s in itertools.product(bt,[.05,.10,.20,.30],[.50,.70,.90,1.10],ss): out.append({'kind':'bal_coh_lcurv','bal_t':t,'bal_w':.20,'coh_t':ct,'coh_w':.15,'curv_t':rt,'curv_w':.25,'strength':s})
    for t,w,s in itertools.product(bt,bw,ss): out.append({'kind':'bal_noturn','bal_t':t,'bal_w':w,'strength':s})
    return out


def prepare(p,r,proto):
    lr=h5.load(p/r['input_path']); ref=h5.load(p/r['reference_path']); v01,v02=q10.reconstruct_pair(lr)
    return {'protocol':proto,'case_id':r['case_id'],'source_id':r.get('source_id',''),'family':r['family'],'crop_tag':r.get('crop_tag',''),'ref':ref,'v01':v01,'v02':v02,'feat':features15(lr),'p01':psnr(v01,ref),'p02':psnr(v02,ref)}


def evaluate(cases,spec):
    rows=[]
    for c in cases:
        g=gate(c['feat'],spec); out=np.clip(c['v01']+(c['v02']-c['v01'])*g,np.float32(0),np.float32(1)); p=psnr(out,c['ref'])
        rows.append({'protocol':c['protocol'],'case_id':c['case_id'],'source_id':c['source_id'],'family':c['family'],'crop_tag':c['crop_tag'],'v01_psnr':c['p01'],'v02_psnr':c['p02'],'hybrid_psnr':p,'gap_v01':p-c['p01'],'gap_v02':p-c['p02'],'gate_mean':float(np.mean(g,dtype=np.float64)),'balance_mean':float(np.mean(c['feat']['balance'],dtype=np.float64))})
    return rows


def write_csv(path,rows):
    keys=[]; seen=set()
    for r in rows:
        for k in r:
            if k not in seen: seen.add(k); keys.append(k)
    with path.open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=keys); w.writeheader(); w.writerows(rows)


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--bgrid',required=True); ap.add_argument('--v1a',required=True); ap.add_argument('--out',required=True); a=ap.parse_args(); out=Path(a.out); out.mkdir(parents=True,exist_ok=True)
    train=[]; diag=[]
    for proto,p in [('B_GRID',Path(a.bgrid)),('V1_A',Path(a.v1a))]:
        rr=list(csv.DictReader((p/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for i,r in enumerate(rr,1):
            c=prepare(p,r,proto); (diag if r.get('crop_tag')=='C' else train).append(c); print(proto,i,len(rr),r['case_id'])
    specs=candidates(); ranked=[]
    for i,s in enumerate(specs,1):
        ranked.append(q13.summarize_train(evaluate(train,s),s))
        if i%100==0: print('candidates',i,'/',len(specs))
    feasible=[r for r in ranked if r['feasible']]; selected=max(feasible,key=lambda r:(r['score'],r['all']['mean_gap_v01'],-r['all']['mean_gate'])) if feasible else max(ranked,key=lambda r:r['score'])
    tr=evaluate(train,selected['spec']); dg=evaluate(diag,selected['spec']); ranked.sort(key=lambda r:r['score'],reverse=True)
    write_csv(out/'TRAIN_SELECTED_PER_CASE.csv',tr); write_csv(out/'REUSED_C_DIAGNOSTIC_PER_CASE.csv',dg)
    flat=[]
    for r in ranked:
        z={'score':r['score'],'feasible':r['feasible'],**r['spec']}
        for n in ['all','A','B','B_GRID','V1_A']:
            for k,v in r[n].items(): z[f'{n}_{k}']=v
        flat.append(z)
    write_csv(out/'SWEEP.csv',flat)
    result={'protocol':'LAB15_OUTER_BALANCE_AB_SELECT_REUSED_C_DIAGNOSTIC','selection_used_C':False,'C_is_pristine_holdout':False,'texture_fetch_delta':0,'candidate_count':len(specs),'feasible_count':len(feasible),'selected_train':selected,'reused_C_diagnostic':q13.block(dg),'top':ranked[:40]}
    (out/'SUMMARY.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8'); print(json.dumps(result,indent=2))

if __name__=='__main__': main()

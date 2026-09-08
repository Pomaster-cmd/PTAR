#!/usr/bin/env python3
"""LAB12: robust non-leaky multi-signal v01/MC router search.

Selection uses only A+B. Crop C remains sealed until exactly one candidate is
selected. The objective is deliberately robustness-biased: a candidate must
improve both A and B, protect structural families, and avoid large per-case
regressions before average PSNR is considered.
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

STRUCT = q11.STRUCT_FAMILIES


def psnr(a, b):
    d=np.asarray(a,dtype=np.float64)-np.asarray(b,dtype=np.float64)
    m=float(np.mean(d*d,dtype=np.float64))
    return float('inf') if m<=0 else 10.0*math.log10(1.0/m)


def sat(x): return np.clip(x,np.float32(0),np.float32(1))

def high(x,t,w): return sat((x-np.float32(t))/np.float32(max(w,1e-6)))
def low(x,t,w): return np.float32(1)-sat((x-np.float32(t))/np.float32(max(w,1e-6)))


def gate(feat, spec):
    kind=spec['kind']; s=np.float32(spec['strength'])
    hc=high(feat['coh'],spec['coh_t'],spec['coh_w']) if 'coh_t' in spec else np.float32(1)
    ld=low(feat['diag'],spec['diag_t'],spec['diag_w']) if 'diag_t' in spec else np.float32(1)
    lr=low(feat['rc'],spec['rc_t'],spec['rc_w']) if 'rc_t' in spec else np.float32(1)
    hr=high(feat['range'],spec['range_t'],spec['range_w']) if 'range_t' in spec else np.float32(1)
    if kind=='hc_ld': g=hc*ld
    elif kind=='hc_lrc': g=hc*lr
    elif kind=='hc_ld_lrc': g=hc*ld*lr
    elif kind=='hc_ld_hr': g=hc*ld*hr
    else: raise ValueError(kind)
    return (g*s)[...,None].astype(np.float32)


def candidates():
    out=[]
    strengths=[.25,.50,.75]
    for ct,cw,dt,dw,s in itertools.product([.05,.10,.15,.20,.30],[.10,.20,.35],[.04,.08,.12,.18,.25],[.10,.20,.35],strengths):
        out.append({'kind':'hc_ld','coh_t':ct,'coh_w':cw,'diag_t':dt,'diag_w':dw,'strength':s})
    for ct,cw,rt,rw,s in itertools.product([.05,.10,.15,.20,.30],[.10,.20,.35],[.05,.08,.12,.20,.35],[.08,.15,.30],strengths):
        out.append({'kind':'hc_lrc','coh_t':ct,'coh_w':cw,'rc_t':rt,'rc_w':rw,'strength':s})
    for ct,dt,rt,s in itertools.product([.05,.10,.15,.20,.30],[.04,.08,.12,.18,.25],[.05,.08,.12,.20,.35],strengths):
        out.append({'kind':'hc_ld_lrc','coh_t':ct,'coh_w':.20,'diag_t':dt,'diag_w':.20,'rc_t':rt,'rc_w':.15,'strength':s})
    for ct,dt,gt,s in itertools.product([.05,.10,.15,.20,.30],[.04,.08,.12,.18,.25],[.005,.01,.02],strengths):
        out.append({'kind':'hc_ld_hr','coh_t':ct,'coh_w':.20,'diag_t':dt,'diag_w':.20,'range_t':gt,'range_w':.08,'strength':s})
    return out


def prepare(corpus,row,proto):
    lr=h5.load(corpus/row['input_path']); ref=h5.load(corpus/row['reference_path'])
    v01,v02=q10.reconstruct_pair(lr)
    return {'protocol':proto,'case_id':row['case_id'],'source_id':row.get('source_id',''),'family':row['family'],'crop_tag':row.get('crop_tag',''),'ref':ref,'v01':v01,'v02':v02,'feat':q11.features(lr),'p01':psnr(v01,ref),'p02':psnr(v02,ref)}


def evaluate(cases,spec):
    rows=[]
    for c in cases:
        g=gate(c['feat'],spec)
        out=np.clip(c['v01']+(c['v02']-c['v01'])*g,np.float32(0),np.float32(1))
        p=psnr(out,c['ref'])
        rows.append({'protocol':c['protocol'],'case_id':c['case_id'],'source_id':c['source_id'],'family':c['family'],'crop_tag':c['crop_tag'],'v01_psnr':c['p01'],'v02_psnr':c['p02'],'hybrid_psnr':p,'gap_v01':p-c['p01'],'gap_v02':p-c['p02'],'gate_mean':float(np.mean(g,dtype=np.float64))})
    return rows


def avg(vals): return float(sum(vals)/len(vals)) if vals else float('nan')

def block(rows):
    if not rows: return {'cases':0}
    gaps=[r['gap_v01'] for r in rows]
    struct=[r for r in rows if r['family'] in STRUCT]
    fam=defaultdict(list)
    for r in rows: fam[r['family']].append(r['gap_v01'])
    fam_means=[avg(v) for v in fam.values()]
    return {'cases':len(rows),'mean_gap_v01':avg(gaps),'worst_gap_v01':min(gaps),'wins_v01':sum(x>0 for x in gaps),'struct_mean_gap_v01':avg([r['gap_v01'] for r in struct]),'struct_worst_gap_v01':min(r['gap_v01'] for r in struct),'family_balanced_mean_gap_v01':avg(fam_means),'worst_family_mean_gap_v01':min(fam_means),'mean_gate':avg([r['gate_mean'] for r in rows])}


def summarize(rows,spec):
    a=[r for r in rows if r['crop_tag']=='A']; b=[r for r in rows if r['crop_tag']=='B']
    d={'spec':spec,'all':block(rows),'A':block(a),'B':block(b)}
    x=d['all']; aa=d['A']; bb=d['B']
    feasible=(x['worst_gap_v01']>=-.20 and x['struct_worst_gap_v01']>=-.20 and aa['mean_gap_v01']>=.05 and bb['mean_gap_v01']>=.05 and aa['struct_mean_gap_v01']>=-.02 and bb['struct_mean_gap_v01']>=-.02 and x['worst_family_mean_gap_v01']>=-.10)
    robust_min=min(aa['mean_gap_v01'],bb['mean_gap_v01'])
    score=(.30*x['mean_gap_v01']+.20*x['family_balanced_mean_gap_v01']+.20*robust_min+.20*x['struct_mean_gap_v01']+.10*x['worst_family_mean_gap_v01']-.01*x['mean_gate'])
    d['feasible']=feasible; d['score']=score
    return d


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--bgrid',required=True); ap.add_argument('--v1a',required=True); ap.add_argument('--out',required=True); a=ap.parse_args()
    out=Path(a.out); out.mkdir(parents=True,exist_ok=True)
    train=[]; hold=[]
    for proto,p in [('B_GRID',Path(a.bgrid)),('V1_A',Path(a.v1a))]:
        rows=list(csv.DictReader((p/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for i,r in enumerate(rows,1):
            c=prepare(p,r,proto); (hold if r.get('crop_tag')=='C' else train).append(c); print(proto,i,len(rows),r['case_id'])

    specs=candidates(); ranked=[]
    for i,spec in enumerate(specs,1):
        ranked.append(summarize(evaluate(train,spec),spec))
        if i%100==0: print('candidates',i,'/',len(specs))
    feasible=[x for x in ranked if x['feasible']]
    selected=max(feasible,key=lambda x:(x['score'],x['all']['mean_gap_v01'],-x['all']['mean_gate'])) if feasible else max(ranked,key=lambda x:x['score'])
    train_rows=evaluate(train,selected['spec']); hold_rows=evaluate(hold,selected['spec']); hold_summary=block(hold_rows)
    hold_struct=[r for r in hold_rows if r['family'] in STRUCT]
    hold_summary['struct_mean_gap_v01']=avg([r['gap_v01'] for r in hold_struct]); hold_summary['struct_worst_gap_v01']=min(r['gap_v01'] for r in hold_struct)
    ranked.sort(key=lambda x:x['score'],reverse=True)

    def write_csv(name,rows):
        fieldnames=[]; seen=set()
        for row in rows:
            for key in row:
                if key not in seen:
                    seen.add(key); fieldnames.append(key)
        with (out/name).open('w',newline='',encoding='utf-8') as f:
            w=csv.DictWriter(f,fieldnames=fieldnames); w.writeheader(); w.writerows(rows)
    write_csv('TRAIN_SELECTED_PER_CASE.csv',train_rows); write_csv('HOLDOUT_C_PER_CASE.csv',hold_rows)
    flat=[]
    for x in ranked:
        r={'score':x['score'],'feasible':x['feasible'],**x['spec']}
        for prefix in ['all','A','B']:
            for k,v in x[prefix].items(): r[f'{prefix}_{k}']=v
        flat.append(r)
    write_csv('SWEEP.csv',flat)
    result={'protocol':'LAB12_ROBUST_MULTISIGNAL_AB_SELECT_C_HOLDOUT','selection_used_holdout':False,'candidate_count':len(specs),'feasible_count':len(feasible),'selected_train':selected,'holdout_C':hold_summary,'holdout_structural_cases':len(hold_struct),'top':ranked[:40]}
    (out/'SUMMARY.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8'); print(json.dumps(result,indent=2))

if __name__=='__main__': main()

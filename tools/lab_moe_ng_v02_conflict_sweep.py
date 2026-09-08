#!/usr/bin/env python3
"""LAB03: cross-axis conflict gate for PTAR-NG MoE v02.

Motivation: cross-protocol diagnostics show harmful V1_A cases often combine
high directional coherence with a large diagonal residual, whereas B-GRID
checker/pixel-art have high coherence but low diagonal conflict and B-GRID
true diagonals have high diagonal residual but very low coherence.

No extra texture fetch is required: coherence and axisConf already exist in
ROUTER-NG v01. Candidate form:
  base = curvature product gate
  conflict = coherence * (1-axisConf)
  gate = base * (1 - strength*saturate((conflict-confLo)/(confHi-confLo)))
Selection uses A+B crops of both protocols; C is untouched holdout.
"""
import argparse,csv,itertools,json,math
from pathlib import Path
import numpy as np
from lab_moe_ng_v02_sweep import components,load,sat
from lab_moe_ng_v02_feature_probe import gather_features

STRUCT_FAMILIES={'curves_high_frequency','raster_repeated','thin_oblique_edges','ui_text','silhouette_edge','pixel_art'}
BATCH=36


def psnr_mse(x):
    x=float(x);return float('inf') if x<=0 else 10.0*math.log10(1.0/x)


def prep(corpus,row,protocol):
    hr=load(corpus/row['reference_path']).astype(np.float32);lr=load(corpus/row['input_path']).astype(np.float32)
    b,m,ac,rc=components(lr);_,_,coh,dr,_,_=gather_features(lr)
    axis=np.float32(1)-sat((dr-np.float32(.08))/np.float32(.45))
    conflict=(coh*(np.float32(1)-axis)).astype(np.float32)
    e=(b-hr).astype(np.float32);d=(m-b).astype(np.float32)
    q0=np.mean(e*e,axis=2,dtype=np.float32);q1=np.mean(np.float32(2)*e*d,axis=2,dtype=np.float32);q2=np.mean(d*d,axis=2,dtype=np.float32)
    return {'protocol':protocol,'case_id':row['case_id'],'family':row['family'],'crop_tag':row.get('crop_tag',''),'pb':psnr_mse(q0.mean()),'pm':psnr_mse((q0+q1+q2).mean()),'ac':ac,'rc':rc,'conflict':conflict,'q0':q0,'q1':q1,'q2':q2}


def candidates():
    # Small base neighbourhood around LAB02 quality frontier + conflict routing.
    base=[]
    for rhi,alo,ahi in itertools.product([.75,.90,1.10],[0.0,.01],[.24,.32]):base.append((0.0,rhi,alo,ahi))
    conf=[]
    for lo,hi,strength in itertools.product([.15,.25,.35,.45,.55],[.65,.80,.95],[.50,.75,1.0]):
        if hi>lo:conf.append((lo,hi,strength))
    return [b+c for b in base for c in conf]


def matrix(cases,cands):
    n=len(cands);k=len(cases);P=np.empty((n,k),np.float64);G=np.empty((n,k),np.float32)
    for j,c in enumerate(cases):
        ac,rc,cf=c['ac'],c['rc'],c['conflict'];q0,q1,q2=c['q0'],c['q1'],c['q2']
        for s in range(0,n,BATCH):
            bb=cands[s:s+BATCH]
            rlo=np.asarray([x[0] for x in bb],np.float32)[:,None,None];rhi=np.asarray([x[1] for x in bb],np.float32)[:,None,None]
            alo=np.asarray([x[2] for x in bb],np.float32)[:,None,None];ahi=np.asarray([x[3] for x in bb],np.float32)[:,None,None]
            clo=np.asarray([x[4] for x in bb],np.float32)[:,None,None];chi=np.asarray([x[5] for x in bb],np.float32)[:,None,None];strength=np.asarray([x[6] for x in bb],np.float32)[:,None,None]
            base=sat((rc[None]-rlo)/(rhi-rlo))*sat((ac[None]-alo)/(ahi-alo))
            cg=sat((cf[None]-clo)/(chi-clo));g=(base*(np.float32(1)-strength*cg)).astype(np.float32)
            mse=np.mean(q0[None]+q1[None]*g+q2[None]*g*g,axis=(1,2),dtype=np.float64)
            P[s:s+len(bb),j]=[psnr_mse(x) for x in mse];G[s:s+len(bb),j]=np.mean(g,axis=(1,2),dtype=np.float64)
    return P,G


def summary(i,cand,cases,P,G):
    v=[]
    for j,c in enumerate(cases):
        p=float(P[i,j]);v.append({'protocol':c['protocol'],'case_id':c['case_id'],'family':c['family'],'crop_tag':c['crop_tag'],'bilinear_psnr':c['pb'],'v01_psnr':c['pm'],'v02_psnr':p,'gap_bilinear':p-c['pb'],'gap_v01':p-c['pm'],'gate_mean':float(G[i,j])})
    bg=[x for x in v if x['protocol']=='B_GRID'];va=[x for x in v if x['protocol']=='V1_A'];st=[x for x in bg if x['family'] in STRUCT_FAMILIES and x['v01_psnr']>x['bilinear_psnr']]
    den=sum(x['v01_psnr']-x['bilinear_psnr'] for x in st);num=sum(x['v02_psnr']-x['bilinear_psnr'] for x in st);ret=num/den if den else 1
    mean=lambda xs,key:sum(x[key] for x in xs)/len(xs)
    r={'rel_lo':cand[0],'rel_hi':cand[1],'abs_lo':cand[2],'abs_hi':cand[3],'conf_lo':cand[4],'conf_hi':cand[5],'conf_strength':cand[6],
       'mean_psnr':mean(v,'v02_psnr'),'mean_gap_vs_bilinear':mean(v,'gap_bilinear'),'mean_gap_vs_v01':mean(v,'gap_v01'),'worst_gap_vs_bilinear':min(x['gap_bilinear'] for x in v),
       'better_than_bilinear_cases':sum(x['gap_bilinear']>0 for x in v),'better_than_v01_cases':sum(x['gap_v01']>0 for x in v),'bg_struct_retention':ret,
       'bg_mean_gap':mean(bg,'gap_bilinear'),'v1a_mean_gap':mean(va,'gap_bilinear'),'bg_worst_gap':min(x['gap_bilinear'] for x in bg),'v1a_worst_gap':min(x['gap_bilinear'] for x in va),'mean_gate':mean(v,'gate_mean')}
    return v,r


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--bgrid',required=True);ap.add_argument('--v1a',required=True);ap.add_argument('--out',required=True);a=ap.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    train=[];hold=[]
    for proto,path in [('B_GRID',Path(a.bgrid)),('V1_A',Path(a.v1a))]:
        rows=list(csv.DictReader((path/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for n,r in enumerate(rows,1):
            c=prep(path,r,proto);(hold if r.get('crop_tag')=='C' else train).append(c);print(f'precompute {proto} {n:02d}/{len(rows)} {r["case_id"]}')
    C=candidates();print('candidates',len(C));P,G=matrix(train,C);recs=[]
    for i,c in enumerate(C):
        _,r=summary(i,c,train,P,G)
        r['score']=r['mean_psnr']-12*max(0,-.15-r['worst_gap_vs_bilinear'])-6*max(0,.94-r['bg_struct_retention'])
        recs.append(r)
    feasible=[r for r in recs if r['worst_gap_vs_bilinear']>=-.15 and r['bg_struct_retention']>=.94]
    if feasible:sel=max(feasible,key=lambda r:r['mean_psnr'])
    else:sel=max(recs,key=lambda r:r['score'])
    ct=(sel['rel_lo'],sel['rel_hi'],sel['abs_lo'],sel['abs_hi'],sel['conf_lo'],sel['conf_hi'],sel['conf_strength']);idx=C.index(ct)
    tr_cases,tr=summary(idx,ct,train,P,G);HP,HG=matrix(hold,[ct]);ho_cases,ho=summary(0,ct,hold,HP,HG)
    recs.sort(key=lambda r:r['score'],reverse=True)
    with (out/'TRAIN_SWEEP.csv').open('w',newline='',encoding='utf-8') as f:w=csv.DictWriter(f,fieldnames=list(recs[0]));w.writeheader();w.writerows(recs)
    with (out/'TRAIN_SELECTED_PER_CASE.csv').open('w',newline='',encoding='utf-8') as f:w=csv.DictWriter(f,fieldnames=list(tr_cases[0]));w.writeheader();w.writerows(tr_cases)
    with (out/'HOLDOUT_C_PER_CASE.csv').open('w',newline='',encoding='utf-8') as f:w=csv.DictWriter(f,fieldnames=list(ho_cases[0]));w.writeheader();w.writerows(ho_cases)
    result={'protocol':'LAB03_CONFLICT_AB_TRAIN_C_HOLDOUT','selection_used_holdout':False,'candidate_count':len(C),'feasible_count':len(feasible),'selected':sel,'train':tr,'holdout_C':ho,'top_training_score':recs[:20]}
    (out/'SUMMARY.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,indent=2))

if __name__=='__main__':main()

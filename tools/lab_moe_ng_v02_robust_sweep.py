#!/usr/bin/env python3
"""Robust PTAR-NG MoE v02 cheap-gate sweep.

Selection protocol is deliberately non-leaky:
  * tune only on crop tags A+B from BOTH PTAR_PERCEPTUAL_V1_B_GRID and V1_A;
  * choose one candidate using training metrics only;
  * evaluate crop tag C only after selection.

All candidates preserve the cheap product form and add no texture fetch:
  gRel = saturate((relCurv-relLo)/(relHi-relLo))
  gAbs = saturate((absCurv-absLo)/(absHi-absLo))
  gate = gRel*gAbs
"""
import argparse,csv,itertools,json,math
from pathlib import Path
import numpy as np
from lab_moe_ng_v02_sweep import components,load,sat

STRUCT_FAMILIES={'curves_high_frequency','raster_repeated','thin_oblique_edges','ui_text','silhouette_edge','pixel_art'}
BATCH=40


def psnr_from_mse(m):
    m=float(m)
    return float('inf') if m<=0.0 else 10.0*math.log10(1.0/m)


def prep_case(corpus,row,protocol):
    hr=load(corpus/row['reference_path']).astype(np.float32)
    lr=load(corpus/row['input_path']).astype(np.float32)
    b,m,ac,rc=components(lr)
    e=(b-hr).astype(np.float32)
    d=(m-b).astype(np.float32)
    # Reduce RGB once. Candidate evaluation then operates on one scalar plane.
    q0=np.mean(e*e,axis=2,dtype=np.float32)
    q1=np.mean(np.float32(2.0)*e*d,axis=2,dtype=np.float32)
    q2=np.mean(d*d,axis=2,dtype=np.float32)
    pb=psnr_from_mse(float(q0.mean()))
    pm=psnr_from_mse(float((q0+q1+q2).mean()))
    return {
        'protocol':protocol,'case_id':row['case_id'],'family':row['family'],
        'crop_tag':row.get('crop_tag',''),'pb':pb,'pm':pm,
        'ac':ac.astype(np.float32),'rc':rc.astype(np.float32),
        'q0':q0,'q1':q1,'q2':q2,
    }


def candidate_grid():
    rel_los=[0.0,0.05,0.10,0.20,0.30]
    rel_his=[0.60,0.75,0.90,1.10,1.30]
    abs_los=[0.0,0.005,0.010,0.020,0.040]
    abs_his=[0.16,0.20,0.24,0.28,0.34,0.40]
    out=[]
    for rlo,rhi,alo,ahi in itertools.product(rel_los,rel_his,abs_los,abs_his):
        if rhi>rlo and ahi>alo:
            out.append((rlo,rhi,alo,ahi))
    return out


def eval_matrix(cases,cands):
    n=len(cands); k=len(cases)
    ps=np.empty((n,k),dtype=np.float64)
    gate_mean=np.empty((n,k),dtype=np.float32)
    for j,c in enumerate(cases):
        ac=c['ac'];rc=c['rc'];q0=c['q0'];q1=c['q1'];q2=c['q2']
        for s in range(0,n,BATCH):
            batch=cands[s:s+BATCH]
            rlo=np.asarray([x[0] for x in batch],dtype=np.float32)[:,None,None]
            rhi=np.asarray([x[1] for x in batch],dtype=np.float32)[:,None,None]
            alo=np.asarray([x[2] for x in batch],dtype=np.float32)[:,None,None]
            ahi=np.asarray([x[3] for x in batch],dtype=np.float32)[:,None,None]
            gr=sat((rc[None,:,:]-rlo)/(rhi-rlo))
            ga=sat((ac[None,:,:]-alo)/(ahi-alo))
            g=(gr*ga).astype(np.float32)
            mse=np.mean(q0[None,:,:]+q1[None,:,:]*g+q2[None,:,:]*g*g,axis=(1,2),dtype=np.float64)
            ps[s:s+len(batch),j]=np.asarray([psnr_from_mse(x) for x in mse])
            gate_mean[s:s+len(batch),j]=np.mean(g,axis=(1,2),dtype=np.float64)
    return ps,gate_mean


def summarize_candidate(idx,cand,cases,ps,gm):
    vals=[]
    for j,c in enumerate(cases):
        vals.append({
            'protocol':c['protocol'],'case_id':c['case_id'],'family':c['family'],'crop_tag':c['crop_tag'],
            'bilinear_psnr':c['pb'],'v01_psnr':c['pm'],'v02_psnr':float(ps[idx,j]),
            'gap_bilinear':float(ps[idx,j]-c['pb']),'gap_v01':float(ps[idx,j]-c['pm']),
            'gate_mean':float(gm[idx,j]),
        })
    mean=lambda key:sum(x[key] for x in vals)/len(vals)
    bg=[x for x in vals if x['protocol']=='B_GRID']
    a=[x for x in vals if x['protocol']=='V1_A']
    struct_bg=[x for x in bg if x['family'] in STRUCT_FAMILIES and x['v01_psnr']>x['bilinear_psnr']]
    def retention(rr):
        den=sum(x['v01_psnr']-x['bilinear_psnr'] for x in rr)
        num=sum(x['v02_psnr']-x['bilinear_psnr'] for x in rr)
        return num/den if den>1e-12 else 1.0
    return vals,{
        'rel_lo':cand[0],'rel_hi':cand[1],'abs_lo':cand[2],'abs_hi':cand[3],
        'mean_psnr':mean('v02_psnr'),
        'mean_gap_vs_bilinear':mean('gap_bilinear'),
        'mean_gap_vs_v01':mean('gap_v01'),
        'worst_gap_vs_bilinear':min(x['gap_bilinear'] for x in vals),
        'better_than_bilinear_cases':sum(x['gap_bilinear']>0 for x in vals),
        'better_than_v01_cases':sum(x['gap_v01']>0 for x in vals),
        'bg_struct_positive_v01_retention':retention(struct_bg),
        'bg_mean_gap_vs_bilinear':sum(x['gap_bilinear'] for x in bg)/len(bg),
        'v1a_mean_gap_vs_bilinear':sum(x['gap_bilinear'] for x in a)/len(a),
        'bg_worst_gap_vs_bilinear':min(x['gap_bilinear'] for x in bg),
        'v1a_worst_gap_vs_bilinear':min(x['gap_bilinear'] for x in a),
        'mean_gate':mean('gate_mean'),
    }


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--bgrid',required=True);ap.add_argument('--v1a',required=True);ap.add_argument('--out',required=True)
    args=ap.parse_args();out=Path(args.out);out.mkdir(parents=True,exist_ok=True)
    corpora=[('B_GRID',Path(args.bgrid)),('V1_A',Path(args.v1a))]
    train=[];hold=[]
    for protocol,corpus in corpora:
        rows=list(csv.DictReader((corpus/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for i,r in enumerate(rows,1):
            c=prep_case(corpus,r,protocol)
            (hold if r.get('crop_tag')=='C' else train).append(c)
            print(f'precompute {protocol} {i:02d}/{len(rows)} {r["case_id"]} split={"holdout" if r.get("crop_tag")=="C" else "train"}')
    cands=candidate_grid();print(f'candidates={len(cands)} train_cases={len(train)} holdout_cases={len(hold)}')
    train_ps,train_gm=eval_matrix(train,cands)
    records=[]
    for i,c in enumerate(cands):
        _,r=summarize_candidate(i,c,train,train_ps,train_gm)
        # Selection uses TRAINING ONLY. Main hard objective is to eliminate large
        # regressions versus directional bilinear across both degradation protocols.
        r['score']=(r['mean_psnr']
                    -12.0*max(0.0,-0.15-r['worst_gap_vs_bilinear'])
                    -5.0*max(0.0,0.94-r['bg_struct_positive_v01_retention']))
        records.append(r)
    feasible=[r for r in records if r['worst_gap_vs_bilinear']>=-0.15 and r['bg_struct_positive_v01_retention']>=0.94]
    pool=feasible if feasible else records
    selected=max(pool,key=lambda r:(r['mean_psnr'],r['score'])) if feasible else max(pool,key=lambda r:r['score'])
    selected_c=(selected['rel_lo'],selected['rel_hi'],selected['abs_lo'],selected['abs_hi'])
    selected_i=cands.index(selected_c)
    # Holdout is evaluated only AFTER selection; it has no influence on selected_i.
    hold_ps,hold_gm=eval_matrix(hold,[selected_c])
    train_cases,train_summary=summarize_candidate(selected_i,selected_c,train,train_ps,train_gm)
    hold_cases,hold_summary=summarize_candidate(0,selected_c,hold,hold_ps,hold_gm)
    records.sort(key=lambda r:r['score'],reverse=True)
    with (out/'TRAIN_SWEEP.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(records[0]));w.writeheader();w.writerows(records)
    with (out/'TRAIN_SELECTED_PER_CASE.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(train_cases[0]));w.writeheader();w.writerows(train_cases)
    with (out/'HOLDOUT_C_PER_CASE.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(hold_cases[0]));w.writeheader();w.writerows(hold_cases)
    result={
        'protocol':'AB_TRAIN_C_HOLDOUT_BOTH_INPUT_PROTOCOLS',
        'candidate_count':len(cands),'feasible_count':len(feasible),
        'selection_used_holdout':False,
        'selected':selected,
        'train':train_summary,
        'holdout_C':hold_summary,
        'top_training_score':records[:20],
    }
    (out/'SUMMARY.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()

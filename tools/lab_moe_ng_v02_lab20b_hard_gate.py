#!/usr/bin/env python3
"""LAB20B: cost-oriented hard texture-admission router.

Selection is A+B only. The candidate deliberately replaces LAB18's soft
normalized admission with three threshold predicates that can be implemented
in HLSL without division/saturate ramps:

  low coherence:  |gx-gy| <= kc * (|gx|+|gy|+eps)
  outer balance:  min(|d0|,|d2|) >= kb * (max(|d0|,|d2|)+eps)
  central support:|d1| >= kt * (|d0|+|d1|+|d2|+eps)

When all three are true, a fixed MC strength is used; otherwise v01 is returned.
The reused C, consumed LAB16 and consumed LAB19 sets are architecture screens
only and never participate in parameter ranking. No texture fetch is added.
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

import lab_moe_ng_v02_feature_audit as q14
import lab_moe_ng_v02_fresh_validation as q16
import lab_moe_ng_v02_hermite_sweep as h5
import lab_moe_ng_v02_postfreeze_cases as q19
import lab_moe_ng_v02_v01_quality as q10

F=np.float32
EPS=F(1e-6)
STRUCT=q16.STRUCT_FAMILIES if hasattr(q16,'STRUCT_FAMILIES') else {
    'curves_high_frequency','raster_repeated','thin_oblique_edges','ui_text','silhouette_edge','pixel_art'
}


def psnr(a,b):
    d=np.asarray(a,dtype=np.float64)-np.asarray(b,dtype=np.float64)
    m=float(np.mean(d*d,dtype=np.float64))
    return float('inf') if m<=0.0 else 10.0*math.log10(1.0/m)


def candidates():
    # 6*5*5*4 = 600 cost-oriented candidates, precommitted by LAB20 plan.
    for kc,kb,kt,s in itertools.product(
        [.60,.70,.75,.80,.85,.90],
        [.05,.10,.15,.20,.25],
        [.05,.10,.15,.20,.25],
        [.25,.375,.50,.625],
    ):
        yield {'coh_max':kc,'outer_min':kb,'central_min':kt,'strength':s}


def hard_mask(feat,spec):
    # q14 features are algebraically the normalized forms of the intended HLSL
    # predicates. Use <=/>= here; the runtime shader will use cross-multiplied
    # raw inequalities to eliminate the divisions.
    return (
        (feat['coh'] <= F(spec['coh_max'])) &
        (feat['outer_balance'] >= F(spec['outer_min'])) &
        (feat['central'] >= F(spec['central_min']))
    )


def gate(feat,spec):
    return (hard_mask(feat,spec).astype(np.float32)*F(spec['strength']))[...,None]


def prepare(corpus,row,proto):
    lr=h5.load(corpus/row['input_path']); ref=h5.load(corpus/row['reference_path'])
    v01,mc=q10.reconstruct_pair(lr)
    return {
        'protocol':proto,'case_id':row['case_id'],'source_id':row.get('source_id',''),
        'family':row['family'],'crop_tag':row.get('crop_tag',''),'group':'corpus',
        'ref':ref,'v01':v01,'mc':mc,'feat':q14.features(lr),
        'p01':psnr(v01,ref),'pmc':psnr(mc,ref)
    }


def proc_case(proto,case_id,family,group,hr):
    hr=np.asarray(hr,dtype=np.float32); lr=q19.downsample(hr)
    v01,mc=q10.reconstruct_pair(lr)
    return {
        'protocol':proto,'case_id':case_id,'source_id':group,'family':family,
        'crop_tag':'CONSUMED','group':group,'ref':hr,'v01':v01,'mc':mc,
        'feat':q14.features(lr),'p01':psnr(v01,hr),'pmc':psnr(mc,hr)
    }


def prepare_lab16():
    out=[]
    for family,group,fn in q16.GENERATORS:
        for i in range(6):
            out.append(proc_case('LAB16_CONSUMED',f'LAB16-{family}-{i+1:02d}',family,group,q16.clip01(fn(i))))
    return out


def prepare_lab19():
    out=[]
    for family,group,fn in q19.GENERATORS:
        for i in range(6):
            out.append(proc_case('LAB19_CONSUMED',f'LAB19-{family}-{i+1:02d}',family,group,q19.clip01(fn(i))))
    return out


def evaluate(cases,spec):
    rows=[]
    for c in cases:
        g=gate(c['feat'],spec)
        img=np.clip(c['v01']+(c['mc']-c['v01'])*g,F(0),F(1)).astype(np.float32)
        p=psnr(img,c['ref'])
        rows.append({
            'protocol':c['protocol'],'case_id':c['case_id'],'source_id':c['source_id'],
            'family':c['family'],'crop_tag':c['crop_tag'],'group':c['group'],
            'v01_psnr':c['p01'],'mc_psnr':c['pmc'],'hard_psnr':p,
            'gap_v01':p-c['p01'],'gap_mc':p-c['pmc'],
            'gate_mean':float(np.mean(g,dtype=np.float64)),
            'active_fraction':float(np.mean(g[...,0]>F(0),dtype=np.float64)),
        })
    return rows


def avg(xs): return float(sum(xs)/len(xs)) if xs else float('nan')


def group_means(rows,key):
    d=defaultdict(list)
    for r in rows: d[r[key]].append(r['gap_v01'])
    return {k:avg(v) for k,v in d.items()}


def block(rows):
    gaps=[r['gap_v01'] for r in rows]
    struct=[r for r in rows if r['family'] in STRUCT or r['group']=='structural']
    fam=group_means(rows,'family'); src=group_means(rows,'source_id')
    return {
        'cases':len(rows),'mean_gap_v01':avg(gaps),'worst_gap_v01':min(gaps),
        'wins_v01':sum(x>0 for x in gaps),'struct_mean_gap_v01':avg([r['gap_v01'] for r in struct]),
        'struct_worst_gap_v01':min([r['gap_v01'] for r in struct]) if struct else float('nan'),
        'family_balanced_mean_gap_v01':avg(list(fam.values())),'worst_family_mean_gap_v01':min(fam.values()),
        'source_balanced_mean_gap_v01':avg(list(src.values())),'worst_source_mean_gap_v01':min(src.values()),
        'mean_gate':avg([r['gate_mean'] for r in rows]),'mean_active_fraction':avg([r['active_fraction'] for r in rows]),
        'family_means':fam,
    }


def summarize_train(rows,spec):
    splits={
        'all':rows,
        'A':[r for r in rows if r['crop_tag']=='A'],
        'B':[r for r in rows if r['crop_tag']=='B'],
        'B_GRID':[r for r in rows if r['protocol']=='B_GRID'],
        'V1_A':[r for r in rows if r['protocol']=='V1_A'],
    }
    s={'spec':spec,**{k:block(v) for k,v in splits.items()}}
    x,a,b,p,q=s['all'],s['A'],s['B'],s['B_GRID'],s['V1_A']
    feasible=(
        x['mean_gap_v01']>=.15 and x['worst_gap_v01']>=-.15
        and x['struct_mean_gap_v01']>=-.02 and x['struct_worst_gap_v01']>=-.15
        and a['mean_gap_v01']>=.04 and b['mean_gap_v01']>=.04
        and p['mean_gap_v01']>=.04 and q['mean_gap_v01']>=.04
        and x['worst_family_mean_gap_v01']>=-.05
        and x['worst_source_mean_gap_v01']>=-.05
    )
    robust=min(a['mean_gap_v01'],b['mean_gap_v01'],p['mean_gap_v01'],q['mean_gap_v01'])
    # Favor quality first but explicitly prefer lower active MC population when
    # two candidates are close, because LAB20 is a cost-reduction experiment.
    score=(.28*x['mean_gap_v01']+.18*x['family_balanced_mean_gap_v01']+.16*x['source_balanced_mean_gap_v01']+
           .16*x['struct_mean_gap_v01']+.14*robust+.04*x['worst_family_mean_gap_v01']+
           .04*x['worst_source_mean_gap_v01']-.025*x['mean_active_fraction'])
    s['feasible']=bool(feasible); s['score']=float(score)
    return s


def screen(name,rows):
    b=block(rows)
    # Consumed sets can falsify a candidate but cannot establish final validity.
    checks={
        'mean_nonnegative':b['mean_gap_v01']>=0.0,
        'worst_ge_minus_0p20':b['worst_gap_v01']>=-.20,
        'struct_mean_ge_minus_0p05':(math.isnan(b['struct_mean_gap_v01']) or b['struct_mean_gap_v01']>=-.05),
        'worst_family_mean_ge_minus_0p10':b['worst_family_mean_gap_v01']>=-.10,
    }
    return {'name':name,'block':b,'checks':checks,'pass':all(checks.values())}


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
    train=[]; cdiag=[]
    for proto,p in [('B_GRID',Path(a.bgrid)),('V1_A',Path(a.v1a))]:
        rr=list(csv.DictReader((p/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for i,r in enumerate(rr,1):
            c=prepare(p,r,proto); (cdiag if r.get('crop_tag')=='C' else train).append(c); print(proto,i,len(rr),r['case_id'])

    ranked=[]; specs=list(candidates())
    for i,spec in enumerate(specs,1):
        ranked.append(summarize_train(evaluate(train,spec),spec))
        if i%100==0: print('candidates',i,'/',len(specs))
    feasible=[r for r in ranked if r['feasible']]
    selected=max(feasible,key=lambda r:(r['score'],r['all']['mean_gap_v01'],-r['all']['mean_active_fraction'])) if feasible else max(ranked,key=lambda r:r['score'])
    spec=selected['spec']

    train_rows=evaluate(train,spec); c_rows=evaluate(cdiag,spec)
    l16_rows=evaluate(prepare_lab16(),spec); l19_rows=evaluate(prepare_lab19(),spec)
    screens=[screen('REUSED_C',c_rows),screen('LAB16_CONSUMED',l16_rows),screen('LAB19_CONSUMED',l19_rows)]

    write_csv(out/'TRAIN_SELECTED_PER_CASE.csv',train_rows)
    write_csv(out/'REUSED_C_SCREEN.csv',c_rows)
    write_csv(out/'LAB16_CONSUMED_SCREEN.csv',l16_rows)
    write_csv(out/'LAB19_CONSUMED_SCREEN.csv',l19_rows)
    ranked.sort(key=lambda r:r['score'],reverse=True)
    flat=[]
    for r in ranked:
        z={'score':r['score'],'feasible':r['feasible'],**r['spec']}
        for name in ['all','A','B','B_GRID','V1_A']:
            for k,v in r[name].items():
                if k!='family_means': z[f'{name}_{k}']=v
        flat.append(z)
    write_csv(out/'SWEEP.csv',flat)

    result={
        'protocol':'LAB20B_HARD_GATE_AB_ONLY_SELECTION',
        'selection_used_C':False,'selection_used_LAB16':False,'selection_used_LAB19':False,
        'C_LAB16_LAB19_are_consumed_screens':True,'texture_fetch_delta':0,
        'candidate_count':len(specs),'feasible_count':len(feasible),'selected_train':selected,
        'screens':screens,'architecture_screen_pass':all(s['pass'] for s in screens),
        'freeze_allowed':bool(selected['feasible'] and all(s['pass'] for s in screens)),
        'runtime_form':'cross-multiplied three-predicate hard branch, fixed strength, MC only in admitted branch',
        'next_step':'If freeze_allowed: freeze unchanged, then author a new LAB21 validation corpus after the freeze commit.',
        'top':ranked[:30],
    }
    (out/'SUMMARY.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))
    if not result['freeze_allowed']:
        raise SystemExit('LAB20B no freezeable candidate')

if __name__=='__main__': main()

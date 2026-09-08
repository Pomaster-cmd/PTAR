#!/usr/bin/env python3
"""PTAR-NG MoE v02 LAB02 selected-candidate validator.

Validates LAB02-CHEAP-PRODUCT independently of the parameter sweep:
  gRel = saturate(relCurv / 0.90)
  gAbs = saturate((absCurv - 0.01) / 0.23)
  gate = gRel * gAbs
The texture/sample geometry is reused from the v01 semantic mirror in
lab_moe_ng_v02_sweep.py; only the selected gate is evaluated here.
"""
import argparse,csv,json,math
from pathlib import Path
import numpy as np
from lab_moe_ng_v02_sweep import components,load,psnr,sat

REL_HI=np.float32(0.90)
ABS_LO=np.float32(0.01)
ABS_SPAN=np.float32(0.23)
EPS=np.float32(1e-6)


def gate_selected(ac,rc):
    return (sat(rc/REL_HI)*sat((ac-ABS_LO)/ABS_SPAN)).astype(np.float32)


def render_selected(lr):
    b,m,ac,rc=components(np.asarray(lr,dtype=np.float32))
    g=gate_selected(ac,rc)
    return b,m,(b+(m-b)*g[...,None]).astype(np.float32),g


def scalar_parity(seed=260908,n=50000):
    rng=np.random.default_rng(seed)
    ac=rng.random(n,dtype=np.float32)
    rc=rng.random(n,dtype=np.float32)*np.float32(2.0)
    vec=gate_selected(ac,rc)
    scalar=np.empty(n,dtype=np.float32)
    for i in range(n):
        gr=np.float32(min(1.0,max(0.0,float(np.float32(rc[i]/REL_HI)))))
        ga=np.float32(min(1.0,max(0.0,float(np.float32((ac[i]-ABS_LO)/ABS_SPAN)))))
        scalar[i]=np.float32(gr*ga)
    d=np.abs(vec-scalar)
    return {'samples':n,'max_abs_gate_diff':float(d.max()),'exact_float32_matches':int(np.count_nonzero(d==0))}


def analytic_sine(freq,amp=0.0125):
    xh=np.arange(288,dtype=np.float32)
    xl=np.arange(192,dtype=np.float32)*np.float32(1.5)
    hr=(np.float32(.5)+np.float32(amp)*np.sin(np.float32(2*math.pi*freq)*xh)).clip(0,1)
    lr=(np.float32(.5)+np.float32(amp)*np.sin(np.float32(2*math.pi*freq)*xl)).clip(0,1)
    hr=np.repeat(np.repeat(hr[None,:,None],288,axis=0),3,axis=2)
    lr=np.repeat(np.repeat(lr[None,:,None],192,axis=0),3,axis=2)
    return hr,lr


def analytic_soft_edge(width=2.0):
    xh=np.arange(288,dtype=np.float32)-np.float32(143.5)
    xl=np.arange(192,dtype=np.float32)*np.float32(1.5)-np.float32(143.5)
    hr=np.float32(.1)+np.float32(.8)/(np.float32(1)+np.exp(-xh/np.float32(width)))
    lr=np.float32(.1)+np.float32(.8)/(np.float32(1)+np.exp(-xl/np.float32(width)))
    hr=np.repeat(np.repeat(hr[None,:,None],288,axis=0),3,axis=2)
    lr=np.repeat(np.repeat(lr[None,:,None],192,axis=0),3,axis=2)
    return hr,lr


def analytics():
    out=[]
    for f in (.03,.05,.07,.09,.12,.15,.18,.21,.25,.29):
        hr,lr=analytic_sine(f);b,m,v,g=render_selected(lr)
        out.append({'probe':f'freq_{f:.2f}','bilinear_psnr':psnr(hr,b),'v01_psnr':psnr(hr,m),'v02_psnr':psnr(hr,v),'v02_minus_bilinear':psnr(hr,v)-psnr(hr,b),'gate_mean':float(g.mean()),'gate_p95':float(np.quantile(g,.95))})
    for w in (1.0,1.5,2.0,3.0,4.0):
        hr,lr=analytic_soft_edge(w);b,m,v,g=render_selected(lr)
        out.append({'probe':f'soft_edge_w{w:g}','bilinear_psnr':psnr(hr,b),'v01_psnr':psnr(hr,m),'v02_psnr':psnr(hr,v),'v02_minus_bilinear':psnr(hr,v)-psnr(hr,b),'gate_mean':float(g.mean()),'gate_p95':float(np.quantile(g,.95))})
    return out


def validate_corpus(corpus):
    rows=list(csv.DictReader((corpus/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
    cases=[]
    for i,r in enumerate(rows,1):
        hr=load(corpus/r['reference_path']);lr=load(corpus/r['input_path'])
        b,m,v,g=render_selected(lr)
        pb,pm,pv=psnr(hr,b),psnr(hr,m),psnr(hr,v)
        cases.append({'case_id':r['case_id'],'family':r['family'],'bilinear_psnr':pb,'v01_psnr':pm,'v02_psnr':pv,'v02_gain_vs_bilinear':pv-pb,'v02_minus_v01':pv-pm,'gate_mean':float(g.mean()),'gate_p95':float(np.quantile(g,.95))})
        print(f'[{i:02d}/{len(rows)}] {r["case_id"]}: B={pb:.4f} V1={pm:.4f} V2={pv:.4f}')
    mean=lambda k:sum(x[k] for x in cases)/len(cases)
    return cases,{
        'corpus':corpus.name,'cases':len(cases),
        'mean_bilinear_psnr':mean('bilinear_psnr'),
        'mean_v01_psnr':mean('v01_psnr'),
        'mean_v02_psnr':mean('v02_psnr'),
        'mean_v02_minus_v01':mean('v02_minus_v01'),
        'mean_v02_gain_vs_bilinear':mean('v02_gain_vs_bilinear'),
        'v02_better_than_v01_cases':sum(x['v02_psnr']>x['v01_psnr'] for x in cases),
        'v02_better_than_bilinear_cases':sum(x['v02_psnr']>x['bilinear_psnr'] for x in cases),
        'worst_v02_gap_vs_bilinear':min(x['v02_gain_vs_bilinear'] for x in cases),
        'gate':{'rel_lo':0.0,'rel_hi':0.90,'abs_lo':0.01,'abs_hi':0.24,'fusion':'product'}
    }


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--corpus',action='append',required=True);ap.add_argument('--out',required=True);a=ap.parse_args()
    out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    parity=scalar_parity();ana=analytics();summaries=[]
    (out/'PARITY.json').write_text(json.dumps(parity,indent=2)+'\n',encoding='utf-8')
    (out/'ANALYTIC.json').write_text(json.dumps(ana,indent=2)+'\n',encoding='utf-8')
    for c in a.corpus:
        corpus=Path(c);cases,summary=validate_corpus(corpus);summaries.append(summary)
        stem=corpus.name
        with (out/f'{stem}_PER_CASE.csv').open('w',newline='',encoding='utf-8') as f:
            w=csv.DictWriter(f,fieldnames=list(cases[0]));w.writeheader();w.writerows(cases)
        (out/f'{stem}_SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    final={'candidate':'LAB02-CHEAP-PRODUCT-ABS01','parity':parity,'corpora':summaries,'analytic':ana}
    (out/'SUMMARY.json').write_text(json.dumps(final,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(final,indent=2))

if __name__=='__main__':main()

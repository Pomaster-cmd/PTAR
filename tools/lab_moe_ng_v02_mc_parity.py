#!/usr/bin/env python3
"""LAB07 always-on MC CPU/HLSL algebra + corpus parity gate.

This does not execute a GPU shader. It independently mirrors the committed
SM5 HLSL math in float32 and compares it with the LAB07 always-on MC CPU
reference over both perceptual corpora plus a deterministic algebra stress
set. No curvature router remains in the selected architecture.
"""
import argparse,csv,json
from pathlib import Path
import numpy as np
import lab_moe_ng_v02_hermite_sweep as ref

F=np.float32
TOL=3.0e-6


def mc_hlsl(a,b):
    avg=F(.5)*(a+b)
    lim=F(2)*np.minimum(np.abs(a),np.abs(b))
    limited=np.minimum(np.maximum(avg,-lim),lim).astype(np.float32)
    same=(a*b>=F(0)).astype(np.float32)
    return (limited*same).astype(np.float32)


def h13(f0,f1,m0,m1):
    h=(F(20)*f0+F(4)*m0+F(7)*f1-F(2)*m1)*F(1/27)
    return np.minimum(np.maximum(h,np.minimum(f0,f1)),np.maximum(f0,f1)).astype(np.float32)


def h23(f0,f1,m0,m1):
    h=(F(7)*f0+F(2)*m0+F(20)*f1-F(4)*m1)*F(1/27)
    return np.minimum(np.maximum(h,np.minimum(f0,f1)),np.maximum(f0,f1)).astype(np.float32)


def mirror(lr):
    oy,ox=np.mgrid[0:288,0:288]; ox=ox.astype(np.uint32); oy=oy.astype(np.uint32)
    sx=ox.astype(np.float32)*F(2/3); sy=oy.astype(np.float32)*F(2/3)
    fx=np.floor(sx).astype(np.int32); fy=np.floor(sy).astype(np.int32)
    h,w,_=lr.shape
    x0=np.clip(fx,0,w-1); x1=np.clip(fx+1,0,w-1); y0=np.clip(fy,0,h-1); y1=np.clip(fy+1,0,h-1)
    tl=lr[y0,x0,1]; tr=lr[y0,x1,1]; bl=lr[y1,x0,1]; br=lr[y1,x1,1]
    gx=(tr+br)-(tl+bl); gy=(bl+br)-(tl+tr); ux=np.abs(gx)>=np.abs(gy)
    bx=np.where(ux,fx.astype(np.float32),sx); by=np.where(ux,sy,fy.astype(np.float32))
    ax=ux.astype(np.float32); ay=(~ux).astype(np.float32)
    fm1=ref.sample(lr,bx-ax,by-ay); f0=ref.sample(lr,bx,by); f1=ref.sample(lr,bx+ax,by+ay); f2=ref.sample(lr,bx+F(2)*ax,by+F(2)*ay)
    phase=np.where(ux,ox%3,oy%3)
    d0=f0-fm1; d1=f1-f0; d2=f2-f1
    m0=mc_hlsl(d0,d1); m1=mc_hlsl(d1,d2)
    mc=np.where((phase==1)[...,None],h23(f0,f1,m0,m1),h13(f0,f1,m0,m1)).astype(np.float32)
    return np.where((phase==0)[...,None],f0,mc).astype(np.float32)


def reference(lr):
    base,E,ac,rc=ref.planes(lr)
    return E['mc'].astype(np.float32)


def random_algebra():
    rng=np.random.default_rng(0x50544152)
    a=rng.uniform(-1,1,(200000,4)).astype(np.float32)
    b=rng.uniform(-1,1,(200000,4)).astype(np.float32)
    mc=float(np.max(np.abs(ref.mc(a,b)-mc_hlsl(a,b))))
    fm1=rng.random((100000,4),dtype=np.float32); f0=rng.random((100000,4),dtype=np.float32)
    f1=rng.random((100000,4),dtype=np.float32); f2=rng.random((100000,4),dtype=np.float32)
    m0=ref.mc(f0-fm1,f1-f0); m1=ref.mc(f1-f0,f2-f1)
    e13=float(np.max(np.abs(ref.hermite(f0,f1,m0,m1,np.full(100000,F(1/3),np.float32))-h13(f0,f1,m0,m1))))
    e23=float(np.max(np.abs(ref.hermite(f0,f1,m0,m1,np.full(100000,F(2/3),np.float32))-h23(f0,f1,m0,m1))))
    return {'mc_limiter_max_abs':mc,'hermite13_max_abs':e13,'hermite23_max_abs':e23}


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--bgrid',required=True); ap.add_argument('--v1a',required=True); ap.add_argument('--out',required=True); a=ap.parse_args()
    out=Path(a.out); out.mkdir(parents=True,exist_ok=True)
    alg=random_algebra(); rows=[]; worst=0.0
    for proto,p in [('B_GRID',Path(a.bgrid)),('V1_A',Path(a.v1a))]:
        manifest=list(csv.DictReader((p/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for i,r in enumerate(manifest,1):
            lr=ref.load(p/r['input_path']); R=reference(lr); H=mirror(lr)
            d=np.abs(R-H); mx=float(d.max()); mean=float(d.mean()); worst=max(worst,mx)
            rows.append({'protocol':proto,'case_id':r['case_id'],'crop_tag':r.get('crop_tag',''),'max_abs':mx,'mean_abs':mean})
            print(f'{proto} {i:02d}/{len(manifest)} {r["case_id"]} max_abs={mx:.9g}')
    with (out/'PER_CASE.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    summary={'protocol':'LAB07_ALWAYS_ON_MC_CPU_HLSL_FLOAT32_PARITY','gpu_executed':False,'tolerance_abs':TOL,'random_algebra':alg,'corpus_cases':len(rows),'corpus_worst_max_abs':worst,'pass':max([worst,*alg.values()])<=TOL}
    (out/'SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(summary,indent=2))
    if not summary['pass']: raise SystemExit('PARITY FAIL')

if __name__=='__main__': main()

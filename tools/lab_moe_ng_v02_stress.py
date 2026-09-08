#!/usr/bin/env python3
"""LAB08 deterministic stress probes for LAB07 always-on MC.

Hard gates cover mathematical safety: finiteness, [f0,f1] shape bounds,
constant reproduction, affine-ramp accuracy, low-contrast stability and global
[0,1] range. A separate analytic sine sweep is diagnostic only: it maps where
always-on MC improves or worsens reconstruction versus grid bilinear up to the
LR Nyquist limit. The frequency sweep is not used to tune a parameter here.
"""
import argparse
import json
import math
from pathlib import Path

import numpy as np
import lab_moe_ng_v02_hermite_sweep as h5

F=np.float32
TOL=3.0e-6


def shape_random_stress(n=500000):
    rng=np.random.default_rng(0x4C41423038)
    fm1=rng.random((n,4),dtype=np.float32)
    f0=rng.random((n,4),dtype=np.float32)
    f1=rng.random((n,4),dtype=np.float32)
    f2=rng.random((n,4),dtype=np.float32)
    m0=h5.mc(f0-fm1,f1-f0); m1=h5.mc(f1-f0,f2-f1)
    results={}
    worst_bound=0.0; worst_range=0.0; finite=True
    for phase,t in [('t13',F(1/3)),('t23',F(2/3))]:
        out=h5.hermite(f0,f1,m0,m1,np.full(n,t,np.float32))
        lo=np.minimum(f0,f1); hi=np.maximum(f0,f1)
        bound=float(max(np.max(np.maximum(lo-out,0)),np.max(np.maximum(out-hi,0))))
        rngv=float(max(max(0.0,-float(out.min())),max(0.0,float(out.max())-1.0)))
        finite=finite and bool(np.isfinite(out).all())
        worst_bound=max(worst_bound,bound); worst_range=max(worst_range,rngv)
        results[phase]={'local_segment_bound_violation':bound,'range_01_violation':rngv}
    return {'samples':n,'finite':finite,'worst_local_segment_bound_violation':worst_bound,'worst_range_01_violation':worst_range,'phases':results}


def constant_stress():
    vals=np.linspace(0.0,1.0,4097,dtype=np.float32)[:,None]
    f=np.repeat(vals,4,axis=1)
    z=np.zeros_like(f)
    e=[]
    for t in (F(1/3),F(2/3)):
        out=h5.hermite(f,f,z,z,np.full(len(f),t,np.float32))
        e.append(float(np.max(np.abs(out-f))))
    return {'max_abs':max(e),'pass':max(e)<=TOL}


def affine_stress(n=250000):
    rng=np.random.default_rng(0x50544152)
    start=rng.uniform(0.15,0.35,(n,4)).astype(np.float32)
    slope=rng.uniform(-0.04,0.04,(n,4)).astype(np.float32)
    # fm1=start, f0=start+slope, f1=start+2slope, f2=start+3slope;
    # chosen ranges keep all values safely within [0,1].
    fm1=start; f0=start+slope; f1=start+F(2)*slope; f2=start+F(3)*slope
    m0=h5.mc(f0-fm1,f1-f0); m1=h5.mc(f1-f0,f2-f1)
    errs=[]
    for t in (F(1/3),F(2/3)):
        out=h5.hermite(f0,f1,m0,m1,np.full(n,t,np.float32))
        ideal=f0+t*(f1-f0)
        errs.append(float(np.max(np.abs(out-ideal))))
    return {'samples':n,'max_abs':max(errs),'pass':max(errs)<=TOL}


def low_contrast_stress(n=250000):
    rng=np.random.default_rng(0x4C4F5743)
    base=rng.uniform(0.2,0.8,(n,4)).astype(np.float32)
    eps=F(1.0e-6)
    noise=rng.uniform(-1,1,(n,4,4)).astype(np.float32)*eps
    fm1=base+noise[:,0]; f0=base+noise[:,1]; f1=base+noise[:,2]; f2=base+noise[:,3]
    m0=h5.mc(f0-fm1,f1-f0); m1=h5.mc(f1-f0,f2-f1)
    worst_bound=0.0; worst_excursion=0.0
    for t in (F(1/3),F(2/3)):
        out=h5.hermite(f0,f1,m0,m1,np.full(n,t,np.float32))
        lo=np.minimum(f0,f1); hi=np.maximum(f0,f1)
        worst_bound=max(worst_bound,float(max(np.max(np.maximum(lo-out,0)),np.max(np.maximum(out-hi,0)))))
        worst_excursion=max(worst_excursion,float(np.max(np.abs(out-base))))
    return {'samples':n,'input_epsilon':float(eps),'worst_local_segment_bound_violation':worst_bound,'worst_excursion_from_base':worst_excursion,'pass':worst_bound<=TOL and worst_excursion<=4.0e-6}


def psnr(a,b):
    d=np.asarray(a,dtype=np.float64)-np.asarray(b,dtype=np.float64)
    mse=float(np.mean(d*d))
    return float('inf') if mse<=0 else 10.0*math.log10(1.0/mse)


def tone_amplitude(y,x,f):
    A=np.stack([np.sin(2*np.pi*f*x),np.cos(2*np.pi*f*x),np.ones_like(x)],axis=1)
    coef=np.linalg.lstsq(A,np.asarray(y,dtype=np.float64),rcond=None)[0]
    return float(math.hypot(float(coef[0]),float(coef[1])))


def frequency_sweep():
    freqs=[0.025,0.05,0.10,0.15,0.20,0.25,0.30,0.35,0.40,0.45,0.49]
    phases=[0.13,0.67]
    rows=[]
    xlr=np.arange(192,dtype=np.float32)
    xhr=np.arange(288,dtype=np.float64)*(2.0/3.0)
    crop=slice(12,276)
    for f in freqs:
        for phase in phases:
            lr1=0.5+0.45*np.sin(2*np.pi*f*xlr+phase)
            lr=np.repeat(lr1[None,:,None],192,axis=0)
            lr=np.repeat(lr,3,axis=2).astype(np.float32)
            bil,E,_,_=h5.planes(lr)
            mc=E['mc']
            ref1=(0.5+0.45*np.sin(2*np.pi*f*xhr+phase)).astype(np.float32)
            ref=np.repeat(ref1[None,:,None],288,axis=0)
            ref=np.repeat(ref,3,axis=2)
            b=bil[:,crop,:]; m=mc[:,crop,:]; r=ref[:,crop,:]
            x=xhr[crop]
            bline=b[144,:,1]; mline=m[144,:,1]; rline=r[144,:,1]
            ar=tone_amplitude(rline,x,f); ab=tone_amplitude(bline,x,f); am=tone_amplitude(mline,x,f)
            rows.append({
                'frequency_cycles_per_lr_pixel':f,
                'frequency_cycles_per_hr_pixel':f*(2.0/3.0),
                'phase':phase,
                'bilinear_psnr':psnr(r,b),
                'mc_psnr':psnr(r,m),
                'mc_minus_bilinear_psnr':psnr(r,m)-psnr(r,b),
                'bilinear_tone_gain_db':20*math.log10(max(ab,1e-12)/max(ar,1e-12)),
                'mc_tone_gain_db':20*math.log10(max(am,1e-12)/max(ar,1e-12)),
            })
    by_freq=[]
    for f in freqs:
        rr=[r for r in rows if r['frequency_cycles_per_lr_pixel']==f]
        mean=lambda k:float(sum(x[k] for x in rr)/len(rr))
        by_freq.append({
            'frequency_cycles_per_lr_pixel':f,
            'frequency_cycles_per_hr_pixel':f*(2.0/3.0),
            'mean_mc_minus_bilinear_psnr':mean('mc_minus_bilinear_psnr'),
            'mean_bilinear_tone_gain_db':mean('bilinear_tone_gain_db'),
            'mean_mc_tone_gain_db':mean('mc_tone_gain_db'),
            'mean_mc_abs_tone_gain_error_db':float(sum(abs(x['mc_tone_gain_db']) for x in rr)/len(rr)),
            'mean_bilinear_abs_tone_gain_error_db':float(sum(abs(x['bilinear_tone_gain_db']) for x in rr)/len(rr)),
        })
    return {'selection_used':False,'acceptance_protocol':False,'frequencies':by_freq,'per_phase':rows}


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--out',required=True); a=ap.parse_args()
    out=Path(a.out); out.mkdir(parents=True,exist_ok=True)
    rnd=shape_random_stress(); const=constant_stress(); affine=affine_stress(); low=low_contrast_stress(); freq=frequency_sweep()
    hard={
        'random_finite':rnd['finite'],
        'random_local_bound':rnd['worst_local_segment_bound_violation']<=TOL,
        'random_range_01':rnd['worst_range_01_violation']<=TOL,
        'constant_reproduction':const['pass'],
        'affine_reproduction':affine['pass'],
        'low_contrast_stability':low['pass'],
    }
    summary={'protocol':'LAB08_ALWAYS_ON_MC_STRESS_V1','hard_invariants':hard,'hard_pass':all(hard.values()),'random_shape':rnd,'constant':const,'affine':affine,'low_contrast':low,'frequency_response':freq}
    (out/'SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(summary,indent=2))
    if not summary['hard_pass']: raise SystemExit('LAB08 hard invariant failure')

if __name__=='__main__': main()

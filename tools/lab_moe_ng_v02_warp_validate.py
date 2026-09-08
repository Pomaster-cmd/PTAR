#!/usr/bin/env python3
"""LAB07 always-on MC D3D11 WARP runtime validator.

Generates a deterministic float32 RGBA test texture and two independent CPU
references: ideal mathematical bilinear weights and D3D11's 8-bit subtexel
filtering precision. The direction decision uses the intended 2x2 green
neighborhood directly, so GatherGreen ordering/resource/phase/sample/DXBC
execution errors remain visible in the WARP comparison.
"""
import argparse
import json
from pathlib import Path
import numpy as np

F=np.float32
IN_W=24
IN_H=24
OUT_W=36
OUT_H=36
D3D11_SUBTEXEL_BITS=8
DEFAULT_TOL=2.0e-6


def quantize_subtexel(t):
    n=1 << D3D11_SUBTEXEL_BITS
    return F(np.floor(float(t)*n + 0.5)/n)


def sample_linear(img,x,y,d3d11_filter=False):
    h,w,_=img.shape
    x=F(np.clip(F(x),F(0),F(w-1)))
    y=F(np.clip(F(y),F(0),F(h-1)))
    x0=int(np.floor(x)); y0=int(np.floor(y))
    x1=min(x0+1,w-1); y1=min(y0+1,h-1)
    tx=F(x-F(x0)); ty=F(y-F(y0))
    if d3d11_filter:
        tx=quantize_subtexel(tx)
        ty=quantize_subtexel(ty)
    a=(img[y0,x0]*(F(1)-tx)+img[y0,x1]*tx).astype(np.float32)
    b=(img[y1,x0]*(F(1)-tx)+img[y1,x1]*tx).astype(np.float32)
    return (a*(F(1)-ty)+b*ty).astype(np.float32)


def mc_slope(a,b):
    avg=F(.5)*(a+b)
    lim=F(2)*np.minimum(np.abs(a),np.abs(b))
    limited=np.minimum(np.maximum(avg,-lim),lim).astype(np.float32)
    mask=(a*b>=F(0)).astype(np.float32)
    return (limited*mask).astype(np.float32)


def h13(f0,f1,m0,m1):
    h=(F(20)*f0+F(4)*m0+F(7)*f1-F(2)*m1)*F(1/27)
    return np.minimum(np.maximum(h,np.minimum(f0,f1)),np.maximum(f0,f1)).astype(np.float32)


def h23(f0,f1,m0,m1):
    h=(F(7)*f0+F(2)*m0+F(20)*f1-F(4)*m1)*F(1/27)
    return np.minimum(np.maximum(h,np.minimum(f0,f1)),np.maximum(f0,f1)).astype(np.float32)


def generate_input():
    y,x=np.mgrid[0:IN_H,0:IN_W]
    xf=x.astype(np.float32)/F(IN_W-1)
    yf=y.astype(np.float32)/F(IN_H-1)
    checker=((x//2+y//3)&1).astype(np.float32)
    diag=((x+2*y)%7<3).astype(np.float32)
    rings=(np.sin(np.sqrt((x-11.5)**2+(y-11.5)**2)*1.7)*0.5+0.5).astype(np.float32)
    rng=np.random.default_rng(0x50544152)
    noise=rng.random((IN_H,IN_W),dtype=np.float32)
    r=F(.08)+F(.42)*xf+F(.18)*checker+F(.12)*diag+F(.08)*noise
    g=F(.05)+F(.30)*yf+F(.28)*checker+F(.17)*diag+F(.10)*rings
    b=F(.04)+F(.20)*(F(1)-xf)+F(.16)*checker+F(.28)*rings+F(.08)*noise
    a=np.ones_like(r,dtype=np.float32)
    return np.clip(np.stack([r,g,b,a],axis=2),F(0),F(1)).astype(np.float32)


def cpu_reference(img,d3d11_filter):
    h,w,_=img.shape
    out=np.empty((OUT_H,OUT_W,4),dtype=np.float32)
    green=img[:,:,1]
    for oy in range(OUT_H):
        for ox in range(OUT_W):
            sx=F(ox)*F(2/3); sy=F(oy)*F(2/3)
            fx=int(np.floor(sx)); fy=int(np.floor(sy))
            x0=min(max(fx,0),w-1); x1=min(max(fx+1,0),w-1)
            y0=min(max(fy,0),h-1); y1=min(max(fy+1,0),h-1)
            tl=green[y0,x0]; tr=green[y0,x1]; bl=green[y1,x0]; br=green[y1,x1]
            gx=F((tr+br)-(tl+bl)); gy=F((bl+br)-(tl+tr))
            use_x=bool(abs(gx)>=abs(gy))
            if use_x:
                bx=F(fx); by=sy; ax=F(1); ay=F(0); phase=ox%3
            else:
                bx=sx; by=F(fy); ax=F(0); ay=F(1); phase=oy%3
            fm1=sample_linear(img,bx-ax,by-ay,d3d11_filter)
            f0 =sample_linear(img,bx,by,d3d11_filter)
            f1 =sample_linear(img,bx+ax,by+ay,d3d11_filter)
            f2 =sample_linear(img,bx+F(2)*ax,by+F(2)*ay,d3d11_filter)
            if phase==0:
                out[oy,ox]=f0
                continue
            d0=f0-fm1; d1=f1-f0; d2=f2-f1
            m0=mc_slope(d0,d1); m1=mc_slope(d1,d2)
            out[oy,ox]=h23(f0,f1,m0,m1) if phase==1 else h13(f0,f1,m0,m1)
    return out


def prepare(outdir):
    outdir.mkdir(parents=True,exist_ok=True)
    img=generate_input()
    ideal=cpu_reference(img,False)
    d3d=cpu_reference(img,True)
    img.tofile(outdir/'input.f32')
    ideal.tofile(outdir/'expected_ideal.f32')
    d3d.tofile(outdir/'expected_d3d11.f32')
    delta=np.abs(d3d-ideal)
    meta={
        'input_width':IN_W,'input_height':IN_H,
        'output_width':OUT_W,'output_height':OUT_H,
        'format':'R32G32B32A32_FLOAT',
        'd3d11_subtexel_fractional_bits':D3D11_SUBTEXEL_BITS,
        'ideal_vs_d3d11_filter_max_abs':float(delta.max()),
        'ideal_vs_d3d11_filter_mean_abs':float(delta.mean()),
        'reference':'independent intended-neighborhood CPU implementation'
    }
    (outdir/'META.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(meta,indent=2))


def error_stats(actual,expected,tol):
    diff=np.abs(actual-expected)
    flat=int(np.argmax(diff)); y,x,c=np.unravel_index(flat,diff.shape)
    return {
        'max_abs':float(diff.max()),
        'mean_abs':float(diff.mean()),
        'per_channel_max_abs':[float(diff[:,:,i].max()) for i in range(4)],
        'pixels_over_tolerance':int(np.count_nonzero(np.any(diff>F(tol),axis=2))),
        'worst_pixel':{'x':int(x),'y':int(y),'channel':int(c),'expected':float(expected[y,x,c]),'actual':float(actual[y,x,c])}
    }


def validate(outdir,gpu_path,tol):
    n=OUT_W*OUT_H*4
    gpu=np.fromfile(gpu_path,dtype=np.float32)
    ideal=np.fromfile(outdir/'expected_ideal.f32',dtype=np.float32)
    d3d=np.fromfile(outdir/'expected_d3d11.f32',dtype=np.float32)
    if gpu.size!=n or ideal.size!=n or d3d.size!=n:
        raise SystemExit(f'size mismatch gpu={gpu.size} ideal={ideal.size} d3d11={d3d.size} required={n}')
    gpu=gpu.reshape(OUT_H,OUT_W,4); ideal=ideal.reshape(OUT_H,OUT_W,4); d3d=d3d.reshape(OUT_H,OUT_W,4)
    if not np.isfinite(gpu).all(): raise SystemExit('GPU output contains NaN/Inf')
    d3d_stats=error_stats(gpu,d3d,tol)
    ideal_stats=error_stats(gpu,ideal,tol)
    summary={
        'protocol':'LAB07_ALWAYS_ON_MC_D3D11_WARP_RUNTIME_PARITY',
        'driver_requested':'D3D_DRIVER_TYPE_WARP',
        'physical_gpu_executed':False,
        'd3d11_subtexel_fractional_bits':D3D11_SUBTEXEL_BITS,
        'tolerance_abs':float(tol),
        'vs_d3d11_filter_reference':d3d_stats,
        'vs_ideal_float_reference':ideal_stats,
        'pass':bool(d3d_stats['max_abs']<=tol)
    }
    (outdir/'SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(summary,indent=2))
    if not summary['pass']: raise SystemExit('WARP PARITY FAIL')


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--outdir',required=True)
    ap.add_argument('--prepare',action='store_true')
    ap.add_argument('--gpu')
    ap.add_argument('--tol',type=float,default=DEFAULT_TOL)
    a=ap.parse_args(); out=Path(a.outdir)
    if a.prepare: prepare(out)
    if a.gpu: validate(out,Path(a.gpu),a.tol)
    if not a.prepare and not a.gpu: ap.error('specify --prepare and/or --gpu')

if __name__=='__main__': main()

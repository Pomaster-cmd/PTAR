#!/usr/bin/env python3
"""LAB18 frozen admission D3D11 WARP runtime validator.

Reuses only the established LAB07 test texture and D3D11 linear-sampling model;
the v01 base, MC expert, admission signals and final mix are implemented here
for the frozen LAB18 shader. Physical GPU execution is explicitly false.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

import lab_moe_ng_v02_warp_validate as w7

F=np.float32
LUMA=np.asarray([0.2126,0.7152,0.0722],dtype=np.float32)
DEFAULT_TOL=5.0e-5


def sat(x):
    return np.clip(np.asarray(x,dtype=np.float32),F(0),F(1)).astype(np.float32)


def k13(fm1,f0,f1,f2):
    return (F(-0.274074074074074)*fm1+F(0.877777777777778)*f0+F(0.533333333333333)*f1+F(-0.137037037037037)*f2).astype(np.float32)


def k23(fm1,f0,f1,f2):
    return (F(-0.137037037037037)*fm1+F(0.533333333333333)*f0+F(0.877777777777778)*f1+F(-0.274074074074074)*f2).astype(np.float32)


def cpu_reference(img,d3d11_filter):
    h,w,_=img.shape
    out=np.empty((w7.OUT_H,w7.OUT_W,4),dtype=np.float32)
    green=img[:,:,1]
    for oy in range(w7.OUT_H):
        for ox in range(w7.OUT_W):
            sx=F(ox)*F(2/3); sy=F(oy)*F(2/3)
            fx=int(np.floor(sx)); fy=int(np.floor(sy))
            x0=min(max(fx,0),w-1); x1=min(max(fx+1,0),w-1)
            y0=min(max(fy,0),h-1); y1=min(max(fy+1,0),h-1)
            tl=F(green[y0,x0]); tr=F(green[y0,x1]); bl=F(green[y1,x0]); br=F(green[y1,x1])
            gx=F((tr+br)-(tl+bl)); gy=F((bl+br)-(tl+tr)); agx=F(abs(gx)); agy=F(abs(gy))
            use_x=bool(agx>=agy)
            if use_x:
                bx=F(fx); by=sy; ax=F(1); ay=F(0); phase=ox%3
            else:
                bx=sx; by=F(fy); ax=F(0); ay=F(1); phase=oy%3
            fm1=w7.sample_linear(img,bx-ax,by-ay,d3d11_filter)
            f0=w7.sample_linear(img,bx,by,d3d11_filter)
            f1=w7.sample_linear(img,bx+ax,by+ay,d3d11_filter)
            f2=w7.sample_linear(img,bx+F(2)*ax,by+F(2)*ay,d3d11_filter)
            if phase==0:
                out[oy,ox]=f0
                continue

            edge=k23(fm1,f0,f1,f2) if phase==1 else k13(fm1,f0,f1,f2)
            raster=np.minimum(np.maximum(edge,np.minimum(f0,f1)),np.maximum(f0,f1)).astype(np.float32)
            t=F(2/3) if phase==1 else F(1/3)
            bilinear=(f0*(F(1)-t)+f1*t).astype(np.float32)
            natural=(bilinear*F(.30)+raster*F(.70)).astype(np.float32)

            local_range=F(max(tl,tr,bl,br)-min(tl,tr,bl,br))
            gradient=F(.5)*F(max(agx,agy))
            diagonal=F(.5)*F(abs(F((tl+br)-(tr+bl))))
            coherence=F(abs(F(agx-agy))/F(agx+agy+F(1e-6)))
            range_conf=F(np.clip(F((local_range-F(.01))/F(.10)),F(0),F(1)))
            coherence_conf=F(np.clip(F((coherence-F(.25))/F(.45)),F(0),F(1)))
            diag_ratio=F(diagonal/F(gradient+F(1e-6)))
            axis_conf=F(1)-F(np.clip(F((diag_ratio-F(.08))/F(.45)),F(0),F(1)))
            raster_weight=F(range_conf*coherence_conf*axis_conf)
            edge_base=F(np.clip(F((gradient-F(.02))/F(.10)),F(0),F(1)))
            edge_weight=F((F(1)-raster_weight)*edge_base)
            natural_weight=F(F(1)-raster_weight-edge_weight)
            v01=sat(natural*natural_weight+edge*edge_weight+raster*raster_weight)

            d0=(f0-fm1).astype(np.float32); d1=(f1-f0).astype(np.float32); d2=(f2-f1).astype(np.float32)
            m0=w7.mc_slope(d0,d1); m1=w7.mc_slope(d1,d2)
            mc=w7.h23(f0,f1,m0,m1) if phase==1 else w7.h13(f0,f1,m0,m1)

            ld0=F(np.sum(d0[:3]*LUMA,dtype=np.float32)); ld1=F(np.sum(d1[:3]*LUMA,dtype=np.float32)); ld2=F(np.sum(d2[:3]*LUMA,dtype=np.float32))
            ad0=F(abs(ld0)); ad1=F(abs(ld1)); ad2=F(abs(ld2))
            central=F(ad1/F(ad0+ad1+ad2+F(1e-6)))
            outer_balance=F(min(ad0,ad2)/F(max(ad0,ad2)+F(1e-6)))
            low_coh=F(1)-F(np.clip(F((coherence-F(.75))/F(.18)),F(0),F(1)))
            balanced=F(np.clip(F((outer_balance-F(.10))/F(.15)),F(0),F(1)))
            central_support=F(np.clip(F((central-F(.10))/F(.15)),F(0),F(1)))
            admission=F(F(.50)*low_coh*balanced*central_support)
            out[oy,ox]=sat(v01+(mc-v01)*admission)
    return out


def prepare(outdir):
    outdir.mkdir(parents=True,exist_ok=True)
    img=w7.generate_input(); ideal=cpu_reference(img,False); d3d=cpu_reference(img,True)
    img.tofile(outdir/'input.f32'); ideal.tofile(outdir/'expected_ideal.f32'); d3d.tofile(outdir/'expected_d3d11.f32')
    delta=np.abs(d3d-ideal)
    meta={'protocol':'LAB18_FROZEN_ADMISSION_WARP_PREP','input_width':w7.IN_W,'input_height':w7.IN_H,'output_width':w7.OUT_W,'output_height':w7.OUT_H,'format':'R32G32B32A32_FLOAT','d3d11_subtexel_fractional_bits':w7.D3D11_SUBTEXEL_BITS,'ideal_vs_d3d11_filter_max_abs':float(delta.max()),'ideal_vs_d3d11_filter_mean_abs':float(delta.mean()),'candidate':'LAB18 frozen lc_bal_central strength 0.50'}
    (outdir/'META.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8'); print(json.dumps(meta,indent=2))


def validate(outdir,gpu_path,tol):
    n=w7.OUT_W*w7.OUT_H*4
    gpu=np.fromfile(gpu_path,dtype=np.float32); ideal=np.fromfile(outdir/'expected_ideal.f32',dtype=np.float32); d3d=np.fromfile(outdir/'expected_d3d11.f32',dtype=np.float32)
    if gpu.size!=n or ideal.size!=n or d3d.size!=n: raise SystemExit(f'size mismatch gpu={gpu.size} ideal={ideal.size} d3d={d3d.size} required={n}')
    gpu=gpu.reshape(w7.OUT_H,w7.OUT_W,4); ideal=ideal.reshape(w7.OUT_H,w7.OUT_W,4); d3d=d3d.reshape(w7.OUT_H,w7.OUT_W,4)
    if not np.isfinite(gpu).all(): raise SystemExit('GPU output contains NaN/Inf')
    ds=w7.error_stats(gpu,d3d,tol); ins=w7.error_stats(gpu,ideal,tol)
    summary={'protocol':'LAB18_FROZEN_ADMISSION_D3D11_WARP_RUNTIME_PARITY','driver_requested':'D3D_DRIVER_TYPE_WARP','physical_gpu_executed':False,'tolerance_abs':float(tol),'vs_d3d11_filter_reference':ds,'vs_ideal_float_reference':ins,'pass':bool(ds['max_abs']<=tol)}
    (outdir/'SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8'); print(json.dumps(summary,indent=2))
    if not summary['pass']: raise SystemExit('LAB18 WARP PARITY FAIL')


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--outdir',required=True); ap.add_argument('--prepare',action='store_true'); ap.add_argument('--gpu'); ap.add_argument('--tol',type=float,default=DEFAULT_TOL); a=ap.parse_args(); out=Path(a.outdir)
    if a.prepare: prepare(out)
    if a.gpu: validate(out,Path(a.gpu),a.tol)
    if not a.prepare and not a.gpu: ap.error('specify --prepare and/or --gpu')

if __name__=='__main__': main()

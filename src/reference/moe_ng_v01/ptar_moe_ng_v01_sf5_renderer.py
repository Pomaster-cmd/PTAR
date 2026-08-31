#!/usr/bin/env python3
"""PTAR-NG MoE v01 SF5 reference.

NEW NG branch. It is NOT a reconstruction of historical MoE v07,
NATURAL v02 or RASTER v03.

Shared-fetch architecture:
    1 GatherGreen-equivalent 2x2 routing footprint
    4 directional linear color samples

Experts derived from the SAME samples:
- EDGE-NG v03 K185: validated raw K185 reconstruction.
- RASTER-NG v01 MCLAMP: component-wise monotone clamp of K185 to f0/f1.
- NATURAL-NG v01 N70: 70% MCLAMP + 30% bilinear.
- ROUTER-NG v01 R1: range/gradient/diagonal/coherence heuristic.

No NIS runtime path.
"""
import numpy as np

W13=np.array([
    -0.274074074074074,
     0.877777777777778,
     0.533333333333333,
    -0.137037037037037,
],dtype=np.float64)
W23=W13[::-1].copy()

NATURAL_MONOTONE_WEIGHT=0.70
ROUTER_EDGE_LOW=0.02
ROUTER_EDGE_WIDTH=0.10
ROUTER_RASTER_RANGE_LOW=0.01
ROUTER_RASTER_RANGE_WIDTH=0.10
ROUTER_RASTER_COHERENCE_LOW=0.25
ROUTER_RASTER_COHERENCE_WIDTH=0.45
ROUTER_RASTER_DIAG_RATIO_LOW=0.08
ROUTER_RASTER_DIAG_RATIO_WIDTH=0.45
EPS=1.0e-6

def _g(src,yy,xx):
    h,w,_=src.shape
    return src[np.clip(yy,0,h-1),np.clip(xx,0,w-1)]

def _render(lr_rgb,dtype,return_meta):
    src=np.asarray(lr_rgb)
    if src.dtype==np.uint8:
        src=src.astype(dtype)/dtype(255.0)
    else:
        src=src.astype(dtype)
        if src.max()>dtype(1.5):
            src=src/dtype(255.0)
    src=np.clip(src,dtype(0),dtype(1))

    h,w,_=src.shape
    oh=int(round(h*1.5)); ow=int(round(w*1.5))
    ox=np.arange(ow,dtype=np.uint32)
    oy=np.arange(oh,dtype=np.uint32)
    sx=ox.astype(dtype)*dtype(2.0/3.0)
    sy=oy.astype(dtype)*dtype(2.0/3.0)
    X,Y=np.meshgrid(sx,sy)
    ix=np.floor(X).astype(np.int64)
    iy=np.floor(Y).astype(np.int64)
    tx=(X-ix.astype(dtype)).astype(dtype)
    ty=(Y-iy.astype(dtype)).astype(dtype)
    x1=ix+1; y1=iy+1

    tl4=_g(src,iy,ix); tr4=_g(src,iy,x1)
    bl4=_g(src,y1,ix); br4=_g(src,y1,x1)
    tl=tl4[...,1]; tr=tr4[...,1]; bl=bl4[...,1]; br=br4[...,1]

    gx=(tr+br)-(tl+bl)
    gy=(bl+br)-(tl+tr)
    agx=np.abs(gx); agy=np.abs(gy)
    use_x=agx>=agy

    def vlin(xidx):
        a=_g(src,iy,xidx); b=_g(src,y1,xidx)
        return a+ty[...,None]*(b-a)
    def hlin(yidx):
        a=_g(src,yidx,ix); b=_g(src,yidx,x1)
        return a+tx[...,None]*(b-a)

    xs=[vlin(ix+k) for k in (-1,0,1,2)]
    ys=[hlin(iy+k) for k in (-1,0,1,2)]
    xm=np.broadcast_to((ox%3)[None,:],(oh,ow))
    ym=np.broadcast_to((oy%3)[:,None],(oh,ow))

    w13=W13.astype(dtype)
    w23=W23.astype(dtype)

    def reconstruct(samples,mod):
        r13=np.zeros_like(samples[0])
        r23=np.zeros_like(samples[0])
        for ww,s in zip(w13,samples): r13=r13+ww*s
        for ww,s in zip(w23,samples): r23=r23+ww*s
        raw=np.where((mod==0)[...,None],samples[1],
            np.where((mod==2)[...,None],r13,r23))

        lo=np.minimum(samples[1],samples[2])
        hi=np.maximum(samples[1],samples[2])
        monotone=np.minimum(np.maximum(raw,lo),hi)
        monotone=np.where((mod==0)[...,None],samples[1],monotone)

        frac=np.where(mod==0,dtype(0),
             np.where(mod==1,dtype(2.0/3.0),dtype(1.0/3.0))).astype(dtype)
        bilinear=samples[1]*(dtype(1)-frac[...,None])+samples[2]*frac[...,None]
        return raw,monotone,bilinear

    rx,mx,bx=reconstruct(xs,xm)
    ry,my,by=reconstruct(ys,ym)
    edge=np.where(use_x[...,None],rx,ry)
    raster=np.where(use_x[...,None],mx,my)
    bilinear=np.where(use_x[...,None],bx,by)
    natural=bilinear*dtype(1.0-NATURAL_MONOTONE_WEIGHT)+raster*dtype(NATURAL_MONOTONE_WEIGHT)

    local_range=np.maximum.reduce([tl,tr,bl,br])-np.minimum.reduce([tl,tr,bl,br])
    gradient=np.maximum(agx,agy)*dtype(0.5)
    diagonal=np.abs((tl+br)-(tr+bl))*dtype(0.5)
    coherence=np.abs(agx-agy)/(agx+agy+dtype(EPS))

    def sat(x):
        return np.clip(x,dtype(0),dtype(1))

    range_conf=sat((local_range-dtype(ROUTER_RASTER_RANGE_LOW))/dtype(ROUTER_RASTER_RANGE_WIDTH))
    coherence_conf=sat((coherence-dtype(ROUTER_RASTER_COHERENCE_LOW))/dtype(ROUTER_RASTER_COHERENCE_WIDTH))
    diag_ratio=diagonal/(gradient+dtype(EPS))
    axis_conf=dtype(1)-sat((diag_ratio-dtype(ROUTER_RASTER_DIAG_RATIO_LOW))/dtype(ROUTER_RASTER_DIAG_RATIO_WIDTH))
    raster_weight=range_conf*coherence_conf*axis_conf
    edge_base=sat((gradient-dtype(ROUTER_EDGE_LOW))/dtype(ROUTER_EDGE_WIDTH))
    edge_weight=(dtype(1)-raster_weight)*edge_base
    natural_weight=dtype(1)-raster_weight-edge_weight

    out=natural*natural_weight[...,None]+edge*edge_weight[...,None]+raster*raster_weight[...,None]
    out=np.clip(out,dtype(0),dtype(1))

    if not return_meta:
        return out
    return out,{
        "algorithm":"PTAR-NG-MoE-v01-SF5",
        "historical_equivalence":False,
        "shared_texture_path":"1 GatherGreen + 4 SampleLevel",
        "natural_expert":"NATURAL-NG-v01-N70",
        "edge_expert":"EDGE-NG-v03-K185",
        "raster_expert":"RASTER-NG-v01-MCLAMP",
        "router":"ROUTER-NG-v01-R1",
        "mean_natural_weight":float(np.mean(natural_weight)),
        "mean_edge_weight":float(np.mean(edge_weight)),
        "mean_raster_weight":float(np.mean(raster_weight)),
    }

def render_moe_ng_v01_sf5(lr_rgb,return_meta=False):
    return _render(lr_rgb,np.float64,return_meta)

def render_shader_semantic(lr_rgb,return_meta=False):
    return _render(lr_rgb,np.float32,return_meta)

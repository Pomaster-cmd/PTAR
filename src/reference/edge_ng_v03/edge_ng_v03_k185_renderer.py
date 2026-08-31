#!/usr/bin/env python3
"""PTAR EDGE-NG v03 K185 reference renderer.

New NG code. NOT reconstructed historical EDGE v03.

Runtime budget target:
    1 GatherGreen + 4 SampleLevel

Orientation:
    same G5 2x2 green-channel gather orientation used by EDGE-NG v02.

Reconstruction:
    Keys-style cubic convolution with fixed a=-1.85.
    For the fixed x1.5 phases, weights are precomputed.

Phase 1/3:
    [-0.274074074074074, 0.877777777777778,
      0.533333333333333,-0.137037037037037]

Phase 2/3:
    mirrored.

Selection rule:
    K185 is within 0.0001 SSIM of the best fine-sweep candidate on EDGE_CORE
    and minimizes out-of-range component incidence among that near-best set.

The renderer clips to [0,1] for the RGB8 benchmark corpus.
Runtime floating-point targets may choose host-format semantics separately.
"""
import numpy as np

A = -1.85
W13 = np.array([
    -0.274074074074074,
     0.877777777777778,
     0.533333333333333,
    -0.137037037037037,
], dtype=np.float64)
W23 = W13[::-1].copy()

def _gather(src, yy, xx):
    h,w,_ = src.shape
    yy = np.clip(yy, 0, h-1)
    xx = np.clip(xx, 0, w-1)
    return src[yy,xx]

def _vlinear(src, xidx, y0, y1, ty):
    a=_gather(src,y0,xidx); b=_gather(src,y1,xidx)
    return a + ty[...,None]*(b-a)

def _hlinear(src, yidx, x0, x1, tx):
    a=_gather(src,yidx,x0); b=_gather(src,yidx,x1)
    return a + tx[...,None]*(b-a)

def _reconstruct(samples, mod):
    r13=sum(w*s for w,s in zip(W13,samples))
    r23=sum(w*s for w,s in zip(W23,samples))
    return np.where(
        (mod==0)[...,None], samples[1],
        np.where((mod==2)[...,None], r13, r23)
    )

def render_edge_ng_v03_k185(lr_rgb, clip_output=True, return_meta=False):
    src=np.asarray(lr_rgb,dtype=np.float64)
    if src.max()>1.5:
        src=src/255.0
    src=np.clip(src,0.0,1.0)

    h,w,_=src.shape
    oh=int(round(h*1.5)); ow=int(round(w*1.5))
    ox=np.arange(ow,dtype=np.int64)
    oy=np.arange(oh,dtype=np.int64)
    X,Y=np.meshgrid(ox/1.5,oy/1.5)
    ix=np.floor(X).astype(np.int64)
    iy=np.floor(Y).astype(np.int64)
    tx=X-ix; ty=Y-iy
    x1=ix+1; y1=iy+1

    # G5 orientation footprint: one 2x2 green-channel gather.
    tl=_gather(src,iy,ix)[...,1]
    tr=_gather(src,iy,x1)[...,1]
    bl=_gather(src,y1,ix)[...,1]
    br=_gather(src,y1,x1)[...,1]
    gx=(tr+br)-(tl+bl)
    gy=(bl+br)-(tl+tr)
    use_x=np.abs(gx)>=np.abs(gy)

    xs=[_vlinear(src,ix+k,iy,y1,ty) for k in (-1,0,1,2)]
    ys=[_hlinear(src,iy+k,ix,x1,tx) for k in (-1,0,1,2)]

    xm=np.broadcast_to((ox%3)[None,:],(oh,ow))
    ym=np.broadcast_to((oy%3)[:,None],(oh,ow))
    bx=_reconstruct(xs,xm)
    by=_reconstruct(ys,ym)
    raw=np.where(use_x[...,None],bx,by)

    out=np.clip(raw,0.0,1.0) if clip_output else raw
    if not return_meta:
        return out
    meta={
        "algorithm":"EDGE-NG-v03-K185",
        "a":A,
        "logical_texture_path":"1 GatherGreen + 4 SampleLevel",
        "x_direction_fraction":float(np.mean(use_x)),
        "component_out_of_range_fraction":
            float(np.mean((raw < -1e-7) | (raw > 1.0+1e-7))),
        "raw_min":float(raw.min()),
        "raw_max":float(raw.max()),
        "mapping":"grid-aligned x1.5",
    }
    return out,meta

#!/usr/bin/env python3
"""Float32 semantic mirror of the EDGE-NG v03 K185 HLSL path.

This is not a GPU replacement. It mirrors:
- UNORM-like source values represented as float32,
- G5 gather footprint semantics,
- linear orthogonal sampling,
- fixed K185 phase coefficients,
- float32 arithmetic.

Expected GPU parity gate after real D3D11 rendering:
- no error > 1 RGB8 LSB;
- mismatch population must be documented per driver/GPU.
"""
import numpy as np

W13=np.array([
    -0.274074074074074,
     0.877777777777778,
     0.533333333333333,
    -0.137037037037037
],dtype=np.float32)
W23=W13[::-1].copy()

def _g(src,yy,xx):
    h,w,_=src.shape
    return src[np.clip(yy,0,h-1),np.clip(xx,0,w-1)]

def render_shader_semantic(lr_rgb):
    src=np.asarray(lr_rgb)
    if src.dtype==np.uint8:
        src=src.astype(np.float32)/np.float32(255.0)
    else:
        src=src.astype(np.float32)
        if src.max()>np.float32(1.5):
            src=src/np.float32(255.0)
    src=np.clip(src,np.float32(0),np.float32(1))

    h,w,_=src.shape
    oh=int(round(h*1.5)); ow=int(round(w*1.5))
    ox=np.arange(ow,dtype=np.uint32)
    oy=np.arange(oh,dtype=np.uint32)
    sx=ox.astype(np.float32)*np.float32(2.0/3.0)
    sy=oy.astype(np.float32)*np.float32(2.0/3.0)
    X,Y=np.meshgrid(sx,sy)
    ix=np.floor(X).astype(np.int64)
    iy=np.floor(Y).astype(np.int64)
    tx=(X-ix.astype(np.float32)).astype(np.float32)
    ty=(Y-iy.astype(np.float32)).astype(np.float32)
    x1=ix+1; y1=iy+1

    tl=_g(src,iy,ix)[...,1]
    tr=_g(src,iy,x1)[...,1]
    bl=_g(src,y1,ix)[...,1]
    br=_g(src,y1,x1)[...,1]
    gx=(tr+br)-(tl+bl)
    gy=(bl+br)-(tl+tr)
    use_x=np.abs(gx)>=np.abs(gy)

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

    def rec(sm,mod):
        r13=np.zeros_like(sm[0])
        r23=np.zeros_like(sm[0])
        for w,s in zip(W13,sm):
            r13=r13+w*s
        for w,s in zip(W23,sm):
            r23=r23+w*s
        return np.where((mod==0)[...,None],sm[1],
               np.where((mod==2)[...,None],r13,r23))

    out=np.where(use_x[...,None],rec(xs,xm),rec(ys,ym))
    return np.clip(out,np.float32(0),np.float32(1))

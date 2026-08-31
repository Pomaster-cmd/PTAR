#!/usr/bin/env python3
"""EDGE-NG v02 reference renderer.

Modes:
- V01_FLOOR: v01 hard axis with floor-grid gradient (regression reference)
- G5: runtime-oriented one-gather direction model + four-sample cubic reconstruction
- HQ05: diagnostic quality reference using four bilinear direction probes at +/-0.5 texel

G5 simulates the orientation information available from one 2x2 green-channel Gather.
It does not claim historical EDGE v03 equivalence.
"""
import sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'reference/edge_ng_v01'))
import edge_ng_v01_renderer as v1


def _cubic(fm1,f0,f1,f2,mod):
    ph=(mod==2); integer=(mod==0)
    ql,qr=v1._candidates(fm1,f0,f1,f2,ph)
    b=v1._ideal_cubic(ql,qr,ph)
    return np.where(integer[...,None],f0,b)

def _bilinear(src,X,Y):
    h,w,_=src.shape
    x0=np.floor(X).astype(np.int64);y0=np.floor(Y).astype(np.int64);tx=X-x0;ty=Y-y0
    a=v1._gather(src,y0,x0);b=v1._gather(src,y0,x0+1);c=v1._gather(src,y0+1,x0);d=v1._gather(src,y0+1,x0+1)
    return a*(1-tx)[...,None]*(1-ty)[...,None]+b*tx[...,None]*(1-ty)[...,None]+c*(1-tx)[...,None]*ty[...,None]+d*tx[...,None]*ty[...,None]

def render_edge_ng_v02(lr_rgb,mode='G5'):
    if mode not in {'V01_FLOOR','G5','HQ05'}:raise ValueError('mode')
    src=np.asarray(lr_rgb,dtype=np.float64)
    if src.max()>1.5:src=src/255.0
    src=np.clip(src,0,1)
    h,w,_=src.shape;oh=int(round(h*1.5));ow=int(round(w*1.5))
    ox=np.arange(ow,dtype=np.int64);oy=np.arange(oh,dtype=np.int64);X,Y=np.meshgrid(ox/1.5,oy/1.5)
    ix=np.floor(X).astype(np.int64);iy=np.floor(Y).astype(np.int64);tx=X-ix;ty=Y-iy;x1=ix+1;y1=iy+1

    if mode=='V01_FLOOR':
        lum=v1._luma(src);l=v1._gather(lum[...,None],iy,ix-1)[...,0];r=v1._gather(lum[...,None],iy,ix+1)[...,0];u=v1._gather(lum[...,None],iy-1,ix)[...,0];d=v1._gather(lum[...,None],iy+1,ix)[...,0]
        gx=r-l;gy=d-u
    elif mode=='G5':
        # CPU semantic equivalent of a 2x2 GatherGreen around floor(srcPos)/floor+1.
        tl=v1._gather(src,iy,ix)[...,1];tr=v1._gather(src,iy,x1)[...,1];bl=v1._gather(src,y1,ix)[...,1];br=v1._gather(src,y1,x1)[...,1]
        gx=(tr+br)-(tl+bl);gy=(bl+br)-(tl+tr)
    else: # HQ05 diagnostic
        l=v1._luma(_bilinear(src,X-0.5,Y));r=v1._luma(_bilinear(src,X+0.5,Y));u=v1._luma(_bilinear(src,X,Y-0.5));d=v1._luma(_bilinear(src,X,Y+0.5))
        gx=r-l;gy=d-u
    usex=np.abs(gx)>=np.abs(gy)

    # CPU computes both branches vectorially; the G5 HLSL runtime computes only the selected branch.
    xfm1=v1._vertical_linear_at_x(src,ix-1,iy,y1,ty);xf0=v1._vertical_linear_at_x(src,ix,iy,y1,ty);xf1=v1._vertical_linear_at_x(src,ix+1,iy,y1,ty);xf2=v1._vertical_linear_at_x(src,ix+2,iy,y1,ty)
    yfm1=v1._horizontal_linear_at_y(src,iy-1,ix,x1,tx);yf0=v1._horizontal_linear_at_y(src,iy,ix,x1,tx);yf1=v1._horizontal_linear_at_y(src,iy+1,ix,x1,tx);yf2=v1._horizontal_linear_at_y(src,iy+2,ix,x1,tx)
    xm=np.broadcast_to((ox%3)[None,:],(oh,ow));ym=np.broadcast_to((oy%3)[:,None],(oh,ow))
    bx=_cubic(xfm1,xf0,xf1,xf2,xm);by=_cubic(yfm1,yf0,yf1,yf2,ym)
    out=np.where(usex[...,None],bx,by)
    return np.clip(out,0,1),{'mode':mode,'x_direction_fraction':float(np.mean(usex)),'mapping':'grid-aligned x1.5'}

#!/usr/bin/env python3
"""PTAR EDGE-NG v01 reference renderer.

Recovery-neutral successor branch.
This is NOT reconstructed EDGE v03 code.

Geometry:
- fixed 1.5x grid-aligned mapping: source_coord = output_index / 1.5
- fractional phases are therefore exactly 1/3 and 2/3, plus integer samples

Direction selection:
- luminance in sRGB/code-value domain using Rec.709 coefficients
- axial central differences around floor(source coordinate)
- reconstruct along the stronger gradient axis (normal to the local edge)
- orthogonal fractional coordinate uses linear interpolation

Modes:
- BASELINE: ideal cubic combination of the same two quadratic candidates
- SW1: p=1 STEP-WENO, no clamp
- SW2: p=2 STEP-WENO, central monotone clamp
- SW3: baseline -> SW2 blend using adaptive EDGE-NG confidence

EDGE confidence is new EDGE-NG logic, not a recovered historical EDGE v03 formula:
    confidence = major_gradient / (local_5tap_range + 1e-8), saturated [0,1]
"""
from pathlib import Path
import argparse, csv, json, math
import numpy as np
from PIL import Image

INV9 = 1.0 / 9.0
BCURV = 13.0 / 12.0
C13L, C13R = 5.0/9.0, 4.0/9.0
C23L, C23R = 4.0/9.0, 5.0/9.0
LUMA = np.array([0.2126,0.7152,0.0722], dtype=np.float64)

MODE_BASELINE='BASELINE'
MODE_SW1='SW1'
MODE_SW2='SW2'
MODE_SW3='SW3'


def _gather(img, yy, xx):
    h,w,_=img.shape
    yy=np.clip(yy,0,h-1)
    xx=np.clip(xx,0,w-1)
    return img[yy,xx]


def _vertical_linear_at_x(img, xidx, y0, y1, ty):
    a=_gather(img,y0,xidx)
    b=_gather(img,y1,xidx)
    return a + ty[...,None]*(b-a)


def _horizontal_linear_at_y(img, yidx, x0, x1, tx):
    a=_gather(img,yidx,x0)
    b=_gather(img,yidx,x1)
    return a + tx[...,None]*(b-a)


def _luma(rgb):
    return np.tensordot(rgb, LUMA, axes=([-1],[0]))


def _candidates(fm1,f0,f1,f2,phase13):
    # phase13 bool: True -> 1/3, False -> 2/3. Values where phase==integer are handled outside.
    ql13=(-fm1 + 8.0*f0 + 2.0*f1)*INV9
    qr13=(5.0*f0 + 5.0*f1 - f2)*INV9
    ql23=(-fm1 + 5.0*f0 + 5.0*f1)*INV9
    qr23=(2.0*f0 + 8.0*f1 - f2)*INV9
    ql=np.where(phase13[...,None], ql13, ql23)
    qr=np.where(phase13[...,None], qr13, qr23)
    return ql,qr


def _weights(lm1,l0,l1,l2,phase13,power,epsilon):
    dL=l0-lm1; dC=l1-l0; dR=l2-l1
    bL=dC*dC + BCURV*(dC-dL)*(dC-dL)
    bR=dC*dC + BCURV*(dR-dC)*(dR-dC)
    cL=np.where(phase13,C13L,C23L)
    cR=np.where(phase13,C13R,C23R)
    sL=epsilon+bL; sR=epsilon+bR
    if power==1:
        aL=cL*sR; aR=cR*sL
    elif power==2:
        aL=cL*sR*sR; aR=cR*sL*sL
    else:
        raise ValueError('power must be 1 or 2')
    return aL/(aL+aR)


def _ideal_cubic(ql,qr,phase13):
    cL=np.where(phase13,C13L,C23L)
    return qr + cL[...,None]*(ql-qr)


def render_edge_ng_v01(lr_rgb, mode=MODE_SW3, epsilon=1e-3):
    if mode not in {MODE_BASELINE,MODE_SW1,MODE_SW2,MODE_SW3}:
        raise ValueError('bad mode')
    if epsilon <= 0: raise ValueError('epsilon must be >0')

    src=np.asarray(lr_rgb,dtype=np.float64)
    if src.max()>1.0: src=src/255.0
    h,w,_=src.shape
    oh=int(round(h*1.5)); ow=int(round(w*1.5))

    ox=np.arange(ow,dtype=np.int64); oy=np.arange(oh,dtype=np.int64)
    sx=ox/1.5; sy=oy/1.5
    X,Y=np.meshgrid(sx,sy)
    ix=np.floor(X).astype(np.int64); iy=np.floor(Y).astype(np.int64)
    tx=X-ix; ty=Y-iy
    x1=ix+1; y1=iy+1

    # Direction/confidence from source-grid floor sample and axial neighbours.
    lum_src=_luma(src)
    c=_gather(lum_src[...,None],iy,ix)[...,0]
    l=_gather(lum_src[...,None],iy,ix-1)[...,0]
    r=_gather(lum_src[...,None],iy,ix+1)[...,0]
    u=_gather(lum_src[...,None],iy-1,ix)[...,0]
    d=_gather(lum_src[...,None],iy+1,ix)[...,0]
    gx=r-l; gy=d-u
    ax=np.abs(gx); ay=np.abs(gy)
    use_x=ax>=ay
    local_min=np.minimum.reduce([c,l,r,u,d])
    local_max=np.maximum.reduce([c,l,r,u,d])
    local_range=local_max-local_min
    major=np.maximum(ax,ay)
    confidence=np.clip(major/(local_range+1e-8),0.0,1.0)

    # X-oriented reconstruction; Y coordinate remains linearly sampled.
    xfm1=_vertical_linear_at_x(src,ix-1,iy,y1,ty)
    xf0 =_vertical_linear_at_x(src,ix,  iy,y1,ty)
    xf1 =_vertical_linear_at_x(src,ix+1,iy,y1,ty)
    xf2 =_vertical_linear_at_x(src,ix+2,iy,y1,ty)

    # Y-oriented reconstruction; X coordinate remains linearly sampled.
    yfm1=_horizontal_linear_at_y(src,iy-1,ix,x1,tx)
    yf0 =_horizontal_linear_at_y(src,iy,  ix,x1,tx)
    yf1 =_horizontal_linear_at_y(src,iy+1,ix,x1,tx)
    yf2 =_horizontal_linear_at_y(src,iy+2,ix,x1,tx)

    fm1=np.where(use_x[...,None],xfm1,yfm1)
    f0 =np.where(use_x[...,None],xf0, yf0)
    f1 =np.where(use_x[...,None],xf1, yf1)
    f2 =np.where(use_x[...,None],xf2, yf2)

    lm1=_luma(fm1); l0=_luma(f0); l1=_luma(f1); l2=_luma(f2)

    # Exact phase from 1.5x grid alignment.
    xmod=np.broadcast_to((ox%3)[None,:],(oh,ow))
    ymod=np.broadcast_to((oy%3)[:,None],(oh,ow))
    mod=np.where(use_x,xmod,ymod)
    is_integer=(mod==0)
    # output mod 2 -> source fractional 1/3; output mod 1 -> source fractional 2/3
    phase13=(mod==2)

    ql,qr=_candidates(fm1,f0,f1,f2,phase13)
    baseline=_ideal_cubic(ql,qr,phase13)
    baseline=np.where(is_integer[...,None],f0,baseline)

    if mode==MODE_BASELINE:
        out=baseline
    else:
        power=1 if mode==MODE_SW1 else 2
        wl=_weights(lm1,l0,l1,l2,phase13,power,epsilon)
        sw=qr + wl[...,None]*(ql-qr)
        sw=np.where(is_integer[...,None],f0,sw)
        if mode in {MODE_SW2,MODE_SW3}:
            sw=np.minimum(np.maximum(sw,np.minimum(f0,f1)),np.maximum(f0,f1))
        if mode==MODE_SW3:
            out=baseline + confidence[...,None]*(sw-baseline)
        else:
            out=sw

    return np.clip(out,0.0,1.0), {
        'mean_edge_confidence':float(np.mean(confidence)),
        'x_direction_fraction':float(np.mean(use_x)),
        'mode':mode,'epsilon':float(epsilon),
        'mapping':'grid_aligned_source_coord=output_index/1.5'
    }


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('input')
    ap.add_argument('output')
    ap.add_argument('--mode',choices=[MODE_BASELINE,MODE_SW1,MODE_SW2,MODE_SW3],default=MODE_SW3)
    ap.add_argument('--epsilon',type=float,default=1e-3)
    args=ap.parse_args()
    with Image.open(args.input) as im:
        arr=np.asarray(im.convert('RGB'),dtype=np.float64)/255.0
    out,meta=render_edge_ng_v01(arr,args.mode,args.epsilon)
    Image.fromarray(np.rint(out*255).astype(np.uint8),'RGB').save(args.output)
    print(json.dumps(meta,indent=2))

if __name__=='__main__': main()

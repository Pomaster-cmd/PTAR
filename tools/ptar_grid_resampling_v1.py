#!/usr/bin/env python3
import math
import numpy as np

SCALE=1.5
LANCZOS_A=3.0

def reflect_index(i,n):
    if n<=1:return 0
    period=2*n-2
    i=i%period
    return i if i<n else period-i

def lanczos_downsample_matrix(n_in,n_out,scale=SCALE,a=LANCZOS_A):
    W=np.zeros((n_out,n_in),dtype=np.float64)
    support=a*scale
    for j in range(n_out):
        u=scale*j
        lo=math.floor(u-support)+1
        hi=math.floor(u+support)
        for k in range(lo,hi+1):
            d=k-u
            if abs(d)>=support:continue
            weight=np.sinc(d/scale)*np.sinc(d/(scale*a))/scale
            W[j,reflect_index(k,n_in)]+=weight
        s=W[j].sum()
        if abs(s)<1e-15:raise RuntimeError('zero resampling row')
        W[j]/=s
    return W

def downsample_grid_lanczos3(hr,out_h=192,out_w=192):
    hr=np.asarray(hr,dtype=np.float64)
    h,w,_=hr.shape
    Wy=lanczos_downsample_matrix(h,out_h)
    Wx=lanczos_downsample_matrix(w,out_w)
    tmp=np.einsum('yxc,ix->yic',hr,Wx,optimize=True)
    out=np.einsum('jy,yic->jic',Wy,tmp,optimize=True)
    return np.clip(out,0.0,1.0)

def _grid_coords(out_h,out_w,scale=SCALE):
    ox=np.arange(out_w,dtype=np.int64);oy=np.arange(out_h,dtype=np.int64)
    sx=ox/scale;sy=oy/scale
    return np.meshgrid(sx,sy),ox,oy

def resize_nearest_grid(lr,out_h=288,out_w=288):
    lr=np.asarray(lr,dtype=np.float64);h,w,_=lr.shape
    (X,Y),_,_=_grid_coords(out_h,out_w)
    ix=np.clip(np.rint(X).astype(np.int64),0,w-1);iy=np.clip(np.rint(Y).astype(np.int64),0,h-1)
    return lr[iy,ix]

def resize_bilinear_grid(lr,out_h=288,out_w=288):
    lr=np.asarray(lr,dtype=np.float64);h,w,_=lr.shape
    (X,Y),_,_=_grid_coords(out_h,out_w)
    x0=np.floor(X).astype(np.int64);y0=np.floor(Y).astype(np.int64)
    tx=X-x0;ty=Y-y0
    x0=np.clip(x0,0,w-1);y0=np.clip(y0,0,h-1);x1=np.clip(x0+1,0,w-1);y1=np.clip(y0+1,0,h-1)
    a=lr[y0,x0];b=lr[y0,x1];c=lr[y1,x0];d=lr[y1,x1]
    return np.clip(a*(1-tx)[...,None]*(1-ty)[...,None]+b*tx[...,None]*(1-ty)[...,None]+c*(1-tx)[...,None]*ty[...,None]+d*tx[...,None]*ty[...,None],0,1)

def _cubic_axis_grid(a,out_n,axis):
    a=np.swapaxes(np.asarray(a,dtype=np.float64),axis,0);n=a.shape[0]
    out=np.empty((out_n,)+a.shape[1:],dtype=np.float64)
    for o in range(out_n):
        s=o/SCALE;i=int(math.floor(s));mod=o%3
        def g(k):return a[min(max(k,0),n-1)]
        f0=g(i)
        if mod==0:v=f0
        else:
            fm1,f1,f2=g(i-1),g(i+1),g(i+2)
            if mod==2:
                ql=(-fm1+8*f0+2*f1)/9.0;qr=(5*f0+5*f1-f2)/9.0;v=(5*ql+4*qr)/9.0
            else:
                ql=(-fm1+5*f0+5*f1)/9.0;qr=(2*f0+8*f1-f2)/9.0;v=(4*ql+5*qr)/9.0
        out[o]=v
    return np.swapaxes(out,0,axis)

def resize_cubic4_separable_grid(lr,out_h=288,out_w=288):
    return np.clip(_cubic_axis_grid(_cubic_axis_grid(lr,out_w,1),out_h,0),0,1)

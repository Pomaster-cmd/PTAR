#!/usr/bin/env python3
import argparse,csv,json,math
from pathlib import Path
import numpy as np
from PIL import Image
try:
    from skimage.metrics import structural_similarity
except Exception:
    structural_similarity=None

REL_LO=np.float32(0.275)
REL_HI=np.float32(0.500)
ABS_LO=np.float32(0.035)
ABS_HI=np.float32(0.120)
EPS=np.float32(1.0e-6)
LUMA=np.asarray([0.2126,0.7152,0.0722],dtype=np.float32)


def sat(x): return np.clip(x,np.float32(0.0),np.float32(1.0))

def psnr(a,b):
    d=np.asarray(a,dtype=np.float64)-np.asarray(b,dtype=np.float64)
    mse=float(np.mean(d*d))
    return float('inf') if mse==0 else 10.0*math.log10(1.0/mse)

def ssim(a,b):
    if structural_similarity is None:return float('nan')
    return float(structural_similarity(np.asarray(a,dtype=np.float64),np.asarray(b,dtype=np.float64),data_range=1.0,channel_axis=2,gaussian_weights=True,sigma=1.5,use_sample_covariance=False))

def load_rgb(p):
    with Image.open(p) as im:
        return np.asarray(im.convert('RGB'),dtype=np.float32)/np.float32(255.0)

def sample_linear(a,x,y):
    h,w,_=a.shape
    x0=np.floor(x).astype(np.int32); y0=np.floor(y).astype(np.int32)
    tx=(x-x0).astype(np.float32); ty=(y-y0).astype(np.float32)
    x0c=np.clip(x0,0,w-1); x1c=np.clip(x0+1,0,w-1)
    y0c=np.clip(y0,0,h-1); y1c=np.clip(y0+1,0,h-1)
    aa=a[y0c,x0c]; bb=a[y0c,x1c]; cc=a[y1c,x0c]; dd=a[y1c,x1c]
    tx=tx[...,None]; ty=ty[...,None]
    return ((aa*(1-tx)+bb*tx)*(1-ty)+(cc*(1-tx)+dd*tx)*ty).astype(np.float32)

def render(lr,mode='v02',out_h=288,out_w=288,return_gate=False):
    lr=np.asarray(lr,dtype=np.float32)
    oy,ox=np.mgrid[0:out_h,0:out_w]
    ox=ox.astype(np.uint32); oy=oy.astype(np.uint32)
    sx=ox.astype(np.float32)*(np.float32(2.0)/np.float32(3.0))
    sy=oy.astype(np.float32)*(np.float32(2.0)/np.float32(3.0))
    fx=np.floor(sx).astype(np.int32); fy=np.floor(sy).astype(np.int32)
    h,w,_=lr.shape
    x0=np.clip(fx,0,w-1); x1=np.clip(fx+1,0,w-1)
    y0=np.clip(fy,0,h-1); y1=np.clip(fy+1,0,h-1)
    # GatherGreen semantic contract used by validated v01 path.
    tl=lr[y0,x0,1]; tr=lr[y0,x1,1]; bl=lr[y1,x0,1]; br=lr[y1,x1,1]
    gx=(tr+br)-(tl+bl); gy=(bl+br)-(tl+tr)
    agx=np.abs(gx); agy=np.abs(gy); usex=agx>=agy
    bx=np.where(usex,fx.astype(np.float32),sx).astype(np.float32)
    by=np.where(usex,sy,fy.astype(np.float32)).astype(np.float32)
    ax=usex.astype(np.float32); ay=(~usex).astype(np.float32)
    fm1=sample_linear(lr,bx-ax,by-ay)
    f0 =sample_linear(lr,bx,by)
    f1 =sample_linear(lr,bx+ax,by+ay)
    f2 =sample_linear(lr,bx+2*ax,by+2*ay)
    phase=np.where(usex,ox%3,oy%3).astype(np.uint32)
    pf=np.where(phase==1,np.float32(2.0/3.0),np.float32(1.0/3.0)).astype(np.float32)
    bilinear=f0*(1-pf[...,None])+f1*pf[...,None]
    if mode=='bilinear':
        out=np.where((phase==0)[...,None],f0,bilinear)
        return (out,None) if return_gate else out
    k13=(-np.float32(0.274074074074074)*fm1+np.float32(0.877777777777778)*f0+np.float32(0.533333333333333)*f1-np.float32(0.137037037037037)*f2)
    k23=(-np.float32(0.137037037037037)*fm1+np.float32(0.533333333333333)*f0+np.float32(0.877777777777778)*f1-np.float32(0.274074074074074)*f2)
    edge=np.where((phase==2)[...,None],k13,k23).astype(np.float32)
    raster=np.minimum(np.maximum(edge,np.minimum(f0,f1)),np.maximum(f0,f1))
    natural=bilinear*np.float32(0.30)+raster*np.float32(0.70)
    local_range=np.maximum(np.maximum(tl,tr),np.maximum(bl,br))-np.minimum(np.minimum(tl,tr),np.minimum(bl,br))
    gradient=np.float32(0.5)*np.maximum(agx,agy)
    diagonal=np.float32(0.5)*np.abs((tl+br)-(tr+bl))
    coherence=np.abs(agx-agy)/(agx+agy+EPS)
    range_conf=sat((local_range-np.float32(0.01))/np.float32(0.10))
    coherence_conf=sat((coherence-np.float32(0.25))/np.float32(0.45))
    diag_ratio=diagonal/(gradient+EPS)
    axis_conf=np.float32(1.0)-sat((diag_ratio-np.float32(0.08))/np.float32(0.45))
    rw=range_conf*coherence_conf*axis_conf
    edge_base=sat((gradient-np.float32(0.02))/np.float32(0.10))
    ew=(np.float32(1.0)-rw)*edge_base
    nw=np.float32(1.0)-rw-ew
    moe=natural*nw[...,None]+edge*ew[...,None]+raster*rw[...,None]
    if mode=='v01':
        out=np.where((phase==0)[...,None],f0,moe)
        return (out,None) if return_gate else out
    # v02 LAB02-PARETO gate, exact float32 mirror of HLSL.
    ym1=np.sum(fm1*LUMA,axis=2,dtype=np.float32); yy0=np.sum(f0*LUMA,axis=2,dtype=np.float32)
    yy1=np.sum(f1*LUMA,axis=2,dtype=np.float32); yy2=np.sum(f2*LUMA,axis=2,dtype=np.float32)
    c0=np.abs(ym1-np.float32(2.0)*yy0+yy1); c1=np.abs(yy0-np.float32(2.0)*yy1+yy2)
    ac=np.maximum(c0,c1)
    slope=np.maximum(np.abs(yy0-ym1),np.maximum(np.abs(yy1-yy0),np.abs(yy2-yy1)))
    rc=ac/(slope+EPS)
    gr=sat((rc-REL_LO)/(REL_HI-REL_LO)); ga=sat((ac-ABS_LO)/(ABS_HI-ABS_LO))
    gate=np.sqrt(gr*ga).astype(np.float32)
    v02=bilinear*(np.float32(1.0)-gate[...,None])+moe*gate[...,None]
    out=np.where((phase==0)[...,None],f0,v02)
    return (out,gate) if return_gate else out

def cubic4(lr,out_h=288,out_w=288):
    # Exact separable cubic4 control from tools/ptar_grid_resampling_v1.py.
    def axis(a,out_n,axis):
        a=np.swapaxes(np.asarray(a,dtype=np.float64),axis,0); n=a.shape[0]
        out=np.empty((out_n,)+a.shape[1:],dtype=np.float64)
        for o in range(out_n):
            s=o/1.5; i=int(math.floor(s)); mod=o%3
            def g(k):return a[min(max(k,0),n-1)]
            f0=g(i)
            if mod==0:v=f0
            else:
                fm1,f1,f2=g(i-1),g(i+1),g(i+2)
                if mod==2:
                    ql=(-fm1+8*f0+2*f1)/9.0; qr=(5*f0+5*f1-f2)/9.0; v=(5*ql+4*qr)/9.0
                else:
                    ql=(-fm1+5*f0+5*f1)/9.0; qr=(2*f0+8*f1-f2)/9.0; v=(4*ql+5*qr)/9.0
            out[o]=v
        return np.swapaxes(out,0,axis)
    return np.clip(axis(axis(lr,out_w,1),out_h,0),0,1).astype(np.float32)

def analytic_sine(freq,amp=0.0125):
    xh=np.arange(288,dtype=np.float32); xl=np.arange(192,dtype=np.float32)*np.float32(1.5)
    hr=(np.float32(0.5)+np.float32(amp)*np.sin(np.float32(2*math.pi*freq)*xh)).clip(0,1)
    lr=(np.float32(0.5)+np.float32(amp)*np.sin(np.float32(2*math.pi*freq)*xl)).clip(0,1)
    return np.repeat(np.repeat(hr[None,:,None],288,axis=0),3,axis=2), np.repeat(np.repeat(lr[None,:,None],192,axis=0),3,axis=2)

def analytic_soft_edge(width=2.0):
    xh=np.arange(288,dtype=np.float32)-np.float32(143.5); xl=np.arange(192,dtype=np.float32)*np.float32(1.5)-np.float32(143.5)
    hr=np.float32(0.1)+np.float32(0.8)/(np.float32(1.0)+np.exp(-xh/np.float32(width)))
    lr=np.float32(0.1)+np.float32(0.8)/(np.float32(1.0)+np.exp(-xl/np.float32(width)))
    return np.repeat(np.repeat(hr[None,:,None],288,axis=0),3,axis=2),np.repeat(np.repeat(lr[None,:,None],192,axis=0),3,axis=2)

def run_analytic():
    rows=[]
    for f in [0.07,0.12,0.18,0.25]:
        hr,lr=analytic_sine(f)
        b=render(lr,'bilinear'); v1=render(lr,'v01'); v2,g=render(lr,'v02',return_gate=True)
        rows.append({'probe':f'freq_{f:.2f}','bilinear_psnr':psnr(hr,b),'v01_psnr':psnr(hr,v1),'v02_psnr':psnr(hr,v2),'v02_minus_bilinear':psnr(hr,v2)-psnr(hr,b),'gate_mean':float(g.mean()),'gate_p95':float(np.quantile(g,.95))})
    hr,lr=analytic_soft_edge()
    b=render(lr,'bilinear'); v1=render(lr,'v01'); v2,g=render(lr,'v02',return_gate=True)
    rows.append({'probe':'soft_edge','bilinear_psnr':psnr(hr,b),'v01_psnr':psnr(hr,v1),'v02_psnr':psnr(hr,v2),'v02_minus_bilinear':psnr(hr,v2)-psnr(hr,b),'gate_mean':float(g.mean()),'gate_p95':float(np.quantile(g,.95))})
    return rows

def run_corpus(corpus,outdir):
    manifest=corpus/'CORPUS_MANIFEST.csv'
    with manifest.open(newline='',encoding='utf-8') as f: rows=list(csv.DictReader(f))
    results=[]
    for i,r in enumerate(rows,1):
        hr=load_rgb(corpus/r['reference_path']); lr=load_rgb(corpus/r['input_path'])
        b=render(lr,'bilinear'); v1=render(lr,'v01'); v2,g=render(lr,'v02',return_gate=True); c=cubic4(lr)
        rb=psnr(hr,b); r1=psnr(hr,v1); r2=psnr(hr,v2); rc=psnr(hr,c)
        results.append({'case_id':r['case_id'],'family':r['family'],'bilinear_psnr':rb,'v01_psnr':r1,'v02_psnr':r2,'cubic4_psnr':rc,'v01_gain_vs_bilinear':r1-rb,'v02_gain_vs_bilinear':r2-rb,'gain_retention':None if abs(r1-rb)<1e-12 else (r2-rb)/(r1-rb),'v02_ssim':ssim(hr,v2),'gate_mean':float(g.mean()),'gate_p95':float(np.quantile(g,.95)),'gate_gt_0_5':float(np.mean(g>0.5))})
        print(f'[{i:02d}/{len(rows)}] {r["case_id"]}: B={rb:.4f} V1={r1:.4f} V2={r2:.4f} C4={rc:.4f}')
    outdir.mkdir(parents=True,exist_ok=True)
    with (outdir/'PER_CASE.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(results[0])); w.writeheader(); w.writerows(results)
    families={}
    for r in results:families.setdefault(r['family'],[]).append(r)
    agg=[]
    for fam,rr in sorted(families.items()):
        agg.append({'family':fam,'cases':len(rr),'bilinear_psnr':sum(x['bilinear_psnr'] for x in rr)/len(rr),'v01_psnr':sum(x['v01_psnr'] for x in rr)/len(rr),'v02_psnr':sum(x['v02_psnr'] for x in rr)/len(rr),'cubic4_psnr':sum(x['cubic4_psnr'] for x in rr)/len(rr),'v01_gain':sum(x['v01_gain_vs_bilinear'] for x in rr)/len(rr),'v02_gain':sum(x['v02_gain_vs_bilinear'] for x in rr)/len(rr),'gate_mean':sum(x['gate_mean'] for x in rr)/len(rr)})
    with (outdir/'FAMILY.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(agg[0])); w.writeheader(); w.writerows(agg)
    summary={'cases':len(results),'mean_bilinear_psnr':sum(x['bilinear_psnr'] for x in results)/len(results),'mean_v01_psnr':sum(x['v01_psnr'] for x in results)/len(results),'mean_v02_psnr':sum(x['v02_psnr'] for x in results)/len(results),'mean_cubic4_psnr':sum(x['cubic4_psnr'] for x in results)/len(results),'v02_better_than_v01_cases':sum(x['v02_psnr']>x['v01_psnr'] for x in results),'v02_better_than_bilinear_cases':sum(x['v02_psnr']>x['bilinear_psnr'] for x in results),'analytic':run_analytic(),'gate_constants':{'rel':[float(REL_LO),float(REL_HI)],'abs':[float(ABS_LO),float(ABS_HI)],'fusion':'sqrt(gRel*gAbs)'}}
    (outdir/'SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    return summary

def parity_random(seed=12345,n=200000):
    # Independent scalar/vectorized float32 check of gate formula itself.
    rng=np.random.default_rng(seed)
    s=rng.random((n,4,3),dtype=np.float32)
    y=np.sum(s*LUMA[None,None,:],axis=2,dtype=np.float32)
    ac=np.maximum(np.abs(y[:,0]-2*y[:,1]+y[:,2]),np.abs(y[:,1]-2*y[:,2]+y[:,3])).astype(np.float32)
    slope=np.maximum(np.abs(y[:,1]-y[:,0]),np.maximum(np.abs(y[:,2]-y[:,1]),np.abs(y[:,3]-y[:,2]))).astype(np.float32)
    vec=np.sqrt(sat((ac/(slope+EPS)-REL_LO)/(REL_HI-REL_LO))*sat((ac-ABS_LO)/(ABS_HI-ABS_LO))).astype(np.float32)
    out=np.empty(n,dtype=np.float32)
    for i in range(n):
        ym1,y0,y1,y2=[np.float32(v) for v in y[i]]
        c0=np.float32(abs(np.float32(ym1-np.float32(2)*y0+y1)))
        c1=np.float32(abs(np.float32(y0-np.float32(2)*y1+y2)))
        acs=np.float32(max(c0,c1)); sl=np.float32(max(abs(np.float32(y0-ym1)),abs(np.float32(y1-y0)),abs(np.float32(y2-y1))))
        rc=np.float32(acs/np.float32(sl+EPS))
        gr=np.float32(min(1,max(0,np.float32((rc-REL_LO)/(REL_HI-REL_LO)))))
        ga=np.float32(min(1,max(0,np.float32((acs-ABS_LO)/(ABS_HI-ABS_LO)))))
        out[i]=np.float32(math.sqrt(float(np.float32(gr*ga))))
    diff=np.abs(vec-out)
    return {'samples':n,'max_abs_gate_diff':float(diff.max()),'exact_float32_matches':int(np.count_nonzero(diff==0)),'within_1e_7':int(np.count_nonzero(diff<=1e-7))}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--corpus');ap.add_argument('--out',default='lab_results/moe_ng_v02');ap.add_argument('--analytic-only',action='store_true');args=ap.parse_args()
    par=parity_random(n=20000)
    print('PARITY',json.dumps(par))
    ar=run_analytic();print(json.dumps(ar,indent=2))
    out=Path(args.out);out.mkdir(parents=True,exist_ok=True);(out/'ANALYTIC.json').write_text(json.dumps(ar,indent=2)+'\n');(out/'PARITY.json').write_text(json.dumps(par,indent=2)+'\n')
    if not args.analytic_only:
        if not args.corpus:raise SystemExit('--corpus required')
        s=run_corpus(Path(args.corpus),out);print(json.dumps(s,indent=2))

if __name__=='__main__':main()

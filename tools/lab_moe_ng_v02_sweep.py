#!/usr/bin/env python3
import argparse,csv,itertools,json,math
from pathlib import Path
import numpy as np
from PIL import Image

EPS=np.float32(1e-6)
LUMA=np.asarray([0.2126,0.7152,0.0722],dtype=np.float32)
STRUCT_FAMILIES={'curves_high_frequency','raster_repeated','thin_oblique_edges','ui_text','silhouette_edge','pixel_art'}
NATURAL_FAMILIES={'natural_edges','natural_gray','natural_scientific','foliage_texture','mixed_frequency','repeated_texture','texture'}

def sat(x):return np.clip(x,np.float32(0),np.float32(1))
def psnr(a,b):
 d=np.asarray(a,dtype=np.float64)-np.asarray(b,dtype=np.float64);m=float(np.mean(d*d));return float('inf') if m==0 else 10*math.log10(1/m)
def load(p):
 with Image.open(p) as im:return np.asarray(im.convert('RGB'),dtype=np.float32)/np.float32(255)
def sample(a,x,y):
 h,w,_=a.shape;x0=np.floor(x).astype(np.int32);y0=np.floor(y).astype(np.int32);tx=(x-x0).astype(np.float32);ty=(y-y0).astype(np.float32)
 xa=np.clip(x0,0,w-1);xb=np.clip(x0+1,0,w-1);ya=np.clip(y0,0,h-1);yb=np.clip(y0+1,0,h-1)
 A=a[ya,xa];B=a[ya,xb];C=a[yb,xa];D=a[yb,xb];tx=tx[...,None];ty=ty[...,None]
 return ((A*(1-tx)+B*tx)*(1-ty)+(C*(1-tx)+D*tx)*ty).astype(np.float32)
def components(lr):
 oy,ox=np.mgrid[0:288,0:288];ox=ox.astype(np.uint32);oy=oy.astype(np.uint32);sx=ox.astype(np.float32)*(np.float32(2)/3);sy=oy.astype(np.float32)*(np.float32(2)/3);fx=np.floor(sx).astype(np.int32);fy=np.floor(sy).astype(np.int32)
 h,w,_=lr.shape;x0=np.clip(fx,0,w-1);x1=np.clip(fx+1,0,w-1);y0=np.clip(fy,0,h-1);y1=np.clip(fy+1,0,h-1)
 tl=lr[y0,x0,1];tr=lr[y0,x1,1];bl=lr[y1,x0,1];br=lr[y1,x1,1];gx=(tr+br)-(tl+bl);gy=(bl+br)-(tl+tr);agx=np.abs(gx);agy=np.abs(gy);ux=agx>=agy
 bx=np.where(ux,fx.astype(np.float32),sx);by=np.where(ux,sy,fy.astype(np.float32));ax=ux.astype(np.float32);ay=(~ux).astype(np.float32)
 fm1=sample(lr,bx-ax,by-ay);f0=sample(lr,bx,by);f1=sample(lr,bx+ax,by+ay);f2=sample(lr,bx+2*ax,by+2*ay);phase=np.where(ux,ox%3,oy%3)
 pf=np.where(phase==1,np.float32(2/3),np.float32(1/3));bil=f0*(1-pf[...,None])+f1*pf[...,None]
 k13=-np.float32(.274074074074074)*fm1+np.float32(.877777777777778)*f0+np.float32(.533333333333333)*f1-np.float32(.137037037037037)*f2
 k23=-np.float32(.137037037037037)*fm1+np.float32(.533333333333333)*f0+np.float32(.877777777777778)*f1-np.float32(.274074074074074)*f2
 edge=np.where((phase==2)[...,None],k13,k23);ras=np.minimum(np.maximum(edge,np.minimum(f0,f1)),np.maximum(f0,f1));nat=bil*np.float32(.3)+ras*np.float32(.7)
 lrng=np.maximum(np.maximum(tl,tr),np.maximum(bl,br))-np.minimum(np.minimum(tl,tr),np.minimum(bl,br));grad=np.float32(.5)*np.maximum(agx,agy);diag=np.float32(.5)*np.abs((tl+br)-(tr+bl));coh=np.abs(agx-agy)/(agx+agy+EPS)
 rw=sat((lrng-np.float32(.01))/np.float32(.10))*sat((coh-np.float32(.25))/np.float32(.45))*(np.float32(1)-sat((diag/(grad+EPS)-np.float32(.08))/np.float32(.45)));eb=sat((grad-np.float32(.02))/np.float32(.10));ew=(1-rw)*eb;nw=1-rw-ew
 moe=nat*nw[...,None]+edge*ew[...,None]+ras*rw[...,None]
 b=np.where((phase==0)[...,None],f0,bil);m=np.where((phase==0)[...,None],f0,moe)
 y=np.stack([np.sum(q*LUMA,axis=2,dtype=np.float32) for q in (fm1,f0,f1,f2)],axis=2)
 ac=np.maximum(np.abs(y[:,:,0]-2*y[:,:,1]+y[:,:,2]),np.abs(y[:,:,1]-2*y[:,:,2]+y[:,:,3])).astype(np.float32);sl=np.maximum(np.abs(y[:,:,1]-y[:,:,0]),np.maximum(np.abs(y[:,:,2]-y[:,:,1]),np.abs(y[:,:,3]-y[:,:,2]))).astype(np.float32);rc=ac/(sl+EPS)
 return b.astype(np.float32),m.astype(np.float32),ac,rc

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--corpus',required=True);ap.add_argument('--out',required=True);a=ap.parse_args();cor=Path(a.corpus);out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
 rows=list(csv.DictReader((cor/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')));cases=[]
 for i,r in enumerate(rows,1):
  hr=load(cor/r['reference_path']);lr=load(cor/r['input_path']);b,m,ac,rc=components(lr);pb=psnr(hr,b);pm=psnr(hr,m);cases.append((r['case_id'],r['family'],hr,b,m,ac,rc,pb,pm));print(f'precompute {i:02d}/{len(rows)} {r["case_id"]}')
 # LAB02 product-only sweep. Purpose: determine whether the expensive fractional
 # power can be removed while preserving >=97.5% structural gain.
 rel_los=[0.0,.05,.10,.15,.20];rel_his=[.45,.60,.75,.90];abs_los=[0.0,.005,.010,.015,.020];abs_his=[.24,.26,.28,.30,.32];exps=[1.0]
 res=[]
 for rlo,rhi,alo,ahi,exp in itertools.product(rel_los,rel_his,abs_los,abs_his,exps):
  if rhi<=rlo or ahi<=alo:continue
  vals=[]
  for cid,fam,hr,b,m,ac,rc,pb,pm in cases:
   gr=sat((rc-rlo)/(rhi-rlo));ga=sat((ac-alo)/(ahi-alo));g=gr*ga;v=b+(m-b)*g[...,None];pv=psnr(hr,v);vals.append((fam,pb,pm,pv))
  mean=sum(x[3] for x in vals)/len(vals)
  struct=[x for x in vals if x[0] in STRUCT_FAMILIES];sg1=sum(x[2]-x[1] for x in struct);sg2=sum(x[3]-x[1] for x in struct);ret=sg2/sg1 if sg1 else 0
  freq=[x for x in vals if x[0]=='mixed_frequency_synthetic'];freq_gap=sum(x[3]-x[1] for x in freq)/len(freq)
  nat=[x for x in vals if x[0] in NATURAL_FAMILIES];nat_gap=sum(x[3]-x[1] for x in nat)/len(nat);nat_worst=min(x[3]-x[1] for x in nat)
  v1better=sum(x[3]>x[2] for x in vals);bbetter=sum(x[3]>x[1] for x in vals)
  score=mean-8*max(0,.975-ret)-.5*max(0,1.0-freq_gap)-.5*max(0,-.15-nat_worst)
  res.append({'rel_lo':rlo,'rel_hi':rhi,'abs_lo':alo,'abs_hi':ahi,'exp':exp,'mean_psnr':mean,'struct_gain_retention':ret,'freq_synth_gap_vs_bilinear':freq_gap,'natural_mean_gap_vs_bilinear':nat_gap,'natural_worst_gap_vs_bilinear':nat_worst,'better_than_v01_cases':v1better,'better_than_bilinear_cases':bbetter,'score':score})
 res.sort(key=lambda x:x['score'],reverse=True)
 with (out/'SWEEP_ALL.csv').open('w',newline='',encoding='utf-8') as f:w=csv.DictWriter(f,fieldnames=list(res[0]));w.writeheader();w.writerows(res)
 feasible=[x for x in res if x['struct_gain_retention']>=.975 and x['freq_synth_gap_vs_bilinear']>=1.0 and x['natural_worst_gap_vs_bilinear']>=-.15]
 feasible.sort(key=lambda x:x['mean_psnr'],reverse=True)
 (out/'SWEEP_TOP.json').write_text(json.dumps(res[:30],indent=2)+'\n',encoding='utf-8')
 (out/'PARETO_FEASIBLE.json').write_text(json.dumps(feasible[:50],indent=2)+'\n',encoding='utf-8')
 print('TOP SCORE');print(json.dumps(res[:20],indent=2));print('FEASIBLE PRODUCT');print(json.dumps(feasible[:20],indent=2))
if __name__=='__main__':main()

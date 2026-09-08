#!/usr/bin/env python3
"""LAB04 monotone-output sweep for PTAR-NG MoE v02.

Tests whether the cross-protocol failures are predominantly v01 overshoot.
No extra texture fetch is added. The candidate blends v01 toward a monotone
projection using the f0/f1 bounds already computed by the RASTER expert:
  moeSafe = clamp(moeV01,min(f0,f1),max(f0,f1))
  expert  = lerp(moeV01,moeSafe,safeStrength)
  output  = lerp(bilinear,expert,curvatureGate)
A+B from B-GRID and V1_A are training; crop C is holdout.
"""
import argparse,csv,itertools,json,math
from pathlib import Path
import numpy as np
from PIL import Image

EPS=np.float32(1e-6);LUMA=np.asarray([.2126,.7152,.0722],np.float32)
STRUCT_FAMILIES={'curves_high_frequency','raster_repeated','thin_oblique_edges','ui_text','silhouette_edge','pixel_art'}
BATCH=24

def sat(x):return np.clip(x,np.float32(0),np.float32(1))
def load(p):
    with Image.open(p) as im:return np.asarray(im.convert('RGB'),dtype=np.float32)/np.float32(255)
def sample(a,x,y):
    h,w,_=a.shape;x0=np.floor(x).astype(np.int32);y0=np.floor(y).astype(np.int32);tx=(x-x0).astype(np.float32);ty=(y-y0).astype(np.float32)
    xa=np.clip(x0,0,w-1);xb=np.clip(x0+1,0,w-1);ya=np.clip(y0,0,h-1);yb=np.clip(y0+1,0,h-1)
    A=a[ya,xa];B=a[ya,xb];C=a[yb,xa];D=a[yb,xb];tx=tx[...,None];ty=ty[...,None]
    return ((A*(1-tx)+B*tx)*(1-ty)+(C*(1-tx)+D*tx)*ty).astype(np.float32)
def psnr_mse(m):
    m=float(m);return float('inf') if m<=0 else 10*math.log10(1/m)

def planes(lr):
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
    rw=sat((lrng-np.float32(.01))/np.float32(.10))*sat((coh-np.float32(.25))/np.float32(.45))*(np.float32(1)-sat((diag/(grad+EPS)-np.float32(.08))/np.float32(.45)));ew=(np.float32(1)-rw)*sat((grad-np.float32(.02))/np.float32(.10));nw=np.float32(1)-rw-ew
    moe=nat*nw[...,None]+edge*ew[...,None]+ras*rw[...,None]
    lo=np.minimum(f0,f1);hi=np.maximum(f0,f1);safe=np.minimum(np.maximum(moe,lo),hi)
    b=np.where((phase==0)[...,None],f0,bil);m=np.where((phase==0)[...,None],f0,moe);s=np.where((phase==0)[...,None],f0,safe)
    y=np.stack([np.sum(q*LUMA,axis=2,dtype=np.float32) for q in (fm1,f0,f1,f2)],axis=2);ac=np.maximum(np.abs(y[:,:,0]-2*y[:,:,1]+y[:,:,2]),np.abs(y[:,:,1]-2*y[:,:,2]+y[:,:,3])).astype(np.float32);sl=np.maximum(np.abs(y[:,:,1]-y[:,:,0]),np.maximum(np.abs(y[:,:,2]-y[:,:,1]),np.abs(y[:,:,3]-y[:,:,2]))).astype(np.float32);rc=ac/(sl+EPS)
    return b.astype(np.float32),m.astype(np.float32),s.astype(np.float32),ac,rc

def prep(corpus,row,proto):
    hr=load(corpus/row['reference_path']);lr=load(corpus/row['input_path']);b,m,s,ac,rc=planes(lr)
    e=(b-hr).astype(np.float32);dm=(m-b).astype(np.float32);ds=(s-b).astype(np.float32)
    q0=np.mean(e*e,axis=2,dtype=np.float32)
    return {'protocol':proto,'case_id':row['case_id'],'family':row['family'],'crop_tag':row.get('crop_tag',''),'pb':psnr_mse(q0.mean()),'pm':psnr_mse(np.mean((m-hr)**2,dtype=np.float64)),'ac':ac,'rc':rc,'q0':q0,'e':e,'dm':dm,'ds':ds}
def candidates():
    return [(0.,rhi,alo,ahi,ss) for rhi,alo,ahi,ss in itertools.product([.75,.90,1.10],[0.,.01,.02],[.20,.24,.32],[.25,.50,.75,1.0])]
def matrix(cases,C):
    P=np.empty((len(C),len(cases)),np.float64);G=np.empty_like(P,dtype=np.float32)
    for j,c in enumerate(cases):
        ac,rc=c['ac'],c['rc'];e=c['e'];dm=c['dm'];ds=c['ds']
        for st in range(0,len(C),BATCH):
            bb=C[st:st+BATCH];rhi=np.asarray([x[1] for x in bb],np.float32)[:,None,None];alo=np.asarray([x[2] for x in bb],np.float32)[:,None,None];ahi=np.asarray([x[3] for x in bb],np.float32)[:,None,None];ss=np.asarray([x[4] for x in bb],np.float32)[:,None,None,None]
            g=(sat(rc[None]/rhi)*sat((ac[None]-alo)/(ahi-alo))).astype(np.float32);d=dm[None]*(np.float32(1)-ss)+ds[None]*ss;err=e[None]+d*g[:,:,:,None]
            mse=np.mean(err*err,axis=(1,2,3),dtype=np.float64);P[st:st+len(bb),j]=[psnr_mse(x) for x in mse];G[st:st+len(bb),j]=np.mean(g,axis=(1,2),dtype=np.float64)
    return P,G
def summarize(i,cand,cases,P,G):
    rows=[]
    for j,c in enumerate(cases):
        p=float(P[i,j]);rows.append({'protocol':c['protocol'],'case_id':c['case_id'],'family':c['family'],'crop_tag':c['crop_tag'],'bilinear_psnr':c['pb'],'v01_psnr':c['pm'],'v02_psnr':p,'gap_bilinear':p-c['pb'],'gap_v01':p-c['pm'],'gate_mean':float(G[i,j])})
    bg=[x for x in rows if x['protocol']=='B_GRID'];va=[x for x in rows if x['protocol']=='V1_A'];struct=[x for x in bg if x['family'] in STRUCT_FAMILIES and x['v01_psnr']>x['bilinear_psnr']];den=sum(x['v01_psnr']-x['bilinear_psnr'] for x in struct);num=sum(x['v02_psnr']-x['bilinear_psnr'] for x in struct);ret=num/den if den else 1
    mean=lambda xs,k:sum(x[k] for x in xs)/len(xs)
    return rows,{'rel_hi':cand[1],'abs_lo':cand[2],'abs_hi':cand[3],'safe_strength':cand[4],'mean_psnr':mean(rows,'v02_psnr'),'mean_gap':mean(rows,'gap_bilinear'),'worst_gap':min(x['gap_bilinear'] for x in rows),'bg_struct_retention':ret,'bg_mean_gap':mean(bg,'gap_bilinear'),'v1a_mean_gap':mean(va,'gap_bilinear'),'bg_worst_gap':min(x['gap_bilinear'] for x in bg),'v1a_worst_gap':min(x['gap_bilinear'] for x in va),'better_bilinear':sum(x['gap_bilinear']>0 for x in rows),'better_v01':sum(x['gap_v01']>0 for x in rows)}
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--bgrid',required=True);ap.add_argument('--v1a',required=True);ap.add_argument('--out',required=True);a=ap.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True);train=[];hold=[]
    for proto,p in [('B_GRID',Path(a.bgrid)),('V1_A',Path(a.v1a))]:
        rr=list(csv.DictReader((p/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for n,r in enumerate(rr,1):c=prep(p,r,proto);(hold if r.get('crop_tag')=='C' else train).append(c);print(proto,n,r['case_id'])
    C=candidates();P,G=matrix(train,C);R=[]
    for i,c in enumerate(C):
        _,r=summarize(i,c,train,P,G);r['score']=r['mean_psnr']-12*max(0,-.15-r['worst_gap'])-6*max(0,.94-r['bg_struct_retention']);R.append(r)
    feasible=[r for r in R if r['worst_gap']>=-.15 and r['bg_struct_retention']>=.94];sel=max(feasible,key=lambda r:r['mean_psnr']) if feasible else max(R,key=lambda r:r['score']);ct=(0.,sel['rel_hi'],sel['abs_lo'],sel['abs_hi'],sel['safe_strength']);idx=C.index(ct);trc,tr=summarize(idx,ct,train,P,G);HP,HG=matrix(hold,[ct]);hoc,ho=summarize(0,ct,hold,HP,HG);R.sort(key=lambda r:r['score'],reverse=True)
    for name,rows in [('TRAIN_SELECTED_PER_CASE.csv',trc),('HOLDOUT_C_PER_CASE.csv',hoc)]:
        with (out/name).open('w',newline='',encoding='utf-8') as f:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    with (out/'TRAIN_SWEEP.csv').open('w',newline='',encoding='utf-8') as f:w=csv.DictWriter(f,fieldnames=list(R[0]));w.writeheader();w.writerows(R)
    res={'protocol':'LAB04_MONOTONE_AB_TRAIN_C_HOLDOUT','selection_used_holdout':False,'candidate_count':len(C),'feasible_count':len(feasible),'selected':sel,'train':tr,'holdout_C':ho,'top':R[:20]};(out/'SUMMARY.json').write_text(json.dumps(res,indent=2)+'\n');print(json.dumps(res,indent=2))
if __name__=='__main__':main()

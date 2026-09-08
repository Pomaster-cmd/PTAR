#!/usr/bin/env python3
"""LAB05: shape-preserving Hermite expert sweep for PTAR-NG MoE v02.

Goal: replace the v01 K185-derived correction with a more prefilter-robust
4-sample directional expert. Texture footprint can remain 1 GatherGreen +
4 SampleLevel because fm1/f0/f1/f2 are reused.

Candidate expert families:
  minmod   : endpoint slopes = minmod(adjacent finite differences)
  harmonic : endpoint slopes = harmonic mean when adjacent slopes agree
  mc       : monotonized-central slope limiter
All outputs are additionally clamped to [min(f0,f1),max(f0,f1)].

Selection protocol is non-leaky:
  train = crop tags A+B from B-GRID and V1_A
  holdout = crop tag C from both corpora
The holdout is evaluated only after training selection.
"""
import argparse,csv,itertools,json,math
from pathlib import Path
import numpy as np
from PIL import Image

EPS=np.float32(1e-6)
LUMA=np.asarray([0.2126,0.7152,0.0722],dtype=np.float32)
STRUCT_FAMILIES={'curves_high_frequency','raster_repeated','thin_oblique_edges','ui_text','silhouette_edge','pixel_art'}
BATCH=24


def sat(x): return np.clip(x,np.float32(0),np.float32(1))
def psnr_mse(m):
    m=float(m)
    return float('inf') if m<=0.0 else 10.0*math.log10(1.0/m)
def load(p):
    with Image.open(p) as im:return np.asarray(im.convert('RGB'),dtype=np.float32)/np.float32(255.0)
def sample(a,x,y):
    h,w,_=a.shape
    x0=np.floor(x).astype(np.int32);y0=np.floor(y).astype(np.int32)
    tx=(x-x0).astype(np.float32);ty=(y-y0).astype(np.float32)
    xa=np.clip(x0,0,w-1);xb=np.clip(x0+1,0,w-1);ya=np.clip(y0,0,h-1);yb=np.clip(y0+1,0,h-1)
    A=a[ya,xa];B=a[ya,xb];C=a[yb,xa];D=a[yb,xb]
    tx=tx[...,None];ty=ty[...,None]
    return ((A*(1-tx)+B*tx)*(1-ty)+(C*(1-tx)+D*tx)*ty).astype(np.float32)

def minmod2(a,b):
    same=(a*b)>np.float32(0)
    return np.where(same,np.sign(a)*np.minimum(np.abs(a),np.abs(b)),np.float32(0)).astype(np.float32)
def harmonic(a,b):
    same=(a*b)>np.float32(0)
    den=a+b
    val=np.where(np.abs(den)>EPS,(np.float32(2)*a*b)/(den+np.where(den>=0,EPS,-EPS)),np.float32(0))
    return np.where(same,val,np.float32(0)).astype(np.float32)
def mc(a,b):
    # minmod((a+b)/2, 2a, 2b), component-wise.
    c=np.float32(.5)*(a+b)
    r=minmod2(c,np.float32(2)*a)
    return minmod2(r,np.float32(2)*b)

def hermite(f0,f1,m0,m1,t):
    t=np.asarray(t,dtype=np.float32)[...,None]
    t2=t*t;t3=t2*t
    h00=np.float32(2)*t3-np.float32(3)*t2+np.float32(1)
    h10=t3-np.float32(2)*t2+t
    h01=-np.float32(2)*t3+np.float32(3)*t2
    h11=t3-t2
    out=h00*f0+h10*m0+h01*f1+h11*m1
    return np.minimum(np.maximum(out,np.minimum(f0,f1)),np.maximum(f0,f1)).astype(np.float32)

def planes(lr):
    oy,ox=np.mgrid[0:288,0:288];ox=ox.astype(np.uint32);oy=oy.astype(np.uint32)
    sx=ox.astype(np.float32)*(np.float32(2)/3);sy=oy.astype(np.float32)*(np.float32(2)/3)
    fx=np.floor(sx).astype(np.int32);fy=np.floor(sy).astype(np.int32)
    h,w,_=lr.shape;x0=np.clip(fx,0,w-1);x1=np.clip(fx+1,0,w-1);y0=np.clip(fy,0,h-1);y1=np.clip(fy+1,0,h-1)
    tl=lr[y0,x0,1];tr=lr[y0,x1,1];bl=lr[y1,x0,1];br=lr[y1,x1,1]
    gx=(tr+br)-(tl+bl);gy=(bl+br)-(tl+tr);agx=np.abs(gx);agy=np.abs(gy);ux=agx>=agy
    bx=np.where(ux,fx.astype(np.float32),sx);by=np.where(ux,sy,fy.astype(np.float32));ax=ux.astype(np.float32);ay=(~ux).astype(np.float32)
    fm1=sample(lr,bx-ax,by-ay);f0=sample(lr,bx,by);f1=sample(lr,bx+ax,by+ay);f2=sample(lr,bx+np.float32(2)*ax,by+np.float32(2)*ay)
    phase=np.where(ux,ox%3,oy%3)
    t=np.where(phase==1,np.float32(2/3),np.float32(1/3)).astype(np.float32)
    bil=(f0*(np.float32(1)-t[...,None])+f1*t[...,None]).astype(np.float32)
    d0=f0-fm1;d1=f1-f0;d2=f2-f1
    experts={}
    for name,fn in [('minmod',minmod2),('harmonic',harmonic),('mc',mc)]:
        m0=fn(d0,d1);m1=fn(d1,d2)
        H=hermite(f0,f1,m0,m1,t)
        experts[name]=np.where((phase==0)[...,None],f0,H).astype(np.float32)
    base=np.where((phase==0)[...,None],f0,bil).astype(np.float32)
    y=np.stack([np.sum(q*LUMA,axis=2,dtype=np.float32) for q in (fm1,f0,f1,f2)],axis=2)
    ac=np.maximum(np.abs(y[:,:,0]-np.float32(2)*y[:,:,1]+y[:,:,2]),np.abs(y[:,:,1]-np.float32(2)*y[:,:,2]+y[:,:,3])).astype(np.float32)
    slope=np.maximum(np.abs(y[:,:,1]-y[:,:,0]),np.maximum(np.abs(y[:,:,2]-y[:,:,1]),np.abs(y[:,:,3]-y[:,:,2]))).astype(np.float32)
    rc=ac/(slope+EPS)
    return base,experts,ac,rc

def prep(corpus,row,proto):
    hr=load(corpus/row['reference_path']);lr=load(corpus/row['input_path'])
    b,E,ac,rc=planes(lr)
    err=(b-hr).astype(np.float32)
    deltas={k:(v-b).astype(np.float32) for k,v in E.items()}
    q0=np.mean(err*err,axis=2,dtype=np.float32)
    return {'protocol':proto,'case_id':row['case_id'],'family':row['family'],'crop_tag':row.get('crop_tag',''),'pb':psnr_mse(q0.mean()),'ac':ac,'rc':rc,'err':err,'delta':deltas}

def candidates():
    # Expert family x route thresholds x blend strength.
    out=[]
    for fam,rhi,alo,ahi,strength in itertools.product(
            ['minmod','harmonic','mc'],[.60,.75,.90,1.10,1.30],[0.,.005,.01,.02],[.16,.20,.24,.32,.40],[.35,.50,.65,.80,1.0]):
        out.append((fam,rhi,alo,ahi,strength))
    return out

def matrix(cases,C):
    P=np.empty((len(C),len(cases)),dtype=np.float64);G=np.empty_like(P,dtype=np.float32)
    for j,c in enumerate(cases):
        ac,rc=c['ac'],c['rc'];err=c['err']
        for start in range(0,len(C),BATCH):
            bb=C[start:start+BATCH]
            rhi=np.asarray([x[1] for x in bb],np.float32)[:,None,None]
            alo=np.asarray([x[2] for x in bb],np.float32)[:,None,None]
            ahi=np.asarray([x[3] for x in bb],np.float32)[:,None,None]
            strength=np.asarray([x[4] for x in bb],np.float32)[:,None,None]
            g=(sat(rc[None]/rhi)*sat((ac[None]-alo)/(ahi-alo))*strength).astype(np.float32)
            # Families differ per row, so group inside the small batch.
            mse=np.empty(len(bb),dtype=np.float64)
            for bi,cand in enumerate(bb):
                d=c['delta'][cand[0]]
                er=err+d*g[bi,:,:,None]
                mse[bi]=np.mean(er*er,dtype=np.float64)
            P[start:start+len(bb),j]=[psnr_mse(x) for x in mse]
            G[start:start+len(bb),j]=np.mean(g,axis=(1,2),dtype=np.float64)
    return P,G

def summarize(i,cand,cases,P,G):
    rows=[]
    for j,c in enumerate(cases):
        pv=float(P[i,j]);rows.append({'protocol':c['protocol'],'case_id':c['case_id'],'family':c['family'],'crop_tag':c['crop_tag'],'bilinear_psnr':c['pb'],'v02_psnr':pv,'gap_bilinear':pv-c['pb'],'gate_mean':float(G[i,j])})
    bg=[x for x in rows if x['protocol']=='B_GRID'];va=[x for x in rows if x['protocol']=='V1_A']
    struct=[x for x in bg if x['family'] in STRUCT_FAMILIES]
    # Structural gain is measured against bilinear directly because LAB05 is a new expert.
    mean=lambda xs,k:sum(x[k] for x in xs)/len(xs)
    return rows,{
        'family':cand[0],'rel_hi':cand[1],'abs_lo':cand[2],'abs_hi':cand[3],'strength':cand[4],
        'mean_psnr':mean(rows,'v02_psnr'),'mean_gap':mean(rows,'gap_bilinear'),'worst_gap':min(x['gap_bilinear'] for x in rows),
        'bg_struct_mean_gap':mean(struct,'gap_bilinear'),'bg_mean_gap':mean(bg,'gap_bilinear'),'v1a_mean_gap':mean(va,'gap_bilinear'),
        'bg_worst_gap':min(x['gap_bilinear'] for x in bg),'v1a_worst_gap':min(x['gap_bilinear'] for x in va),
        'better_bilinear':sum(x['gap_bilinear']>0 for x in rows),'mean_gate':mean(rows,'gate_mean')}
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--bgrid',required=True);ap.add_argument('--v1a',required=True);ap.add_argument('--out',required=True);a=ap.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    train=[];hold=[]
    for proto,path in [('B_GRID',Path(a.bgrid)),('V1_A',Path(a.v1a))]:
        rr=list(csv.DictReader((path/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for n,r in enumerate(rr,1):
            c=prep(path,r,proto);(hold if r.get('crop_tag')=='C' else train).append(c);print(f'{proto} {n:02d}/{len(rr)} {r["case_id"]}')
    C=candidates();print('candidates',len(C));P,G=matrix(train,C);R=[]
    for i,c in enumerate(C):
        _,r=summarize(i,c,train,P,G)
        # Train-only objective: maximize quality while strongly avoiding any large
        # cross-protocol regression; require some genuine B-GRID structure gain.
        r['score']=r['mean_psnr']-12*max(0,-.15-r['worst_gap'])-4*max(0,.20-r['bg_struct_mean_gap'])
        R.append(r)
    feasible=[r for r in R if r['worst_gap']>=-.15 and r['bg_struct_mean_gap']>=.20]
    sel=max(feasible,key=lambda r:r['mean_psnr']) if feasible else max(R,key=lambda r:r['score'])
    ct=(sel['family'],sel['rel_hi'],sel['abs_lo'],sel['abs_hi'],sel['strength']);idx=C.index(ct)
    trc,tr=summarize(idx,ct,train,P,G);HP,HG=matrix(hold,[ct]);hoc,ho=summarize(0,ct,hold,HP,HG);R.sort(key=lambda r:r['score'],reverse=True)
    for name,rows in [('TRAIN_SELECTED_PER_CASE.csv',trc),('HOLDOUT_C_PER_CASE.csv',hoc)]:
        with (out/name).open('w',newline='',encoding='utf-8') as f:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    with (out/'TRAIN_SWEEP.csv').open('w',newline='',encoding='utf-8') as f:w=csv.DictWriter(f,fieldnames=list(R[0]));w.writeheader();w.writerows(R)
    res={'protocol':'LAB05_HERMITE_AB_TRAIN_C_HOLDOUT','selection_used_holdout':False,'candidate_count':len(C),'feasible_count':len(feasible),'selected':sel,'train':tr,'holdout_C':ho,'top':R[:30]}
    (out/'SUMMARY.json').write_text(json.dumps(res,indent=2)+'\n',encoding='utf-8');print(json.dumps(res,indent=2))
if __name__=='__main__':main()

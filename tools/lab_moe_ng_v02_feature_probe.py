#!/usr/bin/env python3
"""Diagnostic feature probe for MoE v02 routing failures.
No candidate selection is performed here."""
import argparse,csv,json
from pathlib import Path
import numpy as np
from lab_moe_ng_v02_sweep import components,load,sat

EPS=np.float32(1e-6)


def gather_features(lr):
    oy,ox=np.mgrid[0:288,0:288]
    sx=ox.astype(np.float32)*(np.float32(2)/3);sy=oy.astype(np.float32)*(np.float32(2)/3)
    fx=np.floor(sx).astype(np.int32);fy=np.floor(sy).astype(np.int32)
    h,w,_=lr.shape;x0=np.clip(fx,0,w-1);x1=np.clip(fx+1,0,w-1);y0=np.clip(fy,0,h-1);y1=np.clip(fy+1,0,h-1)
    tl=lr[y0,x0,1];tr=lr[y0,x1,1];bl=lr[y1,x0,1];br=lr[y1,x1,1]
    gx=(tr+br)-(tl+bl);gy=(bl+br)-(tl+tr);agx=np.abs(gx);agy=np.abs(gy)
    lrng=np.maximum(np.maximum(tl,tr),np.maximum(bl,br))-np.minimum(np.minimum(tl,tr),np.minimum(bl,br))
    grad=np.float32(.5)*np.maximum(agx,agy)
    diag=np.float32(.5)*np.abs((tl+br)-(tr+bl))
    coh=np.abs(agx-agy)/(agx+agy+EPS)
    dr=diag/(grad+EPS)
    range_conf=sat((lrng-np.float32(.01))/np.float32(.10))
    coherence_conf=sat((coh-np.float32(.25))/np.float32(.45))
    axis_conf=np.float32(1)-sat((dr-np.float32(.08))/np.float32(.45))
    rw=range_conf*coherence_conf*axis_conf
    ew=(np.float32(1)-rw)*sat((grad-np.float32(.02))/np.float32(.10))
    return lrng.astype(np.float32),grad.astype(np.float32),coh.astype(np.float32),dr.astype(np.float32),rw.astype(np.float32),ew.astype(np.float32)


def stats(x,prefix):
    x=np.asarray(x,dtype=np.float32)
    return {prefix+'_mean':float(x.mean()),prefix+'_p50':float(np.quantile(x,.5)),prefix+'_p90':float(np.quantile(x,.9)),prefix+'_p95':float(np.quantile(x,.95)),prefix+'_p99':float(np.quantile(x,.99))}


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--corpus',action='append',required=True);ap.add_argument('--out',required=True);a=ap.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
    rowsout=[]
    for cpath in a.corpus:
        corpus=Path(cpath);proto='B_GRID' if 'B_GRID' in corpus.name else 'V1_A'
        rows=list(csv.DictReader((corpus/'CORPUS_MANIFEST.csv').open(newline='',encoding='utf-8')))
        for i,r in enumerate(rows,1):
            lr=load(corpus/r['input_path']).astype(np.float32)
            b,m,ac,rc=components(lr)
            slope=ac/(rc+EPS)
            corr=np.max(np.abs(m-b),axis=2).astype(np.float32)
            corr_luma=np.abs(np.sum((m-b)*np.asarray([.2126,.7152,.0722],dtype=np.float32),axis=2,dtype=np.float32))
            lrng,grad,coh,dr,rw,ew=gather_features(lr)
            g=sat(rc/np.float32(.90))*sat((ac-np.float32(.01))/np.float32(.23))
            active=g>.05
            rec={'protocol':proto,'case_id':r['case_id'],'family':r['family'],'crop_tag':r.get('crop_tag',''),'active_frac':float(active.mean())}
            for arr,name in ((ac,'ac'),(rc,'rc'),(slope,'slope'),(corr,'corr'),(corr_luma,'corr_luma'),(lrng,'range'),(grad,'grad'),(coh,'coh'),(dr,'diag_ratio'),(rw,'raster_w'),(ew,'edge_w'),(g,'gate')):
                rec.update(stats(arr,name))
                if np.any(active):rec[name+'_active_mean']=float(arr[active].mean())
            rowsout.append(rec);print(f'{proto} {i:02d}/{len(rows)} {r["case_id"]}')
    with (out/'FEATURES.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(rowsout[0]));w.writeheader();w.writerows(rowsout)
    (out/'FEATURES.json').write_text(json.dumps(rowsout,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()

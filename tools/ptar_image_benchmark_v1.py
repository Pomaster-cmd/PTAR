#!/usr/bin/env python3
"""
PTAR permanent image benchmark harness v1.
Compares already-rendered outputs with reference images.
It does not implement PTAR.
Manifest columns:
case_id,series,reference_path,variant,output_path
"""
import argparse,csv,hashlib,json,math,platform,sys
from pathlib import Path
import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity

HARNESS_VERSION="1.0.0"

def sha256(path):
    h=hashlib.sha256()
    with open(path,"rb") as f:
        for c in iter(lambda:f.read(1024*1024),b""): h.update(c)
    return h.hexdigest()

def load_rgb(path):
    with Image.open(path) as im:
        return np.asarray(im.convert("RGB"),dtype=np.float64)/255.0

def psnr(a,b):
    mse=float(np.mean((a-b)**2))
    return float("inf") if mse==0.0 else 10.0*math.log10(1.0/mse)

def ssim(a,b):
    return float(structural_similarity(
        a,b,data_range=1.0,channel_axis=2,
        gaussian_weights=True,sigma=1.5,use_sample_covariance=False))

def resolve(base,p):
    p=Path(p)
    return p if p.is_absolute() else (base/p).resolve()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("manifest")
    ap.add_argument("--out",required=True)
    args=ap.parse_args()
    manifest=Path(args.manifest).resolve()
    base=manifest.parent
    outdir=Path(args.out).resolve(); outdir.mkdir(parents=True,exist_ok=True)
    with manifest.open(newline="",encoding="utf-8") as f:
        rows=list(csv.DictReader(f))
    required={"case_id","series","reference_path","variant","output_path"}
    if not rows: raise SystemExit("empty manifest")
    if not required.issubset(rows[0].keys()):
        raise SystemExit(f"manifest missing columns: {sorted(required-set(rows[0].keys()))}")

    results=[]
    for r in rows:
        ref=resolve(base,r["reference_path"]); out=resolve(base,r["output_path"])
        if not ref.is_file() or not out.is_file():
            raise SystemExit(f"missing file for {r['case_id']}: {ref} / {out}")
        a=load_rgb(ref); b=load_rgb(out)
        if a.shape!=b.shape:
            raise SystemExit(f"shape mismatch for {r['case_id']}: {a.shape} vs {b.shape}")
        results.append({
            "case_id":r["case_id"],"series":r["series"],"variant":r["variant"],
            "width":a.shape[1],"height":a.shape[0],
            "psnr_db":psnr(a,b),"ssim":ssim(a,b),
            "reference_sha256":sha256(ref),"output_sha256":sha256(out)
        })

    with (outdir/"per_case.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(results[0].keys()))
        w.writeheader(); w.writerows(results)

    groups={}
    for r in results: groups.setdefault((r["series"],r["variant"]),[]).append(r)
    aggregate=[]
    for (series,variant),rr in sorted(groups.items()):
        ps=[x["psnr_db"] for x in rr]
        aggregate.append({
            "series":series,"variant":variant,"cases":len(rr),
            "mean_psnr_db":float("inf") if any(math.isinf(x) for x in ps)
                           else sum(ps)/len(ps),
            "mean_ssim":sum(x["ssim"] for x in rr)/len(rr),
            "min_ssim":min(x["ssim"] for x in rr)
        })
    with (outdir/"aggregate.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(aggregate[0].keys()))
        w.writeheader(); w.writerows(aggregate)

    env={
        "harness_version":HARNESS_VERSION,"python":sys.version,
        "platform":platform.platform(),"numpy":np.__version__,
        "Pillow":Image.__version__,"scikit_image":__import__("skimage").__version__,
        "manifest_sha256":sha256(manifest),"case_count":len(results)
    }
    (outdir/"environment.json").write_text(json.dumps(env,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(env,indent=2))

if __name__=="__main__":
    main()

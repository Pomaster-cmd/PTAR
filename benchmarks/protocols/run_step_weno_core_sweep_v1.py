#!/usr/bin/env python3
import csv, math, json, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"src/reference"))
from ptar_step_weno_reference import reconstruct_scalar

PROBES=ROOT/"corpus/current/PTAR_CORE_PROBES_V1/probes.csv"
OUTDIR=ROOT/"benchmarks/results/current/STEP_WENO_CORE_SWEEP_V1"
OUTDIR.mkdir(parents=True,exist_ok=True)

eps_values=[1e-12,1e-10,1e-8,1e-6,1e-5,1e-4,1e-3,1e-2]
modes=[("SW1",1,False),("SW2",2,True)]

with PROBES.open(newline="",encoding="utf-8") as f:
    probes=list(csv.DictReader(f))

detail=[]
overall=[]
bycat=[]

for mode,power,clamp in modes:
    for eps in eps_values:
        bucket={}
        allr=[]
        for r in probes:
            vals=[float(r[k]) for k in ("fm1","f0","f1","f2")]
            truth=float(r["truth"]); phase=int(r["phase"])
            got=reconstruct_scalar(*vals,*vals,phase,power,eps,clamp)
            err=got-truth
            lo=min(vals[1],vals[2]); hi=max(vals[1],vals[2])
            rec={
                "case_id":r["case_id"],"category":r["category"],"phase":phase,
                "mode":mode,"epsilon":eps,"value":got,"truth":truth,
                "abs_error":abs(err),"sq_error":err*err,
                "central_interval_violation":max(lo-got,got-hi,0.0)
            }
            detail.append(rec); allr.append(rec)
            bucket.setdefault(r["category"],[]).append(rec)

        overall.append({
            "mode":mode,"epsilon":eps,"cases":len(allr),
            "mae":sum(x["abs_error"] for x in allr)/len(allr),
            "rmse":math.sqrt(sum(x["sq_error"] for x in allr)/len(allr)),
            "max_abs_error":max(x["abs_error"] for x in allr),
            "max_central_interval_violation":
                max(x["central_interval_violation"] for x in allr)
        })
        for cat,rr in bucket.items():
            bycat.append({
                "mode":mode,"epsilon":eps,"category":cat,"cases":len(rr),
                "mae":sum(x["abs_error"] for x in rr)/len(rr),
                "rmse":math.sqrt(sum(x["sq_error"] for x in rr)/len(rr)),
                "max_abs_error":max(x["abs_error"] for x in rr),
                "max_central_interval_violation":
                    max(x["central_interval_violation"] for x in rr)
            })

with (OUTDIR/"per_case.csv").open("w",newline="",encoding="utf-8") as f:
    w=csv.DictWriter(f,fieldnames=list(detail[0].keys())); w.writeheader(); w.writerows(detail)

fields=["mode","epsilon","cases","mae","rmse","max_abs_error","max_central_interval_violation"]
with (OUTDIR/"overall.csv").open("w",newline="",encoding="utf-8") as f:
    w=csv.DictWriter(f,fieldnames=fields); w.writeheader(); w.writerows(overall)

fields=["mode","epsilon","category","cases","mae","rmse","max_abs_error","max_central_interval_violation"]
with (OUTDIR/"by_category.csv").open("w",newline="",encoding="utf-8") as f:
    w=csv.DictWriter(f,fieldnames=fields); w.writeheader(); w.writerows(bycat)

best={}
for mode in ("SW1","SW2"):
    rows=[r for r in overall if r["mode"]==mode]
    best[mode]=min(rows,key=lambda x:x["rmse"])

summary={
    "protocol":"STEP_WENO_CORE_SWEEP_V1",
    "corpus_id":"PTAR-CORE-PROBES-V1",
    "purpose":"epsilon sensitivity and directional-core regression only",
    "not_an_acceptance_gate":True,
    "best_rmse_on_this_probe_set":best,
    "epsilon_values":eps_values
}
(OUTDIR/"summary.json").write_text(json.dumps(summary,indent=2)+"\n",encoding="utf-8")
print(json.dumps(summary,indent=2))

#!/usr/bin/env python3
import csv, json, math, random, hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parent
OUT=ROOT/"probes.csv"
META=ROOT/"metadata.json"
SEED=0x50544152

def sha256(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for c in iter(lambda:f.read(1024*1024),b""): h.update(c)
    return h.hexdigest()

rows=[]
def add(case_id, category, phase, func_name, params, f):
    t=1.0/3.0 if phase==1 else 2.0/3.0
    xs=(-1.0,0.0,1.0,2.0)
    vals=[float(f(x)) for x in xs]
    rows.append({
        "case_id":case_id,"category":category,"phase":phase,
        "function":func_name,
        "params":json.dumps(params,sort_keys=True,separators=(",",":")),
        "fm1":vals[0],"f0":vals[1],"f1":vals[2],"f2":vals[3],
        "truth":float(f(t))
    })

cid=0
for phase in (1,2):
    for a in (-0.35,0.0,0.25):
        for b in (-0.7,-0.15,0.4,0.9):
            for c in (0.0,0.3,0.75):
                cid+=1
                add(f"P{cid:04d}","smooth_polynomial",phase,"quadratic",
                    {"a":a,"b":b,"c":c},
                    lambda x,a=a,b=b,c=c:a*x*x+b*x+c)

for phase in (1,2):
    for freq in (0.08,0.15,0.22,0.30,0.38,0.46):
        for phi in (0.0,0.37,0.91,1.73):
            cid+=1
            add(f"P{cid:04d}","sinusoid",phase,"sin",
                {"freq":freq,"phi":phi},
                lambda x,freq=freq,phi=phi:0.5+0.45*math.sin(2*math.pi*freq*x+phi))

for phase in (1,2):
    for center in (-0.35,-0.10,0.15,0.40,0.65,0.90,1.15):
        for sharp in (2.0,5.0,12.0,28.0):
            cid+=1
            add(f"P{cid:04d}","smooth_edge",phase,"logistic",
                {"center":center,"sharp":sharp},
                lambda x,center=center,sharp=sharp:1.0/(1.0+math.exp(-sharp*(x-center))))

for phase in (1,2):
    for center in (-0.25,0.0,0.20,1/3,0.50,2/3,0.80,1.0,1.25):
        cid+=1
        add(f"P{cid:04d}","hard_step",phase,"step",
            {"center":center},
            lambda x,center=center:0.0 if x<center else 1.0)

for phase in (1,2):
    for center in (-0.2,0.15,0.45,0.75,1.1):
        for left,right in ((0.12,0.85),(0.65,-0.25),(-0.45,0.55)):
            cid+=1
            add(f"P{cid:04d}","kink",phase,"piecewise_linear",
                {"center":center,"left":left,"right":right},
                lambda x,center=center,left=left,right=right:
                    left*(x-center) if x<center else right*(x-center))

for phase in (1,2):
    for center in (-0.1,0.2,0.5,0.8,1.1):
        for sigma in (0.10,0.18,0.30):
            cid+=1
            add(f"P{cid:04d}","thin_feature",phase,"gaussian",
                {"center":center,"sigma":sigma},
                lambda x,center=center,sigma=sigma:
                    math.exp(-0.5*((x-center)/sigma)**2))

rng=random.Random(SEED)
for phase in (1,2):
    for _ in range(64):
        amps=[rng.uniform(-0.25,0.25) for _ in range(3)]
        freqs=[rng.uniform(0.05,0.48) for _ in range(3)]
        phis=[rng.uniform(-math.pi,math.pi) for _ in range(3)]
        cid+=1
        params={"amps":amps,"freqs":freqs,"phis":phis}
        def f(x,amps=amps,freqs=freqs,phis=phis):
            return 0.5+sum(a*math.sin(2*math.pi*fr*x+ph)
                           for a,fr,ph in zip(amps,freqs,phis))
        add(f"P{cid:04d}","mixed_frequency",phase,"sum_sines",params,f)

fields=["case_id","category","phase","function","params","fm1","f0","f1","f2","truth"]
with OUT.open("w",newline="",encoding="utf-8") as f:
    w=csv.DictWriter(f,fieldnames=fields); w.writeheader(); w.writerows(rows)

meta={
    "corpus_id":"PTAR-CORE-PROBES-V1",
    "type":"DIRECTIONAL_ANALYTIC_REGRESSION",
    "purpose":"Regression/epsilon probes for the EDGE directional reconstruction core.",
    "historical_equivalence":False,
    "acceptance_corpus":False,
    "seed":SEED,
    "case_count":len(rows),
    "phase_values":["1/3","2/3"],
    "sha256_probes_csv":sha256(OUT),
    "notes":[
        "Does not replace EDGE/horse/naturel/RASTER historical corpora.",
        "Does not reconstruct EDGE v03 orientation/routing.",
        "Not sufficient for PTAR image-quality acceptance."
    ]
}
META.write_text(json.dumps(meta,indent=2)+"\n",encoding="utf-8")
print(f"{len(rows)} probes")
print(meta["sha256_probes_csv"])

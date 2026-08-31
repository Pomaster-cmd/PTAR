#!/usr/bin/env python3
import csv,hashlib,json,sys
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
C=ROOT/"corpus/current/PTAR_PERCEPTUAL_V1_A"

def sha256(path):
    h=hashlib.sha256()
    with open(path,"rb") as f:
        for c in iter(lambda:f.read(1024*1024),b""): h.update(c)
    return h.hexdigest()

checks=[]
def check(name,cond,detail=""):
    checks.append((name,bool(cond),detail))
    if not cond: raise AssertionError(name+": "+detail)

meta=json.loads((C/"metadata.json").read_text(encoding="utf-8"))
check("historical equivalence false",meta["historical_equivalence"] is False)
check("scale exactly 1.5",meta["scale"]==1.5)

with (C/"SOURCE_REGISTRY.csv").open(newline="",encoding="utf-8") as f:
    sources=list(csv.DictReader(f))
with (C/"CORPUS_MANIFEST.csv").open(newline="",encoding="utf-8") as f:
    cases=list(csv.DictReader(f))

check("14 source snapshots",len(sources)==14,f"got {len(sources)}")
check("42 permanent cases",len(cases)==42,f"got {len(cases)}")
check("all source licenses explicit",all(r["license"] for r in sources))

for r in sources:
    p=C/r["snapshot_path"]
    check("source exists "+r["source_id"],p.is_file())
    check("source hash "+r["source_id"],sha256(p)==r["sha256_snapshot"])

for r in cases:
    ref=C/r["reference_path"]; inp=C/r["input_path"]
    check("ref exists "+r["case_id"],ref.is_file())
    check("input exists "+r["case_id"],inp.is_file())
    check("ref hash "+r["case_id"],sha256(ref)==r["sha256_reference"])
    check("input hash "+r["case_id"],sha256(inp)==r["sha256_input"])
    with Image.open(ref) as im: check("ref size "+r["case_id"],im.size==(288,288))
    with Image.open(inp) as im: check("input size "+r["case_id"],im.size==(192,192))

print(f"{sum(x[1] for x in checks)}/{len(checks)} PASS")

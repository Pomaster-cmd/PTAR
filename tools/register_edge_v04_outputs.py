#!/usr/bin/env python3
import argparse, csv, hashlib, json
from pathlib import Path
from datetime import datetime, timezone

def sha256(path):
    h=hashlib.sha256()
    with open(path,"rb") as f:
        for c in iter(lambda:f.read(1024*1024),b""): h.update(c)
    return h.hexdigest()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--project-root", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--execution-id", required=True)
    ap.add_argument("--variant", required=True)
    ap.add_argument("--output-dir", required=True)
    ap.add_argument("--corpus-manifest",
                    default="corpus/current/PTAR_PERCEPTUAL_V1_A/CORPUS_MANIFEST.csv")
    args=ap.parse_args()

    root=Path(args.project_root).resolve()
    outdir=Path(args.output_dir).resolve()
    corpus_manifest=(root/args.corpus_manifest).resolve()
    regdir=root/"benchmarks/results/current/EDGE_V04_REGISTERED"
    registry=regdir/"EXECUTION_REGISTRY.csv"
    run_dir=regdir/args.execution_id

    if run_dir.exists():
        raise SystemExit("REFUSE: execution directory already exists")
    if not outdir.is_dir():
        raise SystemExit("missing output directory")
    if not corpus_manifest.is_file():
        raise SystemExit("missing corpus manifest")

    with corpus_manifest.open(newline="",encoding="utf-8") as f:
        cases=list(csv.DictReader(f))
    if not cases:
        raise SystemExit("empty corpus manifest")

    rows=[]
    for r in cases:
        cid=r["case_id"]
        candidates=[
            outdir/f"{cid}.png",
            outdir/f"{cid}__{args.variant}.png",
            outdir/f"{cid}_{args.variant}.png",
        ]
        found=[p for p in candidates if p.is_file()]
        if len(found)!=1:
            raise SystemExit(f"{cid}: expected exactly one output, found {len(found)}")
        p=found[0]
        rows.append({
            "case_id":cid,
            "series":r["family"],
            "reference_path":r["reference_path"],
            "variant":args.variant,
            "output_path":str(p),
            "output_sha256":sha256(p),
        })

    old=[]
    if registry.exists():
        with registry.open(newline="",encoding="utf-8") as f:
            old=list(csv.DictReader(f))
        if any(x["execution_id"]==args.execution_id for x in old):
            raise SystemExit("REFUSE: execution ID already registered")

    run_dir.mkdir(parents=True)
    manifest=run_dir/"BENCHMARK_MANIFEST.csv"
    with manifest.open("w",newline="",encoding="utf-8") as f:
        fields=["case_id","series","reference_path","variant","output_path","output_sha256"]
        w=csv.DictWriter(f,fieldnames=fields)
        w.writeheader(); w.writerows(rows)

    rec={
        "execution_id":args.execution_id,
        "variant":args.variant,
        "corpus_id":"PTAR-PERCEPTUAL-V1-A",
        "case_count":str(len(rows)),
        "registered_utc":datetime.now(timezone.utc).isoformat(),
        "source_output_dir":str(outdir),
        "manifest_sha256":sha256(manifest),
        "status":"REGISTERED_NOT_YET_BENCHMARKED"
    }
    allrows=old+[rec]
    with registry.open("w",newline="",encoding="utf-8") as f:
        fields=list(rec.keys())
        w=csv.DictWriter(f,fieldnames=fields)
        w.writeheader(); w.writerows(allrows)

    (run_dir/"RUN_METADATA.json").write_text(json.dumps(rec,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(rec,indent=2))

if __name__=="__main__":
    main()

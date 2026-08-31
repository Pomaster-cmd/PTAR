#!/usr/bin/env python3
import csv,math,subprocess,sys,tempfile
from pathlib import Path
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"src/reference"))
from ptar_edge_v04_reference import edge_v04,MODE_BASELINE,MODE_SW2,MODE_SW3

checks=[]
def check(name,cond,detail=""):
    checks.append((name,bool(cond),detail))
    if not cond: raise AssertionError(name+": "+detail)

fm1=(0.0,0.1,0.2,1.0); f0=(0.2,0.3,0.4,1.0)
f1=(0.8,0.7,0.6,1.0); f2=(1.0,0.9,0.8,1.0)
lum=(0.1,0.3,0.7,0.9); base=(0.45,0.50,0.55,1.0); eps=1e-8

for phase in (1,2):
    b=edge_v04(MODE_BASELINE,base,0.37,fm1,f0,f1,f2,*lum,phase,eps)
    check(f"phase {phase} baseline passthrough",b==base)
    s2=edge_v04(MODE_SW2,base,0.37,fm1,f0,f1,f2,*lum,phase,eps)
    s30=edge_v04(MODE_SW3,base,0.0,fm1,f0,f1,f2,*lum,phase,eps)
    s31=edge_v04(MODE_SW3,base,1.0,fm1,f0,f1,f2,*lum,phase,eps)
    check(f"phase {phase} SW3 conf0",max(abs(s30[i]-base[i]) for i in range(4))<1e-15)
    check(f"phase {phase} SW3 conf1",max(abs(s31[i]-s2[i]) for i in range(4))<1e-15)

hlsl=(ROOT/"src/hlsl/ptar_edge_v04_step_weno_adapter.hlsli").read_text(encoding="utf-8")
u=hlsl.upper()
check("compile-time selector present","#IF PTAR_EDGE_V04_STEP_MODE" in u)
check("no texture access",all(x not in u for x in ("TEXTURE2D","SAMPLERSTATE","RWTEXTURE")))
check("no NIS token","NIS" not in u)
check("both phases exposed","PTAREDGEV04PHASE13" in u and "PTAREDGEV04PHASE23" in u)

with tempfile.TemporaryDirectory() as td:
    td=Path(td)
    ref=np.zeros((32,32,3),dtype=np.uint8)
    ref[:,:,0]=np.arange(32,dtype=np.uint8)[None,:]*8
    ref[:,:,1]=np.arange(32,dtype=np.uint8)[:,None]*8
    ref[:,:,2]=127
    Image.fromarray(ref).save(td/"ref.png")
    Image.fromarray(ref).save(td/"same.png")
    alt=ref.copy(); alt[16,16,0]=255
    Image.fromarray(alt).save(td/"alt.png")
    with (td/"manifest.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.writer(f)
        w.writerow(["case_id","series","reference_path","variant","output_path"])
        w.writerow(["T1","selftest","ref.png","same","same.png"])
        w.writerow(["T2","selftest","ref.png","altered","alt.png"])
    cp=subprocess.run([sys.executable,str(ROOT/"tools/ptar_image_benchmark_v1.py"),
                       str(td/"manifest.csv"),"--out",str(td/"results")],
                      capture_output=True,text=True)
    check("benchmark harness exits 0",cp.returncode==0,cp.stderr)
    with (td/"results/per_case.csv").open(newline="",encoding="utf-8") as f:
        rr=list(csv.DictReader(f))
    same=next(x for x in rr if x["variant"]=="same")
    altered=next(x for x in rr if x["variant"]=="altered")
    check("identical SSIM=1",abs(float(same["ssim"])-1.0)<1e-12)
    check("identical PSNR=inf",math.isinf(float(same["psnr_db"])))
    check("altered SSIM<1",float(altered["ssim"])<1.0)
    check("altered PSNR finite",math.isfinite(float(altered["psnr_db"])))

print(f"{sum(x[1] for x in checks)}/{len(checks)} PASS")
for name,passed,detail in checks:
    print(("PASS" if passed else "FAIL")+" - "+name+((" | "+detail) if detail else ""))

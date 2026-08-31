#!/usr/bin/env python3
import subprocess,sys,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
clang=shutil.which('clang')
if not clang: raise SystemExit('clang not found')
probe=ROOT/'build/windows/ptar_edge_v04_clang_compute_probe.hlsl'
shim=ROOT/'src/hlsl/clang_diagnostic'
outdir=ROOT/'validation/clang_hlsl_diagnostic_ir'; outdir.mkdir(exist_ok=True)
results=[]
for mode in range(4):
    out=outdir/f'mode{mode}.ll'
    cmd=[clang,'--target=dxil-pc-shadermodel6.0-compute','-x','hlsl','-I'+str(shim),
         '-DPTAR_EDGE_V04_STEP_MODE='+str(mode),'-S','-emit-llvm',str(probe),'-o',str(out)]
    cp=subprocess.run(cmd,capture_output=True,text=True)
    results.append({'mode':mode,'returncode':cp.returncode,'stderr':cp.stderr.strip(),'output':str(out)})
    if cp.returncode!=0: raise SystemExit(json.dumps(results,indent=2))
print(json.dumps({'diagnostic_only':True,'not_fxc_sm5_validation':True,'results':results},indent=2))

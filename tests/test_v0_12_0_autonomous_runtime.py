#!/usr/bin/env python3
from pathlib import Path
import json,hashlib,re

ROOT=Path(__file__).resolve().parents[1]
B=ROOT/"runtime_autonomous/PTAR_NG_MOE_V01_WIN81_X64"
src=(B/"source/ptar_autonomous_validation.cpp").read_text(encoding="utf-8")
build=(B/"BUILD_RUNTIME_ONCE_IN_DEV_ENV.bat").read_text(encoding="utf-8")
run=(B/"RUN_AUTONOMOUS_ONLY.bat").read_text(encoding="utf-8")
prov=json.loads((B/"SHADER_PROVENANCE.json").read_text(encoding="utf-8"))

checks=[]
def ck(name,cond):
    checks.append((name,bool(cond)))
    if not cond: raise AssertionError(name)

ck("three validated CSOs",len(list((B/"shaders").glob("*.cso")))==3)
ck("42 bundle inputs",len(list((B/"corpus/input_lr").glob("*_LR.png")))==42)
ck("42 bundle expected",len(list((B/"corpus/expected_moe_float32").glob("*.png")))==42)
ck("no HLSL shipped in autonomous bundle",len(list(B.rglob("*.hlsl")))==0)

for name,entry in prov["shaders"].items():
    p=B/"shaders"/name
    h=hashlib.sha256(p.read_bytes()).hexdigest()
    ck("hash "+name,h==entry["sha256"])
    ck("DXBC "+name,p.read_bytes()[:4]==b"DXBC")

ck("source no d3dcompiler include","#include <d3dcompiler.h>" not in src)
ck("source no D3DCompileFromFile","D3DCompileFromFile" not in src)
ck("source no D3DDisassemble","D3DDisassemble" not in src)
ck("source no ID3DBlob","ID3DBlob" not in src)
ck("source loads precompiled shaders","LoadValidatedPrecompiledShaders" in src)
ck("source verifies SHA256","VerifyFileSha256" in src and "BCrypt" in src)
ck("source verifies DXBC magic",'memcmp(&m[0],"DXBC",4)' in src)

ck("builder static CRT","/MT" in build)
ck("builder Win81 subsystem","/SUBSYSTEM:CONSOLE,6.03" in build)
compile_start=build.lower().find("cl.exe /nologo")
compile_end=build.lower().find("if errorlevel",compile_start)
compile_block=build.lower()[compile_start:compile_end]
ck("builder no d3dcompiler link","d3dcompiler.lib" not in compile_block)
ck("builder audits imports","dumpbin /dependents" in build.lower())
ck("builder rejects D3DCompiler import","D3DCOMPILER" in build)
ck("builder refuses overwrite","Aucun ecrasement automatique" in build)
ck("builder no copy /Y","copy /Y" not in build)

ck("runtime sanitizes PATH","set \"PATH=%SystemRoot%\\System32;%SystemRoot%;%SystemRoot%\\System32\\Wbem\"" in run)
ck("runtime checks cl hidden","cl_visible=" in run.lower())
ck("runtime checks fxc hidden","fxc_visible=" in run.lower())
ck("runtime does not invoke vcvars","vcvars" not in run.lower())
ck("runtime does not invoke compiler","cl.exe /" not in run.lower() and "fxc.exe /" not in run.lower())
ck("runtime parity gate","parity_failed_cases=0" in run)
ck("runtime PNG gate","gpu_png_persistence=PASS_42_NONZERO" in run)

# No destructive operations in runtime/preparation scripts.
scripts=[
    ROOT/"PREPARE_AUTONOMOUS_RUNTIME.bat",
    ROOT/"RUN_PTAR_AUTO.bat",
    B/"BUILD_RUNTIME_ONCE_IN_DEV_ENV.bat",
    B/"RUN_AUTONOMOUS_ONLY.bat",
    B/"VERIFY_AFTER_TOOLCHAIN_REMOVAL.bat",
]
for p in scripts:
    t=p.read_text(encoding="utf-8")
    ck("non-destructive "+p.name,
       "Remove-Item" not in t and not re.search(r'(?im)^\s*(del|erase|rd|rmdir|format)\b',t))

print(f"{sum(1 for _,v in checks if v)}/{len(checks)} PASS")
for n,v in checks:
    print(("PASS" if v else "FAIL")+" - "+n)

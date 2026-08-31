#!/usr/bin/env python3
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
cpp=(ROOT/"runtime_autonomous/PTAR_NG_MOE_V01_WIN81_X64/source/ptar_autonomous_validation.cpp").read_text(encoding="utf-8")

checks=[]
def ck(name,cond):
    checks.append((name,bool(cond)))
    if not cond:
        raise AssertionError(name)

ck("bundleRoot declared","std::wstring bundleRoot,outDir;" in cpp)
ck("bundleRoot parsed",'if(a==L"--bundle-root"' in cpp)
ck("shader directory uses bundleRoot",'JoinPath(bundleRoot,L"shaders")' in cpp)
ck("input corpus uses autonomous bundle",'JoinPath(bundleRoot,L"corpus\\\\input_lr")' in cpp)
ck("expected corpus uses autonomous bundle",'JoinPath(bundleRoot,L"corpus\\\\expected_moe_float32")' in cpp)

# The old C++ identifier root must not remain. Ignore the CLI literal --bundle-root.
without_literals=cpp.replace("--bundle-root","")
ck("no standalone old root identifier",re.search(r"\broot\b",without_literals) is None)

ck("no old project corpus path","corpus\\\\current\\\\PTAR_PERCEPTUAL_V1_B_GRID" not in cpp)
ck("no old project benchmark path","benchmarks\\\\results\\\\current\\\\MOE_NG_V01_SF5" not in cpp)

# Autonomous runtime constraints remain unchanged.
ck("no D3DCompileFromFile","D3DCompileFromFile" not in cpp)
ck("no D3DDisassemble","D3DDisassemble" not in cpp)
ck("no d3dcompiler include","#include <d3dcompiler.h>" not in cpp)
ck("precompiled shader loader retained","LoadValidatedPrecompiledShaders" in cpp)
ck("SHA256 validation retained","VerifyFileSha256" in cpp)
ck("42 case gate retained","ids.size()!=42u" in cpp)

print(f"{sum(v for _,v in checks)}/{len(checks)} PASS")
for n,v in checks:
    print(("PASS" if v else "FAIL")+" - "+n)

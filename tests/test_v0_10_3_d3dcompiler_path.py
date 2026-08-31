#!/usr/bin/env python3
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
cpp=(ROOT/"hardware_validation/ptar_k185_hw_validation.cpp").read_text(encoding="utf-8")
build=(ROOT/"build/windows/BUILD_AND_RUN_K185_HARDWARE_VALIDATION.bat").read_text(encoding="utf-8")
ps=(ROOT/"automation/windows/PTAR_AutoSetupAndValidate.ps1").read_text(encoding="utf-8")

checks=[]
def ck(name,cond):
    checks.append((name,bool(cond)))
    if not cond: raise AssertionError(name)

ck("D3DCompiler header","#include <d3dcompiler.h>" in cpp)
ck("D3DCompileFromFile","D3DCompileFromFile" in cpp)
ck("SM5 VS target",'"vs_5_0"' in cpp)
ck("SM5 PS target",'"ps_5_0"' in cpp)
ck("strict compile","D3DCOMPILE_ENABLE_STRICTNESS" in cpp)
ck("warnings as errors","D3DCOMPILE_WARNINGS_ARE_ERRORS" in cpp)
ck("O3 compile","D3DCOMPILE_OPTIMIZATION_LEVEL3" in cpp)
ck("D3DDisassemble","D3DDisassemble" in cpp)
ck("gather4 audit",'CountToken(text,"gather4")' in cpp)
ck("sample_l audit",'CountToken(text,"sample_l")' in cpp)
ck("UAV audit",'CountToken(text,"dcl_uav")' in cpp)
ck("binary artifacts preserved","ptar_k185_ps.cso" in cpp and "ptar_k185_ps.asm" in cpp)

ck("hardware BAT has no FXC requirement","where fxc.exe" not in build)
ck("hardware BAT links d3dcompiler","d3dcompiler.lib" in build)
ck("hardware BAT passes HLSL sources","--vs-src" in build and "--ps-src" in build and "--bypass-src" in build)
ck("hardware BAT still Win8.1","/SUBSYSTEM:CONSOLE,6.03" in build and "/D_WIN32_WINNT=0x0603" in build)

ck("automation says FXC optional","FXC is optional" in ps or "FXC optional" in ps)
ck("automation does not install SDK solely for missing FXC","if ($fxc -eq $null) {\n            Install-Windows81Sdk" not in ps)
ck("verify-only requires MSVC","Verify-only result: MSVC" in ps)
ck("no destructive cleanup","Remove-Item" not in ps and not re.search(r'(?im)^\s*(del|rd|rmdir|format)\b',ps))

print(f"{sum(p for _,p in checks)}/{len(checks)} PASS")
for n,p in checks:
    print(("PASS" if p else "FAIL")+" - "+n)

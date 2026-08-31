#!/usr/bin/env python3
from pathlib import Path
import subprocess, sys, tempfile, textwrap

ROOT=Path(__file__).resolve().parents[1]
cpp=(ROOT/"hardware_validation/ptar_k185_hw_validation.cpp").read_text(encoding="utf-8")
ps=(ROOT/"automation/windows/PTAR_AutoSetupAndValidate.ps1").read_text(encoding="utf-8")

checks=[]
def ck(name,cond,detail=""):
    checks.append((name,bool(cond),detail))
    if not cond:
        raise AssertionError(name + (": "+detail if detail else ""))

lean=cpp.find("#define WIN32_LEAN_AND_MEAN")
nomin=cpp.find("#define NOMINMAX")
win=cpp.find("#include <windows.h>")
ck("NOMINMAX exists","#define NOMINMAX" in cpp)
ck("NOMINMAX precedes windows.h",0 <= lean < nomin < win)
ck("macro-safe std::max call","globalMax=(std::max)(globalMax,maxLsb);" in cpp)
ck("unsafe exact std::max call removed","globalMax=std::max(globalMax,maxLsb);" not in cpp)

# Portable reproduction of the Win32 macro collision.
# This validates the exact macro-safe syntax even when a hostile max macro
# is deliberately defined.
source=r"""
#include <algorithm>
#define max(a,b) WINDOWS_MAX_MACRO_SHOULD_NOT_EXPAND(a,b)
int main()
{
    unsigned a=2,b=7;
    unsigned c=(std::max)(a,b);
    return c==7 ? 0 : 1;
}
"""
with tempfile.TemporaryDirectory() as td:
    td=Path(td)
    src=td/"macro_test.cpp"
    exe=td/"macro_test"
    src.write_text(source,encoding="utf-8")
    candidates=[
        ["clang++","-std=c++11",str(src),"-o",str(exe)],
        ["g++","-std=c++11",str(src),"-o",str(exe)],
    ]
    compiled=False
    last=""
    for cmd in candidates:
        try:
            cp=subprocess.run(cmd,capture_output=True,text=True)
        except FileNotFoundError:
            continue
        last=cp.stderr
        if cp.returncode==0:
            compiled=True
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            ck("macro-safe syntax executes",run.returncode==0,run.stderr)
            break
    ck("portable macro-collision compile",compiled,last)

ck("FXC preflight path can be preserved",
   'if ($fxc -eq $null -or -not (Test-Path -LiteralPath $fxc))' in ps)
ck("FXC optional path log is explicit","FXC available (optional)" in ps)
ck("D3DCompiler fallback retained","D3DCompiler API path" in ps)

print(f"{sum(1 for _,p,_ in checks if p)}/{len(checks)} PASS")
for name,passed,detail in checks:
    print(("PASS" if passed else "FAIL")+" - "+name+((" | "+detail) if detail else ""))

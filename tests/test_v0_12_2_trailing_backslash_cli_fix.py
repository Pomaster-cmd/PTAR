#!/usr/bin/env python3
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
B=ROOT/"runtime_autonomous/PTAR_NG_MOE_V01_WIN81_X64"
run=(B/"RUN_AUTONOMOUS_ONLY.bat").read_text(encoding="utf-8")
cpp=(B/"source/ptar_autonomous_validation.cpp").read_text(encoding="utf-8")

checks=[]
def ck(name,cond):
    checks.append((name,bool(cond)))
    if not cond: raise AssertionError(name)

ck("old dp0 trailing assignment removed",'set "BUNDLE=%~dp0"' not in run)
ck("bundle canonicalized with dot", 'for %%I in ("%~dp0.") do set "BUNDLE=%%~fI"' in run)
ck("bundle root quoted", '--bundle-root "%BUNDLE%"' in run)
ck("out quoted", '--out "%RUN%\\results"' in run)
ck("argument diagnostics present",'[ARGS] bundle-root=' in run and '[ARGS] out=' in run)

# The canonicalization form resolves the directory itself rather than carrying
# the %~dp0 trailing slash into a quoted CRT argument.
ck("no direct quoted dp0 argument", '--bundle-root "%~dp0"' not in run)

# Parser contract retained.
ck("parser expects bundle-root",'if(a==L"--bundle-root" && i+1<argc)' in cpp)
ck("parser expects out",'else if(a==L"--out" && i+1<argc)' in cpp)
ck("usage requires both", 'if(bundleRoot.empty()||outDir.empty()) return Usage();' in cpp)

# Runtime dependency constraints unchanged.
ck("no D3DCompileFromFile","D3DCompileFromFile" not in cpp)
ck("precompiled loader retained","LoadValidatedPrecompiledShaders" in cpp)
ck("PATH sanitized",'set "PATH=%SystemRoot%\\System32;%SystemRoot%;%SystemRoot%\\System32\\Wbem"' in run)

print(f"{sum(v for _,v in checks)}/{len(checks)} PASS")
for n,v in checks:
    print(("PASS" if v else "FAIL")+" - "+n)

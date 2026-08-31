#!/usr/bin/env python3
from pathlib import Path
import re,hashlib,json

ROOT=Path(__file__).resolve().parents[1]
B=ROOT/"runtime_autonomous/PTAR_NG_MOE_V01_WIN81_X64"

run=(B/"RUN_AUTONOMOUS_ONLY.bat").read_text(encoding="utf-8")
build=(B/"BUILD_RUNTIME_ONCE_IN_DEV_ENV.bat").read_text(encoding="utf-8")
prepare=(ROOT/"PREPARE_AUTONOMOUS_RUNTIME.bat").read_text(encoding="utf-8")
rootrun=(ROOT/"RUN_PTAR_AUTO.bat").read_text(encoding="utf-8")
verify=(B/"VERIFY_AFTER_TOOLCHAIN_REMOVAL.bat").read_text(encoding="utf-8")
cpp=(B/"source/ptar_autonomous_validation.cpp").read_text(encoding="utf-8")
prov=json.loads((B/"SHADER_PROVENANCE.json").read_text(encoding="utf-8"))

checks=[]
def ck(name,cond):
    checks.append((name,bool(cond)))
    if not cond: raise AssertionError(name)

# Path invariant.
ck("run bundle canonical",'for %%I in ("%~dp0.") do set "BUNDLE=%%~fI"' in run)
ck("build bundle canonical",'for %%I in ("%~dp0.") do set "BUNDLE=%%~fI"' in build)
ck("prepare root canonical",'for %%I in ("%~dp0.") do set "ROOT=%%~fI"' in prepare)
ck("verify bundle canonical",'for %%I in ("%~dp0.") do set "BUNDLE=%%~fI"' in verify)

combined=run+build+prepare+verify
for bad in [
    "%BUNDLE%bin","%BUNDLE%shaders","%BUNDLE%corpus","%BUNDLE%runs",
    "%BUNDLE%source","%BUNDLE%include","%BUNDLE%build_evidence",
    "%ROOT%runtime_autonomous",
]:
    ck("no missing separator "+bad,bad not in combined)

ck("exe explicit separator",'set "EXE=%BUNDLE%\\bin\\ptar_autonomous_validation.exe"' in run)
ck("shader dir separator",'set "SHADER_DIR=%BUNDLE%\\shaders"' in run)
ck("runs dir separator",'set "RUNS_DIR=%BUNDLE%\\runs"' in run)
ck("prepare bundle separator",'set "BUNDLE=%ROOT%\\runtime_autonomous\\PTAR_NG_MOE_V01_WIN81_X64"' in prepare)

# Runtime pre/post flight.
ck("42 input preflight",'if not "%INPUT_COUNT%"=="42"' in run)
ck("42 expected preflight",'if not "%EXPECTED_COUNT%"=="42"' in run)
ck("42 GPU PNG postflight",'if not "%GPU_PNG_COUNT%"=="42"' in run)
ck("512 pair postflight",'paired_samples=512' in run)
ck("cl hidden",'CL_VISIBLE=NO' in run)
ck("fxc hidden",'FXC_VISIBLE=NO' in run)
ck("precompiled mode gate",'runtime_shader_mode=PRECOMPILED_DXBC_ONLY' in run)

# Build command compatibility.
ck("Win81 target",'/D_WIN32_WINNT=0x0603' in build and '/DWINVER=0x0603' in build)
ck("Win81 subsystem",'/SUBSYSTEM:CONSOLE,6.03' in build)
ck("static CRT",'/MT' in build)
ck("obj staged",'/Fo:"%STAGE%\\ptar_autonomous_validation.obj"' in build)

cl_start=build.find("cl.exe /nologo")
cl_end=build.find("if errorlevel 1",cl_start)
cl_block=build[cl_start:cl_end].lower()
ck("link command has no d3dcompiler","d3dcompiler.lib" not in cl_block)
ck("base Win81 libraries only",
   all(x in cl_block for x in ["d3d11.lib","dxgi.lib","windowscodecs.lib","ole32.lib","bcrypt.lib"]))

ck("import reject D3DCompiler",'D3DCOMPILER' in build)
ck("import reject VCRUNTIME",'VCRUNTIME' in build)
ck("import reject MSVCP",'MSVCP' in build)
ck("import reject UCRTBASE",'UCRTBASE' in build)
ck("import reject API-MS-WIN-CRT",'API-MS-WIN-CRT' in build)

# Native source Win8.1 family audit.
ck("no runtime compiler","D3DCompileFromFile" not in cpp and "D3DDisassemble" not in cpp)
ck("no D3D12","D3D12" not in cpp and "d3d12" not in cpp.lower())
ck("no CreateDXGIFactory2","CreateDXGIFactory2" not in cpp)
ck("no dxgi1_2+ header",not re.search(r'#include\\s*<dxgi1_[2-9]\\.h>',cpp,re.I))
ck("no d3d11_1+ header",not re.search(r'#include\\s*<d3d11_[1-9]\\.h>',cpp,re.I))
ck("base D3D11","D3D11CreateDevice" in cpp and "#include <d3d11.h>" in cpp)
ck("DXGI1.1","CreateDXGIFactory1" in cpp and "#include <dxgi.h>" in cpp)
ck("WIC","CLSID_WICImagingFactory" in cpp and "#include <wincodec.h>" in cpp)
ck("COM","CoInitializeEx" in cpp and "CoUninitialize" in cpp)
ck("BCrypt","BCryptOpenAlgorithmProvider" in cpp and "BCRYPT_SHA256_ALGORITHM" in cpp)
ck("GetTickCount64","GetTickCount64" in cpp)
ck("NOMINMAX","#define NOMINMAX" in cpp)

# Frozen CSOs.
ck("no HLSL in autonomous bundle",len(list(B.rglob("*.hlsl")))==0)
ck("3 CSOs",len(list((B/"shaders").glob("*.cso")))==3)
for name,entry in prov["shaders"].items():
    p=B/"shaders"/name
    ck("DXBC "+name,p.read_bytes()[:4]==b"DXBC")
    ck("SHA256 "+name,hashlib.sha256(p.read_bytes()).hexdigest()==entry["sha256"])

# Non-destructive.
for label,text in [("run",run),("build",build),("prepare",prepare),("root",rootrun),("verify",verify)]:
    ck("non destructive "+label,
       "Remove-Item" not in text and
       not re.search(r'(?im)^\\s*(del|erase|rd|rmdir|format)\\b',text))

freeze=(B/"build_evidence/HOST_SOURCE_FROZEN_SHA256.txt").read_text(encoding="utf-8")
actual=hashlib.sha256((B/"source/ptar_autonomous_validation.cpp").read_bytes()).hexdigest()
ck("source frozen from successful Win81 compile",freeze.startswith(actual+"  "))

print(f"{sum(v for _,v in checks)}/{len(checks)} PASS")
for n,v in checks:
    print(("PASS" if v else "FAIL")+" - "+n)

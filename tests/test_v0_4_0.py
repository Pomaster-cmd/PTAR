#!/usr/bin/env python3
import json, re, subprocess, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
checks=[]

def check(name, cond, detail=""):
    checks.append((name,bool(cond),detail))
    if not cond:
        raise AssertionError(name + (": "+detail if detail else ""))

adapter=(ROOT/"src/hlsl/ptar_edge_v04_step_weno_adapter.hlsli").read_text(encoding="utf-8")
weno=(ROOT/"src/hlsl/ptar_step_weno_lite.hlsli").read_text(encoding="utf-8")
probe=(ROOT/"build/windows/ptar_edge_v04_compile_probe.hlsl").read_text(encoding="utf-8")
bat=(ROOT/"build/windows/BUILD_EDGE_V04_PERMUTATIONS_SM5.bat").read_text(encoding="utf-8")
contract=(ROOT/"runtime_integration/PTAR_EDGE_V04_HOST_CONTRACT.md").read_text(encoding="utf-8")
variants=json.loads((ROOT/"runtime_integration/EDGE_V04_VARIANT_MANIFEST.json").read_text(encoding="utf-8"))

check("adapter mode 0", "PTAR_EDGE_V04_STEP_MODE == 0" in adapter)
check("adapter mode 1", "PTAR_EDGE_V04_STEP_MODE == 1" in adapter)
check("adapter mode 2", "PTAR_EDGE_V04_STEP_MODE == 2" in adapter)
check("adapter range guard", "#error PTAR_EDGE_V04_STEP_MODE" in adapter)
check("both fixed phases in adapter", "PTAREdgeV04Phase13" in adapter and "PTAREdgeV04Phase23" in adapter)
check("compile probe references both phases", "PTAREdgeV04Phase13" in probe and "PTAREdgeV04Phase23" in probe)
check("FXC matrix has four modes", "for %%M in (0 1 2 3)" in bat)
check("FXC target ps_5_0", "/T ps_5_0" in bat)
check("FXC strict build", "/Ges /WX" in bat)
check("non-destructive output collision guard", 'if exist "%OUT%"' in bat)
check("BAT no delete commands", not re.search(r'(?im)^\s*(del|erase|rd|rmdir)\b',bat))
check("WENO no resource declarations", all(x not in weno.upper() for x in ("TEXTURE2D","RWTEXTURE","SAMPLERSTATE")))
check("WENO no NIS token", "NIS" not in weno.upper())
check("four variant records", len(variants["variants"])==4)
check("no runtime mode switch", variants["runtime_mode_switch"] is False)
check("contract is not EDGE v03 reconstruction", "not an EDGE v03 reconstruction" in contract)
check("contract targets Shader Model 5.0", "Shader Model 5.0" in contract)

cp=subprocess.run([sys.executable,"-m","py_compile",str(ROOT/"tools/register_edge_v04_outputs.py")],
                  capture_output=True,text=True)
check("registration tool syntax", cp.returncode==0, cp.stderr)

print(f"{sum(x[1] for x in checks)}/{len(checks)} PASS")
for name,passed,detail in checks:
    print(("PASS" if passed else "FAIL")+" - "+name+((" | "+detail) if detail else ""))

#!/usr/bin/env python3
"""Prepare an isolated PTAR-NG MoE v02 LAB07 autonomous runtime bundle.

The frozen v01 bundle is used as scaffolding only.  This tool never modifies it
in place: the output directory must not already exist.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
from pathlib import Path

from moe_ng_v02_runtime_reference import load_rgb, reconstruct_x15, save_rgb8

V01_HOST_SHA = "6ce901a529e7a6697cf2cf8cfbd5fe1110db6d573be57f7c958d14e79e50088b"
V01_MOE_CSO_SHA = "bb531ad6f797d2db4a362c82673067e39b7aa3a8fbb0ea19f786a053556a2279"
VS_CSO_SHA = "6328bbd87aac73b07d6112de593f781b2769381aae65fa9c2bcfa06fdc68585c"
K185_CSO_SHA = "6bd926e85f21dd08788ff9d189c472800a5e0eb726091158ed1ecde3d06d8c16"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def sha256_text_lf(path: Path) -> str:
    # Git for Windows can materialize CRLF even when the repository blob and
    # frozen manifest were hashed with LF.  Normalize text only; binary CSO
    # checks remain byte-exact.
    data = path.read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha256(data).hexdigest()


def require_sha(path: Path, expected: str, *, normalize_text_lf: bool = False) -> None:
    got = sha256_text_lf(path) if normalize_text_lf else sha256(path)
    if got != expected:
        raise RuntimeError(f"SHA mismatch {path}: expected {expected}, got {got}")


def patch_host(text: str, v02_hash: str) -> str:
    replacements = {
        "// PTAR v0.12.0 autonomous validation host": "// PTAR-NG MoE v02 LAB07 autonomous validation host",
        V01_MOE_CSO_SHA: v02_hash,
        'L"ptar_moe_ng_v01_sf5_ps.cso"': 'L"ptar_moe_ng_v02_mc_native_detail_ps.cso"',
        '"ptar_moe_ng_v01_sf5_ps.cso"': '"ptar_moe_ng_v02_mc_native_detail_ps.cso"',
        'L"corpus\\\\expected_moe_float32"': 'L"corpus\\\\expected_moe_v02_float32"',
        "PTAR-NG MoE v01 SF5 AUTONOMOUS RUNTIME VALIDATION": "PTAR-NG MoE v02 LAB07 AUTONOMOUS RUNTIME VALIDATION",
        "PTAR_NG_MOE_V01_SF5": "PTAR_NG_MOE_V02_LAB07_MC",
        "MoE parity": "MoE v02 parity",
        "42/42 MoE GPU": "42/42 MoE v02 GPU",
    }
    for old, new in replacements.items():
        if old not in text:
            raise RuntimeError(f"expected host token not found: {old}")
        text = text.replace(old, new)
    if V01_MOE_CSO_SHA in text or "ptar_moe_ng_v01_sf5_ps.cso" in text:
        raise RuntimeError("v01 shader token remains in patched host")
    return text


def generate_expected(root: Path, out_dir: Path) -> int:
    corpus = root / "corpus/current/PTAR_PERCEPTUAL_V1_B_GRID"
    manifest = corpus / "CORPUS_MANIFEST.csv"
    rows = list(csv.DictReader(manifest.open(newline="", encoding="utf-8")))
    if len(rows) != 42:
        raise RuntimeError(f"expected 42 corpus rows, got {len(rows)}")
    for row in rows:
        lr = load_rgb(corpus / row["input_path"])
        out = reconstruct_x15(lr)
        save_rgb8(out_dir / f'{row["case_id"]}.png', out)
    return len(rows)


def write_build_scripts(bundle: Path) -> None:
    build = r'''@echo off
setlocal EnableExtensions
for %%I in ("%~dp0.") do set "BUNDLE=%%~fI"
set "SRC=%BUNDLE%\source\ptar_moe_ng_v02_autonomous_validation.cpp"
set "INC=%BUNDLE%\include"
set "BIN=%BUNDLE%\bin"
set "EVID=%BUNDLE%\build_evidence"
where cl.exe >nul 2>nul || exit /b 11
where dumpbin.exe >nul 2>nul || exit /b 12
if not exist "%BIN%" mkdir "%BIN%"
if not exist "%EVID%" mkdir "%EVID%"
if exist "%BIN%\ptar_moe_ng_v02_autonomous_validation.exe" exit /b 16
cl.exe /nologo /EHsc /O2 /MT /W4 /WX /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /DUNICODE /D_UNICODE /I "%INC%" "%SRC%" /Fe:"%BIN%\ptar_moe_ng_v02_autonomous_validation.exe" /link /SUBSYSTEM:CONSOLE,6.03 d3d11.lib dxgi.lib windowscodecs.lib ole32.lib bcrypt.lib
if errorlevel 1 exit /b 30
dumpbin /dependents "%BIN%\ptar_moe_ng_v02_autonomous_validation.exe" > "%EVID%\IMPORTS_VALIDATED.txt"
if errorlevel 1 exit /b 32
findstr /I /C:"D3DCOMPILER" /C:"VCRUNTIME" /C:"MSVCP" /C:"UCRTBASE" /C:"API-MS-WIN-CRT" "%EVID%\IMPORTS_VALIDATED.txt" >nul
if not errorlevel 1 exit /b 33
certutil -hashfile "%BIN%\ptar_moe_ng_v02_autonomous_validation.exe" SHA256 > "%EVID%\BINARY_SHA256.txt"
exit /b 0
'''
    run = r'''@echo off
setlocal EnableExtensions
for %%I in ("%~dp0.") do set "BUNDLE=%%~fI"
set "OUT=%BUNDLE%\results_%RANDOM%_%RANDOM%"
if not exist "%BUNDLE%\bin\ptar_moe_ng_v02_autonomous_validation.exe" exit /b 10
"%BUNDLE%\bin\ptar_moe_ng_v02_autonomous_validation.exe" --bundle-root "%BUNDLE%" --out "%OUT%"
set "RC=%ERRORLEVEL%"
echo [PTAR] Results: "%OUT%"
exit /b %RC%
'''
    verify = r'''@echo off
setlocal EnableExtensions
where cl.exe >nul 2>nul && echo [INFO] cl.exe still visible in PATH
where fxc.exe >nul 2>nul && echo [INFO] fxc.exe still visible in PATH
call "%~dp0RUN_V02_HARDWARE_VALIDATION.bat"
exit /b %ERRORLEVEL%
'''
    (bundle / "BUILD_HOST_ONCE_IN_DEV_ENV.bat").write_text(build, encoding="ascii")
    (bundle / "RUN_V02_HARDWARE_VALIDATION.bat").write_text(run, encoding="ascii")
    (bundle / "VERIFY_AFTER_TOOLCHAIN_REMOVAL.bat").write_text(verify, encoding="ascii")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--v02-cso", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    root = Path(args.root).resolve()
    v02_cso = Path(args.v02_cso).resolve()
    out = Path(args.out).resolve()
    if out.exists():
        raise SystemExit(f"refusing to overwrite existing output: {out}")

    v01 = root / "runtime_autonomous/PTAR_NG_MOE_V01_WIN81_X64"
    host = v01 / "source/ptar_autonomous_validation.cpp"
    vs = v01 / "shaders/ptar_vs.cso"
    k185 = v01 / "shaders/ptar_k185_control_ps.cso"
    old_moe = v01 / "shaders/ptar_moe_ng_v01_sf5_ps.cso"
    require_sha(host, V01_HOST_SHA, normalize_text_lf=True)
    require_sha(vs, VS_CSO_SHA)
    require_sha(k185, K185_CSO_SHA)
    require_sha(old_moe, V01_MOE_CSO_SHA)
    v02_hash = sha256(v02_cso)

    (out / "source").mkdir(parents=True)
    (out / "shaders").mkdir()
    (out / "include").mkdir()
    (out / "corpus/input_lr").mkdir(parents=True)
    (out / "corpus/expected_moe_v02_float32").mkdir()
    (out / "build_evidence").mkdir()

    shutil.copy2(vs, out / "shaders/ptar_vs.cso")
    shutil.copy2(k185, out / "shaders/ptar_k185_control_ps.cso")
    shutil.copy2(v02_cso, out / "shaders/ptar_moe_ng_v02_mc_native_detail_ps.cso")
    shutil.copy2(v01 / "include/PTARD3D11GpuTimerRing.h", out / "include/PTARD3D11GpuTimerRing.h")

    corpus = root / "corpus/current/PTAR_PERCEPTUAL_V1_B_GRID"
    rows = list(csv.DictReader((corpus / "CORPUS_MANIFEST.csv").open(newline="", encoding="utf-8")))
    for row in rows:
        shutil.copy2(corpus / row["input_path"], out / "corpus/input_lr" / Path(row["input_path"]).name)

    expected_count = generate_expected(root, out / "corpus/expected_moe_v02_float32")
    patched = patch_host(host.read_text(encoding="utf-8"), v02_hash)
    (out / "source/ptar_moe_ng_v02_autonomous_validation.cpp").write_text(patched, encoding="utf-8")
    write_build_scripts(out)

    provenance = {
        "variant": "PTAR_NG_MOE_V02_LAB07_MC",
        "lab_source_branch": "lab/moe-ng-v02-native-detail",
        "lab_selected_head": "ea291eac7484e1f368271bdbbbab18c27fd0b33f",
        "lab_selected_tree": "069736e3fcdbadb874106640696d8cf871220696",
        "v02_cso_sha256": v02_hash,
        "v01_host_scaffold_sha256": V01_HOST_SHA,
        "v01_runtime_modified_in_place": False,
        "expected_cases": expected_count,
        "hardware_executed": False,
    }
    (out / "SHADER_PROVENANCE.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(provenance, indent=2))


if __name__ == "__main__":
    main()

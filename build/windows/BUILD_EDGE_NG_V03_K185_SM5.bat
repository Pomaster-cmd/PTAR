@echo off
setlocal EnableExtensions

where fxc.exe >nul 2>nul
if errorlevel 1 (
  echo [FAIL] fxc.exe introuvable dans PATH.
  echo Activez un Windows SDK compatible puis relancez.
  exit /b 10
)

set "ROOT=%~dp0..\.."
set "PS=%ROOT%\src\hlsl\edge_ng_v03\ptar_edge_ng_v03_k185_ps.hlsl"
set "VS=%ROOT%\src\hlsl\edge_ng_v03\ptar_fullscreen_triangle_vs.hlsl"
set "OUTROOT=%~dp0compiled_edge_ng_v03"
set "OUT=%OUTROOT%\run_%RANDOM%_%RANDOM%"

if not exist "%OUTROOT%" mkdir "%OUTROOT%"
if exist "%OUT%" (
  echo [FAIL] Collision dossier sortie: "%OUT%"
  exit /b 11
)
mkdir "%OUT%"
if errorlevel 1 exit /b 12

fxc.exe /nologo /T vs_5_0 /E main /O3 /Ges /WX ^
  /Fo "%OUT%\ptar_fullscreen_triangle_vs.cso" ^
  /Fc "%OUT%\ptar_fullscreen_triangle_vs.asm" ^
  "%VS%"
if errorlevel 1 exit /b 20

fxc.exe /nologo /T ps_5_0 /E main /O3 /Ges /WX ^
  /Fo "%OUT%\ptar_edge_ng_v03_k185_ps.cso" ^
  /Fc "%OUT%\ptar_edge_ng_v03_k185_ps.asm" ^
  "%PS%"
if errorlevel 1 exit /b 21

echo [PASS] VS/PS SM5 compiles.
echo [INFO] Audit DXBC:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0AUDIT_EDGE_NG_V03_DXBC.ps1" ^
  "%OUT%\ptar_edge_ng_v03_k185_ps.asm"
if errorlevel 1 exit /b 30

echo [PASS] EDGE-NG v03 K185 compile + audit.
echo Sortie: "%OUT%"
exit /b 0

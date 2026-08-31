@echo off
setlocal EnableExtensions

where fxc.exe >nul 2>nul
if errorlevel 1 (
  echo [FAIL] fxc.exe introuvable dans PATH.
  echo Activez le Windows SDK contenant FXC puis relancez.
  exit /b 10
)

set "PROBE=%~dp0ptar_edge_v04_compile_probe.hlsl"
set "OUTROOT=%~dp0compiled"
set "OUT=%OUTROOT%\run_%RANDOM%_%RANDOM%"

if not exist "%OUTROOT%" mkdir "%OUTROOT%"
if exist "%OUT%" (
  echo [FAIL] Collision de dossier: "%OUT%"
  exit /b 11
)
mkdir "%OUT%"
if errorlevel 1 exit /b 12

for %%M in (0 1 2 3) do (
  echo [BUILD] PTAR_EDGE_V04_STEP_MODE=%%M
  fxc.exe /nologo /T ps_5_0 /E main /O3 /Ges /WX ^
    /D PTAR_EDGE_V04_STEP_MODE=%%M ^
    /Fo "%OUT%\edge_v04_mode%%M.cso" ^
    "%PROBE%"
  if errorlevel 1 (
    echo [FAIL] mode %%M
    echo Les fichiers deja produits sont conserves pour diagnostic.
    exit /b 20
  )
)

echo [PASS] 4/4 permutations compilees en ps_5_0.
echo Sortie: "%OUT%"
exit /b 0

@echo off
setlocal EnableExtensions

set "ROOT=%~dp0..\.."
set "OUTROOT=%~dp0hardware_builds"
set "OUT=%OUTROOT%\run_%RANDOM%_%RANDOM%"

where cl.exe >nul 2>nul
if errorlevel 1 (
  echo [FAIL] cl.exe introuvable dans PATH.
  exit /b 11
)

if not exist "%OUTROOT%" mkdir "%OUTROOT%"
if exist "%OUT%" (
  echo [FAIL] Collision dossier sortie: "%OUT%"
  exit /b 12
)
mkdir "%OUT%"
if errorlevel 1 exit /b 13

set "VS=%ROOT%\src\hlsl\edge_ng_v03\ptar_fullscreen_triangle_vs.hlsl"
set "PS=%ROOT%\src\hlsl\edge_ng_v03\ptar_edge_ng_v03_k185_ps.hlsl"
set "BP=%ROOT%\src\hlsl\edge_ng_v03\ptar_bilinear_control_ps.hlsl"
set "CPP=%ROOT%\hardware_validation\ptar_k185_hw_validation.cpp"

echo [BUILD] Native validator + in-process D3DCompiler HLSL path
cl.exe /nologo /EHsc /O2 /MT /W4 /WX ^
  /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /DUNICODE /D_UNICODE ^
  /I "%ROOT%\runtime_integration\d3d11" ^
  "%CPP%" /Fe:"%OUT%\ptar_k185_hw_validation.exe" ^
  /link /SUBSYSTEM:CONSOLE,6.03 d3d11.lib d3dcompiler.lib dxgi.lib windowscodecs.lib ole32.lib
if errorlevel 1 (
  echo [FAIL] C++/D3DCompiler build failed.
  exit /b 30
)

mkdir "%OUT%\results"
"%OUT%\ptar_k185_hw_validation.exe" ^
  --root "%ROOT%" ^
  --vs-src "%VS%" ^
  --ps-src "%PS%" ^
  --bypass-src "%BP%" ^
  --out "%OUT%\results"
set "RC=%ERRORLEVEL%"

echo.
echo [PTAR] Resultats: "%OUT%\results"
if not "%RC%"=="0" (
  echo [FAIL] validation materielle code=%RC%
  exit /b %RC%
)

echo [PASS] PTAR EDGE-NG v03 K185: D3DCompile + DXBC audit + parity + timing.
exit /b 0

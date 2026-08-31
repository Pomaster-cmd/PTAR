@echo off
setlocal EnableExtensions
where fxc.exe >nul 2>nul
if errorlevel 1 ( echo [FAIL] fxc.exe introuvable. & exit /b 10 )
set "SRC=%~dp0..\..\src\hlsl\edge_ng_v02\ptar_edge_ng_v02_g5_ps.hlsl"
set "OUTROOT=%~dp0compiled_edge_ng_v02"
set "OUT=%OUTROOT%\run_%RANDOM%_%RANDOM%"
if not exist "%OUTROOT%" mkdir "%OUTROOT%"
if exist "%OUT%" ( echo [FAIL] Collision sortie. & exit /b 11 )
mkdir "%OUT%"
fxc.exe /nologo /T ps_5_0 /E main /O3 /Ges /WX /Fo "%OUT%\ptar_edge_ng_v02_g5.cso" "%SRC%"
if errorlevel 1 exit /b 20
echo [PASS] EDGE-NG v02 G5 ps_5_0 compile.
echo Sortie: "%OUT%"
exit /b 0

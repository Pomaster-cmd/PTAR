@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR - MARQUEURS REAL / GENERATED
set "ROOT=%~dp0.."
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "D9TOOL=%ROOT%\BACKENDS\D3D9_X86\diag\set_vblank_diagnostics.ps1"
set "D11TOOL=%ROOT%\BACKENDS\D3D11_X64\diag\set_vblank_diagnostics.ps1"
set "BACKEND=D3D11"
set "TOOL=%D11TOOL%"
if exist "%ROOT%\d3d9.dll" if exist "%D9TOOL%" (
  set "BACKEND=D3D9"
  set "TOOL=%D9TOOL%"
)
if not exist "%TOOL%" (
  echo [ERREUR] Outil VBlankDiagnostics introuvable pour %BACKEND%.
  pause
  exit /b 2
)
set "ACT=%~1"
if /I "%ACT%"=="ON" goto ON
if /I "%ACT%"=="1" goto ON
if /I "%ACT%"=="OFF" goto OFF
if /I "%ACT%"=="0" goto OFF
if /I "%ACT%"=="STATUS" goto STATUS
if /I "%ACT%"=="?" goto HELP
if /I "%ACT%"=="/?" goto HELP
echo.
echo PTAR - CARRES REAL / GENERATED
echo  Backend detecte : %BACKEND%
echo  [1] Afficher les carres ^(VBlankDiagnostics=1^)
echo  [2] Masquer les carres ^(VBlankDiagnostics=0^)
echo  [3] Lire l'etat actuel
echo  [Q] Quitter
echo.
choice /C 123Q /N /M "Choix : "
if errorlevel 4 exit /b 0
if errorlevel 3 goto STATUS
if errorlevel 2 goto OFF
if errorlevel 1 goto ON
:ON
call :RUNTOOL 1
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" echo [INFO] Marqueurs visibles en permanence. Ctrl+F1 / RAWCOMPARE3 restent compatibles.
pause
exit /b %RC%
:OFF
call :RUNTOOL 0
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
  echo [INFO] FG et HUD/FPS restent actifs ; seuls les carres R/G sont masques.
  echo [INFO] Ctrl+F1 / RAWCOMPARE3 les activent temporairement pendant une capture puis restaurent OFF.
)
pause
exit /b %RC%
:STATUS
call :RUNTOOL -1
set "RC=%ERRORLEVEL%"
pause
exit /b %RC%
:HELP
echo Usage : %~nx0 [ON^|OFF^|STATUS]
pause
exit /b 0
:RUNTOOL
if /I "%BACKEND%"=="D3D9" (
  "%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%TOOL%" -GameRoot "%ROOT%" -Value %~1
) else (
  "%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%TOOL%" -Value %~1
)
exit /b %ERRORLEVEL%

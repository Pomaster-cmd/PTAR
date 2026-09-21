@echo off
setlocal EnableExtensions
set "HERE=%~dp0"
set "TARGET=%~1"

if not defined TARGET (
  if exist "%HERE%PTAR_X86_D3D9_INSTALL_STATE.txt" (
    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HERE%collect_x86_d3d9_diag.ps1"
    goto :done
  )
  echo.
  echo PTAR X86/D3D9 - COLLECTE DIAGNOSTIC GENERIQUE
  echo ==============================================
  echo.
  echo Glissez l'executable cible sur ce BAT,
  echo ou saisissez son chemin complet.
  echo.
  set /p "TARGET=Chemin complet de l'EXE cible : "
)

if defined TARGET (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HERE%collect_x86_d3d9_diag.ps1" -GameExe "%TARGET%"
) else (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HERE%collect_x86_d3d9_diag.ps1"
)

:done
echo.
echo Le ZIP de diagnostic est indique sur la ligne OUTPUT=...
pause
exit /b %ERRORLEVEL%

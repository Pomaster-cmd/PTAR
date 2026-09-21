@echo off
setlocal EnableExtensions
set "HERE=%~dp0"
set "TARGET=%~1"

if not defined TARGET (
  echo.
  echo PTAR X86/D3D9 - INSTALLATION GENERIQUE
  echo ======================================
  echo.
  echo Glissez l'executable x86/D3D9 cible sur ce BAT,
  echo ou relancez:
  echo   01-INSTALL_PTAR_X86_D3D9.bat "C:\chemin\programme.exe"
  echo.
  set /p "TARGET=Chemin complet de l'EXE cible : "
)

if not defined TARGET (
  echo [PTAR] Aucun executable fourni.
  exit /b 2
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HERE%install_x86_d3d9.ps1" -GameExe "%TARGET%"
exit /b %ERRORLEVEL%

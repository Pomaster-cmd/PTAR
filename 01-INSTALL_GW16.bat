@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR SAFEPOINT11 FUSEDDETAIL1 HUDREC1 UNIVERSAL1 - INSTALLATION
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if "%~1"=="" (
  "%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0diag\install.ps1"
) else (
  "%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0diag\install.ps1" -GameExe "%~1"
)
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" (
  echo [FAIL] Installation interrompue - code %RC%.
) else (
  echo [INFO] Installation runtime terminee.
  echo [INFO] Cible memorisee dans win81_nis_install_exe.txt.
  echo [INFO] Pour le recorder QSV, lance 03-INSTALL_QSV_HELPER_WIN81.bat.
)
echo Appuyez sur une touche pour fermer.
pause >nul
exit /b %RC%

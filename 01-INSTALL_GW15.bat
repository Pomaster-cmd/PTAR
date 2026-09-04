@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR GW15 LOCK30 - INSTALLATION
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0diag\install.ps1"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" echo [FAIL] Installation interrompue - code %RC%.
echo Appuyez sur une touche pour fermer.
pause >nul
exit /b %RC%

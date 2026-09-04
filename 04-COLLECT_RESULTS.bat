@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR GW15 LOCK30 - COLLECTE
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0diag\collect.ps1"
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%

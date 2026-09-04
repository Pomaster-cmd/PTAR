@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR GW15 - ROLLBACK TEST
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0diag\rollback_test.ps1"
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%

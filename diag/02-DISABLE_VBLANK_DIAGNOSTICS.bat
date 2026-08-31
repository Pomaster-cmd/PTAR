@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR RC17 - DISABLE VBLANK DIAGNOSTICS
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0set_vblank_diagnostics.ps1" -Value 0
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" echo [ECHEC] Le mode diagnostic n'a pas ete desactive correctement.
pause
exit /b %RC%

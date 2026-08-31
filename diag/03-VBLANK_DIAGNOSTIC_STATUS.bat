@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR RC17 - VBLANK DIAGNOSTIC STATUS
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0set_vblank_diagnostics.ps1" -Value -1
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" echo [ECHEC] Impossible de lire l'etat du diagnostic.
pause
exit /b %RC%

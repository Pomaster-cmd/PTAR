@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0diag\restore_only.ps1"
set "RC=%ERRORLEVEL%"
pause
exit /b %RC%

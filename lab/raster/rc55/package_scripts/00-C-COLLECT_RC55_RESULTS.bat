@echo off
setlocal EnableExtensions
cd /d "%~dp0"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0RC55_BOUND_PHYSICAL_FIX\collect.ps1"
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%

@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0diag\enable_diag.ps1" -GameExe "%~1"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" echo [FAIL] Activation diagnostic interrompue - code %RC%.
pause
exit /b %RC%

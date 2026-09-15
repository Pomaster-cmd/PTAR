@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR RC55 BOUND PHYSICAL IDEMPOTENCE - INSTALL + VERIFY
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0RC55_BOUND_PHYSICAL_FIX\install.ps1"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto :done
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0RC55_BOUND_PHYSICAL_FIX\verify.ps1"
set "RC=%ERRORLEVEL%"
:done
echo.
if "%RC%"=="0" (echo [PASS] RC55 installee et verifiee.) else (echo [FAIL] RC55 - code %RC%.)
pause
exit /b %RC%

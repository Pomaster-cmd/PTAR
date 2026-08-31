@echo off
setlocal EnableExtensions
title PTAR Toolchain Verification Only

rem v0.10.1: no ProjectRoot argument crosses the PowerShell command line.
powershell.exe -NoProfile -ExecutionPolicy Bypass ^
  -File "%~dp0PTAR_AutoSetupAndValidate.ps1" ^
  -VerifyOnly

set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
  echo [PTAR] Toolchain verification successful.
) else (
  echo [PTAR][FAIL] Toolchain verification stopped with code %RC%.
)
echo.
pause
exit /b %RC%

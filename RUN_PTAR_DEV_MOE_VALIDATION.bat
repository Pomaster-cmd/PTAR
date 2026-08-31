@echo off
setlocal EnableExtensions
title PTAR Automatic Setup + GTX 960M Validation

echo ================================================================
echo  PTAR - Automatic Windows 8.1 Toolchain + Hardware Validation
echo ================================================================
echo.
echo This launcher:
echo   - checks the machine and GPU
echo   - installs missing Microsoft build prerequisites
echo   - configures MSVC + FXC
echo   - compiles EDGE-NG v03 K185
echo   - audits DXBC
echo   - runs the 42-case GPU parity test
echo   - measures the GPU pass
echo.
echo It NEVER deletes project files and NEVER reboots Windows automatically.
echo A Windows UAC approval may be required.
echo.

rem v0.10.1: ProjectRoot is intentionally NOT passed here.
rem The PowerShell script derives the root from its own location.
powershell.exe -NoProfile -ExecutionPolicy Bypass ^
  -File "%~dp0automation\windows\PTAR_AutoSetupAndValidate.ps1"

set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
  echo [PTAR] Automation completed successfully.
) else if "%RC%"=="61" (
  echo [PTAR] TLS compatibility settings were applied.
  echo [PTAR] Save your work, restart Windows manually, then run this BAT again.
  echo [PTAR] Nothing was deleted and PTAR did NOT reboot Windows automatically.
) else (
  echo [PTAR][FAIL] Automation stopped with code %RC%.
  echo [PTAR] This is NOT a successful run.
  echo [PTAR] Check automation\windows\runs if a run folder was created.
)
echo.
pause
exit /b %RC%

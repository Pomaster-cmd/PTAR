@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR LAB DWMPHASE2 AUTOCOLLECT1 RC18 - FULLSTACK1 VERIFY - VERIFYFIX5
set "AUTO=0"
if /i "%~1"=="/AUTO" set "AUTO=1"
set "LOG=%~dp0PTAR_VERIFY_LAST.log"
set "PS=%~dp0diag\PTAR_VERIFY_FULLSTACK1.ps1"
if not exist "%PS%" goto :MISSING
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%PS%" >"%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
type "%LOG%"
if "%RC%"=="0" goto :OK
echo.
echo ============================================================
echo [FAIL GLOBAL] Verification FULLSTACK1 echouee.
echo Journal : "%LOG%"
echo ============================================================
goto :END
:OK
echo.
echo ============================================================
echo [PASS GLOBAL] Verification FULLSTACK1 complete.
echo Journal : "%LOG%"
echo ============================================================
goto :END
:MISSING
set "RC=90"
>"%LOG%" echo [FAIL] Helper diag\PTAR_VERIFY_FULLSTACK1.ps1 absent.
type "%LOG%"
:END
if "%AUTO%"=="1" exit /b %RC%
echo.
echo Fermez cette fenetre manuellement quand vous avez termine.
pause >nul
exit /b %RC%

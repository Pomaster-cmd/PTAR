@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title PTAR GW15 - DESINSTALLATION COMPLETE SECURISEE
for %%I in ("%~dp0.") do set "ROOT=%%~fI"
set "TMP=%TEMP%\PTAR_GW15_UNINSTALL_%RANDOM%_%RANDOM%"
mkdir "%TMP%" >nul 2>&1
if not exist "%ROOT%\_PTAR_UNINSTALL\PTAR_SAFE_UNINSTALL.ps1" (
  echo [FAIL] Moteur de desinstallation PTAR absent.
  pause
  exit /b 20
)
copy /y "%ROOT%\_PTAR_UNINSTALL\PTAR_SAFE_UNINSTALL.ps1" "%TMP%\PTAR_SAFE_UNINSTALL.ps1" >nul
if errorlevel 1 (
  echo [FAIL] Impossible de preparer le desinstalleur temporaire.
  pause
  exit /b 21
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%TMP%\PTAR_SAFE_UNINSTALL.ps1" -Root "%ROOT%"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" (
  echo [FAIL] Desinstallation interrompue - code %RC%.
) else (
  echo [PASS] Desinstallation PTAR terminee.
)
echo.
echo Appuyez sur une touche pour fermer.
pause >nul
if "%RC%"=="0" (
  start "" /b "%ComSpec%" /d /c "ping -n 3 127.0.0.1 >nul & del /f /q ^"%ROOT%\06-DESINSTALLER_PTAR_COMPLET.bat^" >nul 2>&1 & rd /s /q ^"%TMP%^" >nul 2>&1"
) else (
  rd /s /q "%TMP%" >nul 2>&1
)
exit /b %RC%

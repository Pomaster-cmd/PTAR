@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0" 2>nul
if errorlevel 1 (
  echo [FAIL] Impossible d'acceder au dossier PTAR.
  pause >nul
  exit /b 19
)
title PTAR COMPLETE - DESINSTALLATION SECURISEE
for %%I in ("%~dp0.") do set "ROOT=%%~fI"
set "TMP=%TEMP%\PTAR_COMPLETE_UNINSTALL_%RANDOM%_%RANDOM%"
set "ENGINE=%ROOT%\_PTAR_UNINSTALL\PTAR_SAFE_UNINSTALL.ps1"
set "PSEXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"

if not exist "%ENGINE%" (
  echo [FAIL] Moteur de desinstallation absent.
  pause >nul
  exit /b 20
)
if not exist "%PSEXE%" (
  echo [FAIL] Windows PowerShell introuvable.
  pause >nul
  exit /b 22
)

mkdir "%TMP%" >nul 2>&1
if errorlevel 1 (
  echo [FAIL] Impossible de creer le dossier temporaire de desinstallation.
  pause >nul
  exit /b 21
)
copy /y "%ENGINE%" "%TMP%\PTAR_SAFE_UNINSTALL.ps1" >nul 2>&1
if errorlevel 1 (
  echo [FAIL] Impossible de preparer le desinstalleur temporaire.
  rd /s /q "%TMP%" >nul 2>&1
  pause >nul
  exit /b 21
)

"%PSEXE%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%TMP%\PTAR_SAFE_UNINSTALL.ps1" -Root "%ROOT%"
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (
  echo [PASS] Desinstallation PTAR terminee.
) else (
  echo [FAIL] Desinstallation interrompue - code %RC%.
)
echo.
echo Appuyez sur une touche pour fermer.
pause >nul

if "%RC%"=="0" (
  >"%TMP%\PTAR_FINAL_CLEANUP.cmd" echo @echo off
  >>"%TMP%\PTAR_FINAL_CLEANUP.cmd" echo ping -n 3 127.0.0.1 ^>nul 2^>^&1
  >>"%TMP%\PTAR_FINAL_CLEANUP.cmd" echo del /f /q "%ROOT%\PACKAGE_SHA256SUMS.txt" ^>nul 2^>^&1
  >>"%TMP%\PTAR_FINAL_CLEANUP.cmd" echo del /f /q "%ROOT%\06-DESINSTALLER_PTAR_AUTO.bat" ^>nul 2^>^&1
  >>"%TMP%\PTAR_FINAL_CLEANUP.cmd" echo rd /s /q "%TMP%" ^>nul 2^>^&1
  start "" /b "%ComSpec%" /d /c call "%TMP%\PTAR_FINAL_CLEANUP.cmd" >nul 2>&1
) else (
  rd /s /q "%TMP%" >nul 2>&1
)
exit /b %RC%

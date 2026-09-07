@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title PTAR FULLSTACK1 - QSV INSTALL FIX1

echo =============================================================
echo FULLSTACK1 RECORDERFIX2 - INSTALLATION QSV WINDOWS 8.1
echo QSVINSTALLFIX1
echo =============================================================
echo.

set "ROOT=%~dp0"
set "PS1=%ROOT%tools\installer\INSTALL_QSV_HELPER_WIN81.ps1"
set "TARGET_FILE=%ROOT%win81_nis_install_target.txt"
set "TARGET="

if not exist "%PS1%" (
  echo [FAIL] Script PowerShell absent:
  echo        "%PS1%"
  pause
  exit /b 20
)

if exist "%TARGET_FILE%" (
  for /f "usebackq delims=" %%T in ("%TARGET_FILE%") do (
    if not defined TARGET set "TARGET=%%~T"
  )
)

if not defined TARGET (
  if exist "%ROOT%SatGat-Win64-Shipping.exe" set "TARGET=%ROOT%"
)

if not defined TARGET (
  echo [FAIL] Cible du jeu introuvable.
  echo        Lance d'abord 01-INSTALL_FULLSTACK1.bat.
  pause
  exit /b 21
)

set "TARGET=!TARGET:"=!"
if not "!TARGET:~-1!"=="\" set "TARGET=!TARGET!\"

echo [INFO] Cible reelle:
echo        "!TARGET!"
echo.

rem No PackageRoot argument: the PS1 derives package root from its own path.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" ^
  -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass ^
  -File "%PS1%"
set "RC=!ERRORLEVEL!"

echo.
if not "!RC!"=="0" (
  echo [FAIL] Le script QSV a echoue - code !RC!.
  echo        Consulte "%ROOT%INSTALL_QSV_HELPER_WIN81.log"
  pause
  exit /b !RC!
)

set "FF=!TARGET!tools\qsv\b18k17_ffmpeg.exe"
set "READY=!TARGET!tools\qsv\B18K18_QSV_READY.txt"

if not exist "!FF!" (
  echo [FAIL] PowerShell a retourne 0 mais le helper est ABSENT de la cible:
  echo        "!FF!"
  echo        Ceci est traite comme un echec.
  pause
  exit /b 31
)

for %%Z in ("!FF!") do set "FFSIZE=%%~zZ"
if !FFSIZE! LSS 1000000 (
  echo [FAIL] Helper anormalement petit: !FFSIZE! octets.
  pause
  exit /b 32
)

set "ENC=%TEMP%\ptar_qsv_encoders_%RANDOM%_%RANDOM%.txt"
"!FF!" -hide_banner -encoders >"!ENC!" 2>&1
set "FRC=!ERRORLEVEL!"
if not "!FRC!"=="0" (
  echo [FAIL] Le helper cible ne demarre pas correctement - code !FRC!.
  del /q "!ENC!" >nul 2>&1
  pause
  exit /b 33
)

findstr /i /c:"h264_qsv" "!ENC!" >nul
if errorlevel 1 (
  echo [FAIL] Le helper cible ne declare pas h264_qsv.
  del /q "!ENC!" >nul 2>&1
  pause
  exit /b 34
)
del /q "!ENC!" >nul 2>&1

if not exist "!READY!" (
  echo [FAIL] Le helper existe mais le marqueur READY est absent:
  echo        "!READY!"
  pause
  exit /b 35
)

echo [PASS] Helper QSV REELLEMENT installe et verifie.
echo [PASS] "!FF!"
echo [PASS] Taille: !FFSIZE! octets
echo [PASS] h264_qsv detecte.
echo.
echo Lance maintenant 04-TEST_QSV_5S_MP4.bat.
pause
exit /b 0

@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title PTAR - QSV PROFILE 1/2/3 SELFTEST

set "ROOT=%~dp0"
set "TARGET_FILE=%ROOT%win81_nis_install_target.txt"
set "TARGET="

if exist "%TARGET_FILE%" (
  for /f "usebackq delims=" %%A in ("%TARGET_FILE%") do (
    if not defined TARGET set "TARGET=%%~A"
  )
)
if not defined TARGET if exist "%ROOT%SatGat-Win64-Shipping.exe" set "TARGET=%ROOT%"

if not defined TARGET (
 echo [FAIL] Cible introuvable.
 pause
 exit /b 1
)

set "TARGET=!TARGET:"=!"
if not "!TARGET:~-1!"=="\" set "TARGET=!TARGET!\"

set "INI=!TARGET!win81_nis.ini"
set "FF=!TARGET!tools\qsv\b18k17_ffmpeg.exe"
set "READY=!TARGET!tools\qsv\B18K18_QSV_READY.txt"

if not exist "!FF!" (
 echo [FAIL] Helper QSV absent.
 echo Lance 03-INSTALL_QSV_HELPER_WIN81.bat.
 pause
 exit /b 2
)

set "PROFILE=1"
if exist "!INI!" (
  for /f "tokens=1,2 delims==" %%A in ('findstr /b /c:"VideoRecordProfile=" "!INI!"') do set "PROFILE=%%B"
)

if "!PROFILE!"=="1" (
 set "W=1920"
 set "H=1080"
 set "FPS=30"
 set "KBPS=16000"
 set "PNAME=QUALITY"
) else if "!PROFILE!"=="2" (
 set "W=1600"
 set "H=900"
 set "FPS=60"
 set "KBPS=17000"
 set "PNAME=MOTION"
) else if "!PROFILE!"=="3" (
 set "W=1920"
 set "H=1080"
 set "FPS=60"
 set "KBPS=22000"
 set "PNAME=COMBINED"
) else (
 set "PROFILE=1"
 set "W=1920"
 set "H=1080"
 set "FPS=30"
 set "KBPS=16000"
 set "PNAME=QUALITY-FALLBACK"
)

if not exist "!TARGET!recordings" mkdir "!TARGET!recordings" >nul 2>&1
set "STAMP=%RANDOM%_%RANDOM%"
set "OUT=!TARGET!recordings\PTAR_PROFILE!PROFILE!_!W!x!H!_!FPS!FPS_SELFTEST_5S_!STAMP!.mp4"
set "LOG=%ROOT%TEST_QSV_PROFILE_SELFTEST.log"

echo =============================================================
echo PTAR QSV PROFILE SELFTEST - 5 SECONDES
echo =============================================================
echo PROFILE !PROFILE! - !PNAME!
echo !W!x!H! / !FPS! FPS / !KBPS! kbps
echo.

"!FF!" -hide_banner -loglevel warning ^
 -f lavfi -i "testsrc2=size=!W!x!H!:rate=!FPS!" ^
 -t 5 -vf format=nv12 -an ^
 -c:v h264_qsv -b:v !KBPS!k -maxrate !KBPS!k -bufsize !KBPS!k ^
 -movflags +faststart -n "!OUT!" >"!LOG!" 2>&1

set "RC=!ERRORLEVEL!"
if not "!RC!"=="0" (
 echo [FAIL] Encodage QSV echoue - code !RC!.
 echo Envoie "!LOG!".
 pause
 exit /b !RC!
)

if not exist "!OUT!" (
 echo [FAIL] Aucun MP4 cree.
 pause
 exit /b 4
)

for %%Z in ("!OUT!") do set "SZ=%%~zZ"
if !SZ! LSS 10000 (
 echo [FAIL] MP4 anormalement petit: !SZ! octets.
 pause
 exit /b 5
)

echo [PASS] PROFILE !PROFILE! !PNAME!
echo [PASS] MP4 cree: "!OUT!"
echo [PASS] Taille: !SZ! octets
echo.
echo Verifie qu'il dure environ 5 secondes et qu'il est lisible.
pause
exit /b 0

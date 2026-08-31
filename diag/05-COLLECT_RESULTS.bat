@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
for %%I in ("%~dp0..") do set "ROOT=%%~fI\"
set "TARGET_FILE=%ROOT%win81_nis_install_target.txt"
set "TARGET_EXE_FILE=%ROOT%win81_nis_install_exe.txt"
set "TARGET="
set "TARGET_EXE="

title PTAR DISPLAY DELIVERY LAB DWMPHASE2 AUTOCOLLECT1 RC18 - COLLECTE

if not exist "%TARGET_FILE%" (
  echo [FAIL] win81_nis_install_target.txt absent. Installez PTAR d'abord.
  echo Appuyez sur une touche lorsque vous voulez fermer cette fenetre.
  pause >nul
  exit /b 2
)

for /f "usebackq delims=" %%A in ("%TARGET_FILE%") do if not defined TARGET set "TARGET=%%~A"
if not defined TARGET (
  echo [FAIL] Cible PTAR vide.
  echo Appuyez sur une touche lorsque vous voulez fermer cette fenetre.
  pause >nul
  exit /b 3
)

if "%TARGET:~-1%"=="\" (
  set "TARGET_DIR=%TARGET%"
) else (
  set "TARGET_DIR=%TARGET%\"
)

if not exist "%TARGET_DIR%." (
  echo [FAIL] Dossier cible introuvable: "%TARGET_DIR%"
  echo Appuyez sur une touche lorsque vous voulez fermer cette fenetre.
  pause >nul
  exit /b 4
)

if exist "%TARGET_EXE_FILE%" (
  for /f "usebackq delims=" %%A in ("%TARGET_EXE_FILE%") do if not defined TARGET_EXE set "TARGET_EXE=%%~A"
)

set "OUT=%ROOT%PTARFG1_RESULTS_%RANDOM%_%RANDOM%"
if exist "%OUT%" (
  echo [FAIL] Collision dossier resultats.
  echo Appuyez sur une touche lorsque vous voulez fermer cette fenetre.
  pause >nul
  exit /b 5
)

mkdir "%OUT%" >nul 2>&1
if errorlevel 1 (
  echo [FAIL] Creation du dossier resultats impossible.
  echo Appuyez sur une touche lorsque vous voulez fermer cette fenetre.
  pause >nul
  exit /b 6
)

>"%OUT%\TARGET.txt" echo %TARGET_DIR%
if defined TARGET_EXE >"%OUT%\TARGET_EXE.txt" echo %TARGET_EXE%

call :COPY "%TARGET_DIR%win81_nis.log" "%OUT%\win81_nis.log"
call :COPY "%TARGET_DIR%win81_nis.ini" "%OUT%\win81_nis.ini"
call :COPY "%TARGET_DIR%win81_nis_version.txt" "%OUT%\win81_nis_version.txt"
call :COPY "%ROOT%diag\PTAR_PACKAGE_ID.txt" "%OUT%\PTAR_PACKAGE_ID.txt"
call :COPY "%ROOT%diag\PRODUCTION_VALIDATION.txt" "%OUT%\PRODUCTION_VALIDATION.txt"
call :COPY "%ROOT%PTAR_VERIFY_LAST.log" "%OUT%\PTAR_VERIFY_LAST.log"
call :COPY "%ROOT%PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt" "%OUT%\PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt"
call :COPY "%ROOT%PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt" "%OUT%\PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt"
call :COPY "%ROOT%diag\LAST_VISIBLE_VERIFIER.log" "%OUT%\LEGACY_LAST_VISIBLE_VERIFIER.log"

set "CAPCOUNT=0"
for %%F in ("%TARGET_DIR%win81_nis_capture_*.bmp") do (
  if exist "%%~fF" (
    copy /y "%%~fF" "%OUT%\%%~nxF" >nul
    if not errorlevel 1 set /a CAPCOUNT+=1 >nul
  )
)

if exist "%TARGET_DIR%recordings\." (
  dir /a:-d /o:-d "%TARGET_DIR%recordings\*.mp4" >"%OUT%\recordings_list.txt" 2>nul
)

if exist "%TARGET_DIR%d3d11.dll" (
  certutil -hashfile "%TARGET_DIR%d3d11.dll" SHA256 >"%OUT%\INSTALLED_DLL_SHA256.txt" 2>nul
)

>"%OUT%\COLLECT_INFO.txt" echo PTAR_DISPLAY_DELIVERY_LAB_DWMPHASE2_AUTOCOLLECT1_RC18_COLLECT=PASS
>>"%OUT%\COLLECT_INFO.txt" echo FIELD_MACHINE_EXPECTED=GTX_960M_WINDOWS_8_1_DWMPHASE2_AUTOCOLLECT1_TEST
>>"%OUT%\COLLECT_INFO.txt" echo TARGET=%TARGET_DIR%
if defined TARGET_EXE >>"%OUT%\COLLECT_INFO.txt" echo TARGET_EXE=%TARGET_EXE%
>>"%OUT%\COLLECT_INFO.txt" echo F9_BMP_COPIED=%CAPCOUNT%
>>"%OUT%\COLLECT_INFO.txt" echo POLICY=COPY_ONLY_NO_DELETE_NO_GAME_FILE_MODIFICATION

>"%OUT%\USER_RESULT.txt" echo A COMPLETER: FPS FG OFF / FPS FG ON / HUD DISPLAY / fluidite / crash-TDR / FG OFF stable 60s ? Les chiffres visible-verifier et le DWM LAB F8 sont collectes automatiquement.

echo.
echo [PASS] Collecte terminee.
echo [INFO] Resultats:
echo "%OUT%"
echo [SAFE] Aucun fichier du jeu n'a ete supprime ou modifie.
echo.
echo Appuyez sur une touche lorsque vous voulez fermer cette fenetre.
pause >nul
exit /b 0

:COPY
if exist "%~1" copy /y "%~1" "%~2" >nul
exit /b 0

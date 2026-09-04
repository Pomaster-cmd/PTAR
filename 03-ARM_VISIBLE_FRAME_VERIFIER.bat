@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
title PTAR GW15 - VBLANK3 VISIBLE + PACING

if /i "%~1"=="__PTAR_DIAG_INNER__" goto :INNER
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "PTAR_DIAG_SCRIPT=%~f0"
cls
echo ============================================================
echo PTAR GW15 - VBLANK3 VERIFIER
echo ============================================================
echo [INFO] UN seul moteur capture le marqueur et calcule a la fois :
echo        FPS visibles / GENERATED / REAL / gaps / parite + frame pacing.
echo [INFO] Aucun second processus de capture ecran n'est lance.
echo [INFO] Le diagnostic va ouvrir sa console en administrateur.
echo.
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$q=[char]34;$a='/D /K call '+$q+$env:PTAR_DIAG_SCRIPT+$q+' __PTAR_DIAG_INNER__';Start-Process -FilePath $env:ComSpec -ArgumentList $a -Verb RunAs"
set "LAUNCH_RC=%ERRORLEVEL%"
if "%LAUNCH_RC%"=="0" exit /b 0
echo [ERREUR] La console administrateur n'a pas pu etre lancee. Code %LAUNCH_RC%
pause
exit /b %LAUNCH_RC%

:INNER
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "HELPER=%~dp0diag\set_vblank_diagnostics.ps1"
set "SHAHELPER=%~dp0diag\PTAR_SHA256.ps1"
set "RUNNER=%~dp0diag\visible_pacing\run_single_engine_verifier.ps1"
set "SOURCE=%~dp0diag\visible_pacing\PTARVisiblePacingVerifier.cs"
set "EXPECTED_RUNTIME=2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d"
set "GAMEROOT="
if exist "%~dp0Warhammer.exe" set "GAMEROOT=%~dp0"
if not defined GAMEROOT for %%I in ("%~dp0..") do if exist "%%~fI\Warhammer.exe" set "GAMEROOT=%%~fI\"
if not defined GAMEROOT (
  echo [ERREUR] Warhammer.exe introuvable dans la racine du pack ou son parent.
  exit /b 26
)
set "STATUS=%GAMEROOT%PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt"
set "OUTPUT=%GAMEROOT%PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt"
set "CSV=%GAMEROOT%PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv"
set "ERR=%GAMEROOT%PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt"
set "RUNTIME=%GAMEROOT%d3d11.dll"
set "RC=0"

cls
echo ============================================================
echo PTAR GW15 - VBLANK3 VERIFIER
echo ============================================================
echo Une seule capture GDI par echantillon :
echo   - UNIQUE / GENERATED / REAL / gaps / parite
echo   - frametime / dwell G-R / jitter / stalls / vblank spans
echo.

"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$id=[Security.Principal.WindowsIdentity]::GetCurrent();$p=New-Object Security.Principal.WindowsPrincipal($id);if($p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){exit 0}else{exit 1}" >nul 2>&1
if errorlevel 1 (
  echo [ERREUR] La console n'est pas elevee administrateur.
  set "RC=22"
  goto :END
)
echo [PASS] Console administrateur confirmee.

echo.
echo [1/3] Verification GW15 actif + moteur VBLANK3...
if not exist "%RUNTIME%" (
  echo [ERREUR] d3d11.dll actif introuvable.
  set "RC=27"
  goto :END
)
call :HASH "%RUNTIME%" RUNTIME_HASH
if /i not "%RUNTIME_HASH%"=="%EXPECTED_RUNTIME%" (
  echo [ERREUR] Le runtime actif n'est pas GW15 exact.
  echo Attendu : %EXPECTED_RUNTIME%
  echo Actif   : %RUNTIME_HASH%
  set "RC=29"
  goto :END
)
if not exist "%RUNNER%" (
  echo [ERREUR] Runner VBLANK3 absent.
  set "RC=30"
  goto :END
)
if not exist "%SOURCE%" (
  echo [ERREUR] Source moteur VBLANK3 absente.
  set "RC=31"
  goto :END
)
echo [PASS] Runtime GW15 exact + moteur VBLANK3 present.

echo.
echo [2/3] Activation VBlankDiagnostics=1...
if not exist "%HELPER%" (
  echo [ERREUR] Helper diagnostic absent.
  set "RC=20"
  goto :END
)
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HELPER%" -Value 1 -RequireEnabled
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo [ERREUR] Impossible d'activer VBlankDiagnostics=1.
  goto :END
)

for %%F in ("%OUTPUT%" "%CSV%" "%ERR%" "%GAMEROOT%PTAR_FLUIDITY_LAST_OUTPUT.txt" "%GAMEROOT%PTAR_FLUIDITY_LAST_SAMPLES.csv" "%GAMEROOT%PTAR_VISIBLE_VERIFIER_LAST_COUNTS.txt") do if exist "%%~F" del /q "%%~F" >nul 2>&1

echo.
echo [3/3] Mesure SINGLE-ENGINE - PROCEDURE :
echo   1. Lancer Inquisitor et placer la scene a tester.
echo   2. CTRL+F6 ON si le FG n'est pas deja actif.
echo   3. Attendre 2 a 3 secondes.
echo   4. Appuyer UNE SEULE FOIS sur F5.
echo   5. Ne pas modifier le FG pendant les 20 secondes.
echo.
echo IMPORTANT : apres F5, le moteur valide d'abord la presence physique du marqueur.
echo Si le marqueur n'est pas lisible, le test s'arrete immediatement au lieu d'attendre 20 s.
echo BIP 900 Hz = debut mesure ; BIP 1400 Hz = fin mesure.
echo IMPORTANT : si CAPTURE RATE est proche de 60 Hz, le pacing est exploitable.
echo Si SAMPLING / REFRESH RATIO est inferieur a 0.90x, le rapport le signalera.
echo.
for %%I in ("%GAMEROOT%.") do set "PTAR_GAME_ROOT=%%~fI"
echo [PASS] Racine jeu transmise par environnement : "%PTAR_GAME_ROOT%"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%RUNNER%" -DurationSeconds 20
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo [ERREUR] VBLANK3 a retourne %RC%.
  if exist "%ERR%" type "%ERR%"
  goto :END
)

echo.
echo ---------------- RAPPORT VBLANK3 ----------------
if exist "%OUTPUT%" type "%OUTPUT%"
if not exist "%OUTPUT%" (
  echo [ERREUR] Rapport VBLANK3 absent.
  set "RC=36"
)

:END
echo.
echo ============================================================
if "%RC%"=="0" echo [ETAT] OK - comptage + fluidite issus DU MEME flux de capture.
if not "%RC%"=="0" echo [ETAT] ECHEC/PARTIEL - code %RC%.
echo Rapport principal : "%OUTPUT%"
echo CSV frame-by-frame : "%CSV%"
echo ============================================================
echo.
echo LA FENETRE RESTE OUVERTE. Fermez-la avec la croix ou tapez EXIT.
exit /b %RC%

:HASH
setlocal EnableDelayedExpansion
set "HV="
set "HASH_TMP=%TEMP%\PTAR_V3_HASH_%RANDOM%_%RANDOM%.txt"
set "HASH_ERR=%HASH_TMP%.err"
if exist "%SHAHELPER%" (
  "%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SHAHELPER%" -InputFile "%~1" >"%HASH_TMP%" 2>"%HASH_ERR%"
  if not errorlevel 1 if exist "%HASH_TMP%" set /p "HV="<"%HASH_TMP%"
)
if not defined HV (
  "%SystemRoot%\System32\certutil.exe" -hashfile "%~1" SHA256 >"%HASH_TMP%" 2>"%HASH_ERR%"
  if not errorlevel 1 for /f "skip=1 delims=" %%H in ('type "%HASH_TMP%"') do if not defined HV set "HV=%%H"
)
set "HV=!HV: =!"
if exist "%HASH_TMP%" del /q "%HASH_TMP%" >nul 2>&1
if exist "%HASH_ERR%" del /q "%HASH_ERR%" >nul 2>&1
endlocal & set "%~2=%HV%"
exit /b 0

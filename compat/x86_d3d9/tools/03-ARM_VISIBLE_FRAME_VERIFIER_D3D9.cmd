@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
title PTAR Conviction D3D9 - VBLANK3 VISIBLE + PACING

if /i "%~1"=="__PTAR_DIAG_INNER__" goto :INNER
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "PTAR_DIAG_SCRIPT=%~f0"
cls
echo ============================================================
echo PTAR CONVICTION D3D9 - VBLANK3 VISIBLE FRAME VERIFIER
echo ============================================================
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$q=[char]34;$a='/D /K call '+$q+$env:PTAR_DIAG_SCRIPT+$q+' __PTAR_DIAG_INNER__';Start-Process -FilePath $env:ComSpec -ArgumentList $a -Verb RunAs"
if "%ERRORLEVEL%"=="0" exit /b 0
exit /b %ERRORLEVEL%

:INNER
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "ROOT=%~dp0"
set "HELPER=%ROOT%set_vblank_diagnostics_d3d9.ps1"
set "RUNNER=%ROOT%visible_pacing\run_single_engine_verifier.ps1"
set "SOURCE=%ROOT%visible_pacing\PTARVisiblePacingVerifier.cs"
set "EXPECTED_RUNTIME=c4a0668b313a2d6ca31c165eb2e5b216559bc3de8c5a1fe60b4aa9ac57c4a16d"
set "RUNTIME=%ROOT%d3d9.dll"
set "GAMEEXE=%ROOT%Conviction_game.exe"
set "OUTPUT=%ROOT%PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt"
set "RC=0"

if not exist "%RUNTIME%" (
 echo [ERREUR] d3d9.dll actif introuvable dans : "%ROOT%"
 echo [INFO] Extrais ce pack dans le dossier qui contient le d3d9.dll PTAR actif.
 exit /b 27
)
if not exist "%GAMEEXE%" (
 echo [ERREUR] Conviction_game.exe introuvable dans : "%ROOT%"
 echo [INFO] Le verifier doit etre place dans le dossier system du jeu.
 exit /b 26
)
if not exist "%HELPER%" (echo [ERREUR] Helper VBlank absent.&exit /b 28)
if not exist "%RUNNER%" (echo [ERREUR] Runner VBLANK3 absent.&exit /b 28)
if not exist "%SOURCE%" (echo [ERREUR] Moteur VBLANK3 absent.&exit /b 28)

tasklist /FI "IMAGENAME eq Conviction_game.exe" 2>NUL | find /I "Conviction_game.exe" >NUL
if not errorlevel 1 (
 echo [ERREUR] Conviction_game.exe est deja lance.
 echo [INFO] Ferme le jeu. Le marqueur doit etre arme AVANT le demarrage du runtime D3D9.
 exit /b 30
)

set "RUNTIME_HASH="
for /f "usebackq delims=" %%H in (`"%SystemRoot%\System32\certutil.exe" -hashfile "%RUNTIME%" SHA256 2^>nul ^| findstr /R /I "^[0-9A-F][0-9A-F ]*$"`) do if not defined RUNTIME_HASH set "RUNTIME_HASH=%%H"
if not defined RUNTIME_HASH (
 echo [ERREUR] Impossible de calculer le SHA-256 du runtime.
 exit /b 31
)
set "RUNTIME_HASH=%RUNTIME_HASH: =%"
if /i not "%RUNTIME_HASH%"=="%EXPECTED_RUNTIME%" (
 echo [ERREUR] Le d3d9.dll actif n'est pas la base terrain validee Legacy Repair V1.
 echo [ATTENDU] %EXPECTED_RUNTIME%
 echo [ACTIF]    %RUNTIME_HASH%
 exit /b 29
)

"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HELPER%" -GameRoot "%ROOT%" -Value 1 -RequireEnabled
if errorlevel 1 exit /b %ERRORLEVEL%

echo.
echo PROCEDURE :
echo   1. Lance Conviction_game.exe.
echo   2. Dans le jeu, active le FG avec CTRL+F6 si necessaire.
echo   3. Attends 2 a 3 secondes dans une scene animee.
echo   4. Appuie UNE SEULE FOIS sur F5.
echo   5. Ne change plus de mode pendant les 20 secondes de mesure.
echo.
echo Le verifier reprend EXACTEMENT le moteur VBLANK3 D3D11 :
echo   - comptage des contenus REAL et GENERATED visibles
echo   - verification de l'alternance G/R
echo   - cadence visible et pacing
echo   - exclusion des stalls provoques par l'observateur
echo.
set "PTAR_GAME_ROOT=%ROOT%"
set "PTAR_TARGET_EXE=%GAMEEXE%"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%RUNNER%" -DurationSeconds 20
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" if exist "%OUTPUT%" (
 echo.
 type "%OUTPUT%"
)

echo.
echo Rapport : "%OUTPUT%"
exit /b %RC%

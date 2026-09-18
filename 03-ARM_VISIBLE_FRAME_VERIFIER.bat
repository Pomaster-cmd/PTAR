@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
title PTAR HUDREC1 UNIVERSAL1 - VBLANK3 VISIBLE + PACING
if /i "%~1"=="__PTAR_DIAG_INNER__" goto :INNER
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "PTAR_DIAG_SCRIPT=%~f0"
cls
echo ============================================================
echo PTAR HUDREC1 UNIVERSAL1 - VBLANK3 VERIFIER
echo ============================================================
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$q=[char]34;$a='/D /K call '+$q+$env:PTAR_DIAG_SCRIPT+$q+' __PTAR_DIAG_INNER__';Start-Process -FilePath $env:ComSpec -ArgumentList $a -Verb RunAs"
if "%ERRORLEVEL%"=="0" exit /b 0
exit /b %ERRORLEVEL%

:INNER
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "HELPER=%~dp0diag\set_vblank_diagnostics.ps1"
set "SHAHELPER=%~dp0diag\PTAR_SHA256.ps1"
set "RUNNER=%~dp0diag\visible_pacing\run_single_engine_verifier.ps1"
set "SOURCE=%~dp0diag\visible_pacing\PTARVisiblePacingVerifier.cs"
set "EXPECTED_RUNTIME=bc291f0f91013df7a28630ffef44983856fce6eb71d79aca597ab292012165e0"
set "TARGET_FILE=%~dp0win81_nis_install_target.txt"
set "EXE_FILE=%~dp0win81_nis_install_exe.txt"
if not exist "%TARGET_FILE%" (
 echo [ERREUR] Installation cible absente. Lance 01-INSTALL_GW16.bat.
 exit /b 26
)
if not exist "%EXE_FILE%" (
 echo [ERREUR] Executable cible memorise absent. Reinstalle UNIVERSAL1.
 exit /b 26
)
set /p "GAMEROOT="<"%TARGET_FILE%"
set /p "GAMEEXE="<"%EXE_FILE%"
if not exist "%GAMEEXE%" (
 echo [ERREUR] Executable cible introuvable : "%GAMEEXE%"
 exit /b 26
)
set "RUNTIME=%GAMEROOT%\d3d11.dll"
set "OUTPUT=%GAMEROOT%\PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt"
set "CSV=%GAMEROOT%\PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv"
set "ERR=%GAMEROOT%\PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt"
set "RC=0"
if not exist "%RUNTIME%" (echo [ERREUR] d3d11.dll actif introuvable.&exit /b 27)
call :HASH "%RUNTIME%" RUNTIME_HASH
if /i not "%RUNTIME_HASH%"=="%EXPECTED_RUNTIME%" (
 echo [ERREUR] Runtime actif inattendu.
 exit /b 29
)
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HELPER%" -Value 1 -RequireEnabled
if errorlevel 1 exit /b %ERRORLEVEL%
echo.
echo PROCEDURE :
echo   1. Lance le jeu cible : "%GAMEEXE%"
echo   2. CTRL+F6 ON si FG n'est pas deja actif.
echo   3. Attends 2 a 3 secondes.
echo   4. Appuie UNE SEULE FOIS sur F5.
echo.
set "PTAR_GAME_ROOT=%GAMEROOT%"
set "PTAR_TARGET_EXE=%GAMEEXE%"
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%RUNNER%" -DurationSeconds 20
set "RC=%ERRORLEVEL%"
if "%RC%"=="0" if exist "%OUTPUT%" type "%OUTPUT%"
echo.
echo Rapport : "%OUTPUT%"
exit /b %RC%

:HASH
setlocal EnableDelayedExpansion
set "HV="
set "HASH_TMP=%TEMP%\PTAR_HASH_%RANDOM%_%RANDOM%.txt"
if exist "%SHAHELPER%" (
 "%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SHAHELPER%" -InputFile "%~1" >"%HASH_TMP%" 2>nul
 if not errorlevel 1 if exist "%HASH_TMP%" set /p "HV="<"%HASH_TMP%"
)
if not defined HV (
 "%SystemRoot%\System32\certutil.exe" -hashfile "%~1" SHA256 >"%HASH_TMP%" 2>nul
 if not errorlevel 1 for /f "skip=1 delims=" %%H in ('type "%HASH_TMP%"') do if not defined HV set "HV=%%H"
)
set "HV=!HV: =!"
if exist "%HASH_TMP%" del /q "%HASH_TMP%" >nul 2>&1
endlocal & set "%~2=%HV%"
exit /b 0

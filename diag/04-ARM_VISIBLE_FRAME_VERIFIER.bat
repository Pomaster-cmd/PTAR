@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
title PTAR RC18 DWMPHASE2 AUTOCOLLECT1 - VISIBLE FRAME VERIFIER

rem DIAGREPAIR5/HOTKEYFIX1/ESCSAFE1:
rem - elevate the existing diagnostic entry point before launching the verifier;
rem - keep the verifier directly attached to an elevated CMD /K console;
rem - verifier accepts F5 alone as well as CTRL+F5.
if /i "%~1"=="__PTAR_DIAG_INNER__" goto :INNER

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "PTAR_DIAG_SCRIPT=%~f0"
cls
echo ============================================================
echo PTAR RC18 DWMPHASE2 AUTOCOLLECT1 - VISIBLE FRAME VERIFIER
echo ============================================================
echo [INFO] Le diagnostic va ouvrir sa console en administrateur.
echo [INFO] Acceptez la demande UAC Windows.
echo.
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$q=[char]34;$a='/D /K call '+$q+$env:PTAR_DIAG_SCRIPT+$q+' __PTAR_DIAG_INNER__';Start-Process -FilePath $env:ComSpec -ArgumentList $a -Verb RunAs"
set "LAUNCH_RC=%ERRORLEVEL%"
if "%LAUNCH_RC%"=="0" exit /b 0

echo.
echo [ERREUR] La console administrateur n'a pas pu etre lancee.
echo Code PowerShell/UAC : %LAUNCH_RC%
echo Le diagnostic n'a rien modifie au runtime PTAR.
echo.
pause
exit /b %LAUNCH_RC%

:INNER
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "HELPER=%~dp0set_vblank_diagnostics.ps1"
set "SHAHELPER=%~dp0PTAR_SHA256.ps1"
set "VERIFIER=%~dp0visible_verifier\win81_vblank2_visible_marker_x64.exe"
set "EXPECTED_VERIFIER=67b163bf3203366562f066c10ac971992b5846991f19c68f61cbd975f9ef8305"
set "STATUS=%~dp0..\PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt"
set "OUTPUT=%~dp0..\PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt"
set "RC=0"
set "VERIFIER_HASH="
set "HASH_TMP=%TEMP%\PTAR_VISIBLE_HASH_%RANDOM%_%RANDOM%.txt"
set "HASH_ERR=%HASH_TMP%.err"

>"%STATUS%" echo PTAR RC18 DWMPHASE2 AUTOCOLLECT1 DIAGREPAIR5 HOTKEYFIX1 ESCSAFE1
>>"%STATUS%" echo START_DATE=%DATE%
>>"%STATUS%" echo START_TIME=%TIME%
>>"%STATUS%" echo DIAG_DIR=%~dp0
>>"%STATUS%" echo VERIFIER_OUTPUT=%OUTPUT%

cls
echo ============================================================
echo PTAR RC18 DWMPHASE2 AUTOCOLLECT1 - VISIBLE FRAME VERIFIER
echo ============================================================
echo Le verificateur reste celui du systeme diag existant.
echo Il est lance directement, sans Tee, pipe ni START detache.
echo AUTOCOLLECT1 sauvegarde sa sortie texte pour la collecte finale.
echo Cette console reste ouverte via CMD /K.
echo.

"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$id=[Security.Principal.WindowsIdentity]::GetCurrent();$p=New-Object Security.Principal.WindowsPrincipal($id);if($p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){exit 0}else{exit 1}" >nul 2>&1
if errorlevel 1 (
  echo [ERREUR] La console n'est pas elevee administrateur.
  >>"%STATUS%" echo RESULT=NOT_ELEVATED
  set "RC=22"
  goto :END
)
echo [PASS] Console administrateur confirmee.
>>"%STATUS%" echo ELEVATED=YES

echo.
echo [1/4] Activation + relecture Win32 de VBlankDiagnostics=1...
if exist "%HELPER%" goto :HELPER_OK
echo [ERREUR] Helper diagnostic introuvable :
echo "%HELPER%"
>>"%STATUS%" echo RESULT=HELPER_MISSING
set "RC=20"
goto :END

:HELPER_OK
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HELPER%" -Value 1 -RequireEnabled
set "RC=%ERRORLEVEL%"
>>"%STATUS%" echo VBLANK_HELPER_RC=%RC%
if "%RC%"=="0" goto :VBLANK_OK
echo.
echo [ERREUR] Impossible d'activer/confirmer VBlankDiagnostics=1.
>>"%STATUS%" echo RESULT=VBLANK_ENABLE_FAILED
goto :END

:VBLANK_OK
echo.
echo [2/4] Verification du binaire visible-verifier HOTKEYFIX1 + ESCSAFE1...
if not exist "%VERIFIER%" (
  echo [ERREUR] Verificateur introuvable :
  echo "%VERIFIER%"
  >>"%STATUS%" echo RESULT=VERIFIER_MISSING
  set "RC=21"
  goto :END
)
if exist "%HASH_TMP%" del /q "%HASH_TMP%" >nul 2>&1
if exist "%HASH_ERR%" del /q "%HASH_ERR%" >nul 2>&1
if exist "%SHAHELPER%" (
  "%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%SHAHELPER%" -InputFile "%VERIFIER%" >"%HASH_TMP%" 2>"%HASH_ERR%"
  if not errorlevel 1 if exist "%HASH_TMP%" set /p "VERIFIER_HASH="<"%HASH_TMP%"
)
if not defined VERIFIER_HASH (
  "%SystemRoot%\System32\certutil.exe" -hashfile "%VERIFIER%" SHA256 >"%HASH_TMP%" 2>"%HASH_ERR%"
  if not errorlevel 1 (
    for /f "skip=1 delims=" %%H in ('type "%HASH_TMP%"') do if not defined VERIFIER_HASH set "VERIFIER_HASH=%%H"
  )
)
if defined VERIFIER_HASH set "VERIFIER_HASH=%VERIFIER_HASH: =%"
if exist "%HASH_TMP%" del /q "%HASH_TMP%" >nul 2>&1
if exist "%HASH_ERR%" del /q "%HASH_ERR%" >nul 2>&1
if not defined VERIFIER_HASH (
  echo [ERREUR] SHA-256 du verificateur impossible a calculer ^(PowerShell + CertUtil^).
  >>"%STATUS%" echo RESULT=VERIFIER_HASH_FAILED
  set "RC=24"
  goto :END
)
echo [VERIFIER SHA-256] %VERIFIER_HASH%
>>"%STATUS%" echo VERIFIER_SHA256=%VERIFIER_HASH%
if /i not "%VERIFIER_HASH%"=="%EXPECTED_VERIFIER%" (
  echo [ERREUR] Le verificateur n'est pas la version HOTKEYFIX1 + ESCSAFE1 attendue.
  echo Attendu : %EXPECTED_VERIFIER%
  >>"%STATUS%" echo RESULT=VERIFIER_HASH_MISMATCH
  set "RC=25"
  goto :END
)
echo [PASS] Verificateur HOTKEYFIX1 + ESCSAFE1 exact.

echo.
echo [3/4] Hotkey de mesure : F5 seul OU CTRL+F5. ESC est ignore.
echo La dependance au maintien de CTRL a ete retiree du verificateur.
echo La touche ESC n'annule plus le diagnostic, meme si elle est utilisee dans le jeu.
echo CTRL+F5 reste donc compatible, mais F5 seul est le test recommande.
echo.
echo Procedure dans le jeu :
echo   1. Lancer Inquisitor.
echo   2. Profil CONSERVATIVE si c'est le profil a mesurer.
echo   3. Si FG etait deja ON : CTRL+F6 OFF puis CTRL+F6 ON.
echo   4. Attendre 2 a 3 secondes.
echo   5. Appuyer UNE FOIS sur F5.
echo   6. Le HUD PTAR affiche VISIB TEST quand le hotkey est recu.
echo   7. Laisser les 20 secondes se terminer.
echo   8. Le resume UNIQUE / GENERATED / REAL est sauvegarde automatiquement.
echo   9. Pour arreter le diagnostic : fermer MANUELLEMENT cette fenetre.
echo.

echo [4/4] Lancement DIRECT du verificateur existant...
echo ---------------- VISIBLE VERIFIER ----------------
>>"%STATUS%" echo VERIFIER_START_TIME=%TIME%
pushd "%~dp0visible_verifier"
"%VERIFIER%" >"%OUTPUT%" 2>&1
set "RC=%ERRORLEVEL%"
popd
if exist "%OUTPUT%" (
  type "%OUTPUT%"
  >>"%STATUS%" echo VERIFIER_OUTPUT_SAVED=YES
) else (
  >>"%STATUS%" echo VERIFIER_OUTPUT_SAVED=NO
)
>>"%STATUS%" echo VERIFIER_RC=%RC%
>>"%STATUS%" echo VERIFIER_END_TIME=%TIME%
echo ---------------- FIN VISIBLE VERIFIER -----------
echo.
if "%RC%"=="0" (
  echo [OK] Le verificateur s'est termine normalement.
  >>"%STATUS%" echo RESULT=VERIFIER_COMPLETED
) else (
  echo [ERREUR] Le verificateur a retourne le code %RC%.
  echo Le message exact est conserve dans PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt.
  >>"%STATUS%" echo RESULT=VERIFIER_FAILED
)

:END
echo.
echo ============================================================
echo DIAGREPAIR5 HOTKEYFIX1 ESCSAFE1 - FIN DU SCRIPT
if "%RC%"=="0" echo [ETAT] OK
if not "%RC%"=="0" echo [ETAT] ECHEC - code %RC%
echo Statut : "%STATUS%"
echo ============================================================
echo.
echo LA FENETRE RESTE OUVERTE.
echo Fermez-la avec la croix ou tapez EXIT lorsque vous avez fini.
exit /b %RC%

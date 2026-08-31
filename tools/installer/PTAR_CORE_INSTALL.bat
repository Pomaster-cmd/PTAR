@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "ROOT=%~dp0..\..\"
for %%I in ("%ROOT%") do set "ROOT=%%~fI\"
cd /d "%ROOT%"
set "CAND=%TEMP%\win81_usr_candidates_%RANDOM%_%RANDOM%.txt"
set "STALE=%TEMP%\win81_usr_stale_%RANDOM%_%RANDOM%.txt"
set "TARGET_FILE=%ROOT%win81_nis_install_target.txt"
set "TARGET_EXE_FILE=%ROOT%win81_nis_install_exe.txt"
set "BACKUP_FILE=%ROOT%win81_nis_install_backup.txt"
set "QUAR_FILE=%ROOT%win81_nis_quarantined_p1fg7n.txt"
>"%CAND%" type nul
>"%STALE%" type nul

  echo Win81 Universal Spatial Presenter v0.41-P1FG7N-VBLANK2-PTARFG1B18K18 - PTAR-NG MOE V01 + FIXED-QPC 540P INTEL QSV RECORDER TEST
 echo LOCKED P1U46 + P1FG7D HOTKEY/TELEMETRY + ISOLATED DISPLAY DEVICE/MAILBOX
 echo ======================================================
 echo.
if not exist "%ROOT%win81_nis_dx11_x64.dll" (
  echo [ERREUR] win81_nis_dx11_x64.dll introuvable.
  pause
  exit /b 1
)

for /r "%ROOT%" %%F in (*-Win64-Shipping.exe) do >>"%CAND%" echo %%~fF
call :COUNT
if "!COUNT!"=="0" (
  >"%CAND%" type nul
  for /r "%ROOT%" %%F in (*.exe) do (
    set "DP=%%~dpF"
    echo(!DP!|%SystemRoot%\System32\findstr.exe /i /l /c:"\Binaries\Win64\" >nul 2>&1
    if not errorlevel 1 >>"%CAND%" echo %%~fF
  )
  call :COUNT
)
if "!COUNT!"=="0" (
  >"%CAND%" type nul
  for %%F in ("%ROOT%*.exe") do if exist "%%~fF" >>"%CAND%" echo %%~fF
  call :COUNT
)
if "!COUNT!"=="0" (
  echo [ERREUR] Aucun executable de rendu plausible trouve.
  echo Place le paquet a la racine du jeu ou a cote du vrai EXE de rendu.
  rem Non-destructive contract: temporary inventory file intentionally retained in TEMP.
  pause
  exit /b 2
)
if not "!COUNT!"=="1" (
  echo [ERREUR] Plusieurs executables plausibles ont ete trouves :
  type "%CAND%"
  echo Installation refusee pour ne jamais choisir un launcher arbitrairement.
  rem Non-destructive contract: temporary inventory file intentionally retained in TEMP.
  pause
  exit /b 3
)
set /p "TARGET_EXE="<"%CAND%"
for %%A in ("!TARGET_EXE!") do (
  set "TARGET_DIR=%%~dpA"
  set "TARGET_EXE_NAME=%%~nxA"
)
rem Non-destructive contract: temporary inventory file intentionally retained in TEMP.

echo [CIBLE EXE] !TARGET_EXE!
echo [LIAISON] TargetExe=!TARGET_EXE_NAME!
echo [STEP 01/08] Renderer selection ........ PASS
set "WIN81_NIS_TARGET_EXE=!TARGET_EXE!"

echo [STEP 02/08] PE x64 validation ......... RUN
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$p=$env:WIN81_NIS_TARGET_EXE;try{$fs=[IO.File]::OpenRead($p);$br=New-Object IO.BinaryReader($fs);$fs.Position=0x3c;$pe=$br.ReadInt32();$fs.Position=$pe+4;$m=$br.ReadUInt16();$br.Close();if($m -eq 0x8664){exit 0}else{exit 2}}catch{exit 3}" >nul 2>&1
if errorlevel 1 (
  echo [STEP 02/08] PE x64 validation ......... FAIL
  echo [ERREUR] La cible n'est pas un PE x64 valide.
  pause
  exit /b 4
)
echo [STEP 02/08] PE x64 validation ......... PASS

rem INSTALLFIX1: bounded check by selected executable process name only.
echo [STEP 03/08] Running-process check ..... RUN
set "WIN81_NIS_TARGET_PROCESS=!TARGET_EXE_NAME!"
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$n=[IO.Path]::GetFileNameWithoutExtension($env:WIN81_NIS_TARGET_PROCESS);try{$p=Get-Process -Name $n -ErrorAction SilentlyContinue;if($null -ne $p){exit 9}else{exit 0}}catch{exit 10}" >nul 2>&1
set "PROC_CHECK_RC=!ERRORLEVEL!"
if "!PROC_CHECK_RC!"=="9" (
  echo [STEP 03/08] Running-process check ..... FAIL
  echo [ERREUR] Un processus nomme !TARGET_EXE_NAME! est encore ouvert.
  echo Ferme completement le jeu puis relance l'installation.
  pause
  exit /b 9
)
if not "!PROC_CHECK_RC!"=="0" (
  echo [STEP 03/08] Running-process check ..... FAIL
  echo [ERREUR] Impossible de verifier proprement si !TARGET_EXE_NAME! est ouvert.
  echo Aucune installation n'a ete effectuee.
  pause
  exit /b 11
)
echo [STEP 03/08] Running-process check ..... PASS

rem INSTALLFIX1: bounded stale-proxy audit.
rem Never recurse through the complete game tree for d3d11.dll.
rem TARGET_DIR\d3d11.dll is still validated by the original TARGET_PRECHECK.
echo [STEP 04/08] Bounded proxy audit ........ RUN
set "STALE_COUNT=0"
set "ROOT_DLL=!ROOT!d3d11.dll"
set "TARGET_DLL=!TARGET_DIR!d3d11.dll"
rem INSTALLFIX3: canonicalize both file paths before deciding ROOT is external to TARGET_DIR.
for %%I in ("!ROOT_DLL!") do set "ROOT_DLL=%%~fI"
for %%I in ("!TARGET_DLL!") do set "TARGET_DLL=%%~fI"
if /i not "!ROOT_DLL!"=="!TARGET_DLL!" if exist "!ROOT_DLL!" (
  set "MATCH=0"
  fc /b "!ROOT_DLL!" "!ROOT!win81_nis_dx11_x64.dll" >nul 2>&1
  if not errorlevel 1 set "MATCH=1"
  if "!MATCH!"=="0" for %%R in ("!ROOT!diag\r\*.dll") do (
    if exist "%%~fR" if "!MATCH!"=="0" (
      fc /b "!ROOT_DLL!" "%%~fR" >nul 2>&1
      if not errorlevel 1 set "MATCH=1"
    )
  )
  if "!MATCH!"=="1" >>"!STALE!" echo(!ROOT_DLL!
)
for /f %%N in ('%SystemRoot%\System32\find.exe /v /c "" ^< "!STALE!"') do set "STALE_COUNT=%%N"
if not "!STALE_COUNT!"=="0" (
  echo.
  echo [IMPORTANT] Ancienne copie Win81 NIS detectee a la racine du paquet :
  type "!STALE!"
  echo Elle sera renommee sans suppression.
)
echo [STEP 04/08] Bounded proxy audit ........ PASS

echo.
echo P1FG7N-VBLANK2-PTARFG1B18K18 derive de la base B18K17 fournie et reconstruite bit-identique. Le moteur temporel/NVENC/VBlank est preserve ; seul le spatial devient PTAR-NG MoE v01. CTRL+F9 capture la texture finale REAL+GENERATED en NV12 960x540 puis l alimente vers Intel Quick Sync H.264 via un worker et un pipe bornes. Le FG ne doit jamais attendre le recorder.
echo Pendant la Frame Generation, le profil PTAR utilise PTAR-NG MoE v01 sur toutes les frames REAL et GENERATED admissibles en x1.5. Toute geometrie non valide bascule en BILINEAR; NIS n est jamais un fallback PTAR.
echo PTAR-NG MoE v01 conserve son empreinte validee : 1 GatherGreen + 4 SampleLevel, un seul pixel pass, aucun UAV, texture intermediaire, compute pass ni nouveau Flush/wait/gate NVENC.
echo Hors Frame Generation, le presenter USR utilise lui aussi PTAR en x1.5 et BILINEAR hors contrat.
echo Si PTAR ne peut pas etre cree ou si le facteur n est pas exactement x1.5, le temporal reste actif en BILINEAR fail-open.
echo Le thread display C conserve le WaitForVBlank valide ; le chemin de production evite le marqueur et le timing QPC VBlank tant que le diagnostic est OFF.
echo LAB DWMPHASE2 AUTOCOLLECT1 conserve exactement RC18 PRESENTDELIVERY1, echantillonne passivement la phase DWM toutes les 8 frames par type et sauvegarde automatiquement les preuves au prochain F8. RC17 conserve les 4 profils et renforce uniquement CONSERVATIVE avec un garde de discontinuite des vecteurs de mouvement. CTRL+F8 devient un menu: premier appui = afficher le nom; nouvel appui pendant l affichage = profil suivant. Un changement de tier ME /3-/2 pendant FG demande toujours CTRL+F6 OFF/ON; aucun rebuild FG automatique n est declenche.
echo Tout autre processus chargeant cette DLL reste en pass-through D3D11 strict.
echo La resolution est choisie dans le jeu. Le presenter conserve son ratio par ASPECT FIT.
echo F10 bascule le chemin de sortie et affiche toujours son etat.
echo Le jeu est maintenu en mode fenetre ; toute demande de plein ecran exclusif est neutralisee.
echo Le presenter reste plein ecran natif. CTRL+F6 bascule la generation de frames ; CTRL+F11 gere le HUD.
echo Les raccourcis existants restent preserves. RC17 reserve CTRL+F8 au menu de qualite FG ; F8 garde le statut et DWMPHASE2 lui ajoute seulement un dump diagnostic apres le statut.
set "ANSWER="
set /p "ANSWER=Continuer ? [O/N] : "
if /i not "!ANSWER!"=="O" (
  rem Non-destructive contract: temporary inventory file intentionally retained in TEMP.
  echo [INFO] Installation annulee.
  pause
  exit /b 0
)

echo [STEP 05/08] Target proxy precheck ...... RUN
set "TARGET_PRECHECK=0"
if not exist "!TARGET_DIR!d3d11.dll" set "TARGET_PRECHECK=1"
if exist "!TARGET_DIR!d3d11.dll" (
  fc /b "!TARGET_DIR!d3d11.dll" "%ROOT%win81_nis_dx11_x64.dll" >nul 2>&1
  if not errorlevel 1 set "TARGET_PRECHECK=1"
  if "!TARGET_PRECHECK!"=="0" for %%R in ("%ROOT%diag\r\*.dll") do (
    if exist "%%~fR" if "!TARGET_PRECHECK!"=="0" (
      fc /b "!TARGET_DIR!d3d11.dll" "%%~fR" >nul 2>&1
      if not errorlevel 1 set "TARGET_PRECHECK=1"
    )
  )
)
if "!TARGET_PRECHECK!"=="0" (
  echo [STEP 05/08] Target proxy precheck ...... FAIL
  echo [ERREUR] Le d3d11.dll du dossier cible est inconnu. Aucune copie n'a ete neutralisee.
  rem Non-destructive contract: temporary inventory file intentionally retained in TEMP.
  pause
  exit /b 5
)
echo [STEP 05/08] Target proxy precheck ...... PASS

>"%QUAR_FILE%" type nul
set "QUAR_FAIL=0"
for /f "usebackq delims=" %%D in ("%STALE%") do call :QUARANTINE "%%D"
rem Non-destructive contract: temporary inventory file intentionally retained in TEMP.
if "!QUAR_FAIL!"=="1" (
  echo [ERREUR] Au moins une ancienne copie n'a pas pu etre neutralisee. Installation arretee.
  pause
  exit /b 10
)

echo [STEP 06/08] Backup/deploy proxy ........ RUN
set "KNOWN=0"
set "DID_BACKUP=0"
if exist "!TARGET_DIR!d3d11.dll" (
  fc /b "!TARGET_DIR!d3d11.dll" "%ROOT%win81_nis_dx11_x64.dll" >nul 2>&1
  if not errorlevel 1 (
    echo [OK] V0.41-P1FG7N-VBLANK2-PTARFG1B18K18 deja installee.
    >"%TARGET_FILE%" echo !TARGET_DIR!
    >"%TARGET_EXE_FILE%" echo !TARGET_EXE!
    echo [STEP 06/08] Backup/deploy proxy ........ PASS ^(already installed^)
    goto :MERGE_INI
  )
  for %%R in ("%ROOT%diag\r\*.dll") do (
    if exist "%%~fR" (
      fc /b "!TARGET_DIR!d3d11.dll" "%%~fR" >nul 2>&1
      if not errorlevel 1 set "KNOWN=1"
    )
  )
  if "!KNOWN!"=="0" (
    echo [ERREUR] Un d3d11.dll inconnu existe deja dans le dossier cible.
    echo Aucun fichier inconnu n'a ete ecrase.
    pause
    exit /b 5
  )
  set "OLD_BACKUP="
  for /l %%N in (1,1,16) do if not defined OLD_BACKUP if not exist "!TARGET_DIR!d3d11.win81nis_previous_%%N.dll" set "OLD_BACKUP=!TARGET_DIR!d3d11.win81nis_previous_%%N.dll"
  if not defined OLD_BACKUP (
    echo [ERREUR] Aucun nom de sauvegarde disponible.
    pause
    exit /b 6
  )
  copy /y "!TARGET_DIR!d3d11.dll" "!OLD_BACKUP!" >nul
  if errorlevel 1 (
    echo [ERREUR] Sauvegarde impossible.
    pause
    exit /b 6
  )
  >"%BACKUP_FILE%" echo !OLD_BACKUP!
  set "DID_BACKUP=1"
)
if "!DID_BACKUP!"=="0" >"%BACKUP_FILE%" echo NONE

copy /y "%ROOT%win81_nis_dx11_x64.dll" "!TARGET_DIR!d3d11.dll" >nul
if errorlevel 1 (
  echo [ERREUR] Copie impossible. Verifie que le jeu et le launcher sont fermes.
  pause
  exit /b 7
)
fc /b "!TARGET_DIR!d3d11.dll" "%ROOT%win81_nis_dx11_x64.dll" >nul 2>&1
if errorlevel 1 (
  echo [ERREUR] Verification binaire apres copie echouee.
  pause
  exit /b 8
)
>"%TARGET_FILE%" echo !TARGET_DIR!
>"%TARGET_EXE_FILE%" echo !TARGET_EXE!
echo [STEP 06/08] Backup/deploy proxy ........ PASS

:MERGE_INI
echo [STEP 07/08] Merge runtime INI .......... RUN
rem B18K18 optional QSV helper: additive copy only; never overwrite an existing helper.
if exist "%ROOT%tools\qsv\b18k17_ffmpeg.exe" (
  if not exist "!TARGET_DIR!tools" mkdir "!TARGET_DIR!tools" >nul 2>&1
  if not exist "!TARGET_DIR!tools\qsv" mkdir "!TARGET_DIR!tools\qsv" >nul 2>&1
  if not exist "!TARGET_DIR!tools\qsv\b18k17_ffmpeg.exe" (
    copy "%ROOT%tools\qsv\b18k17_ffmpeg.exe" "!TARGET_DIR!tools\qsv\b18k17_ffmpeg.exe" >nul
    if errorlevel 1 echo [ATTENTION] Le helper QSV local n a pas pu etre copie. Lance INSTALL_B18K18_QSV_ENCODER_WIN81.bat apres installation.
  )
)
set "KEEP_ENABLED=1"
set "KEEP_USR=1"
set "KEEP_RECOVERY=1"
set "KEEP_EXTERNAL_CAPTURE_STABLE=1"
set "KEEP_OVERLAY=1"
set "KEEP_FG_ENABLED=0"
set "KEEP_FG_QUALITY=2"
set "KEEP_FG_REQUIRE_VSYNC=0"
set "KEEP_FG_ASYNC=1"
set "KEEP_FG_SEPARATE_DEVICE=1"
set "KEEP_FG_SHARED_FLUSH=1"
set "KEEP_FG_TARGET_FPS=60"
set "KEEP_FG_PRESENT_SYNC=1"
set "KEEP_FG_VECTOR_SIGN=1"
set "KEEP_FG_BLEND_GUARD=35"
set "KEEP_FG_DISABLE_ERRORS=3"
set "KEEP_FG_ME_SCALE=50"
set "KEEP_FG_ME_BUDGET=6000"
set "KEEP_FG_ADAPTIVE=1"
set "KEEP_FILTER=2"
set "KEEP_HK_MANUAL=F6"
set "KEEP_HK_BENCH=F7"
set "KEEP_HK_STATUS=F8"
set "KEEP_HK_CAPTURE=F9"
set "KEEP_HK_VIDEO=CTRL+F9"
set "KEEP_HK_PRESENTER=F10"
set "KEEP_HK_HUD=CTRL+F11"
set "KEEP_HK_FG=CTRL+F6"
set "KEEP_HK_NEXT=F12"
set "KEEP_INPUT_DIAG=0"
set "KEEP_VBLANK_DIAG=0"
set "KEEP_FG_NIS_AB_MODE=1"
set "KEEP_VIDEO_PROFILE=3"
set "KEEP_VIDEO_FPS=60"
set "KEEP_VIDEO_PROBE_W=1280"
set "KEEP_VIDEO_PROBE_H=720"
set "KEEP_VIDEO_QSV_KBPS=5000"
set "KEEP_VIDEO_SEG_MB=256"
set "KEEP_VIDEO_SEG_COUNT=4"
if exist "!TARGET_DIR!win81_nis.ini" (
  if not exist "!TARGET_DIR!win81_nis.pre_p1fg7n.bak" copy /y "!TARGET_DIR!win81_nis.ini" "!TARGET_DIR!win81_nis.pre_p1fg7n.bak" >nul
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"Enabled=[01]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_ENABLED=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"UniversalSpatialPresenter=[01]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_USR=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"AutoRecovery=[01]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_RECOVERY=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"ExternalCaptureStable=[01]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_EXTERNAL_CAPTURE_STABLE=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"Overlay=[01]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_OVERLAY=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"FrameGeneration=[01]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_ENABLED=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"FrameGenerationQuality=[0-3]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_QUALITY=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"FrameGenerationRequireVSync=[01]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_REQUIRE_VSYNC=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"FrameGenerationAsync=[01]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_ASYNC=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"FrameGenerationPresentSync=[0-4]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_PRESENT_SYNC=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"FrameGenerationTargetFPS=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_TARGET_FPS=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"FrameGenerationVectorSign=-*[1]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_VECTOR_SIGN=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"FrameGenerationBlendGuard=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_BLEND_GUARD=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"FrameGenerationDisableAfterErrors=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_DISABLE_ERRORS=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"FrameGenerationMEScalePercent=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_ME_SCALE=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"FrameGenerationMEBudgetUs=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_ME_BUDGET=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"FrameGenerationAdaptiveDeadline=[01]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FG_ADAPTIVE=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"F12QualityProfile=[0-2]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_FILTER=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"VideoRecordProfile=[1-3]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_VIDEO_PROFILE=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"VideoRecordFPS=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_VIDEO_FPS=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"VideoProbeWidth=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_VIDEO_PROBE_W=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"VideoProbeHeight=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_VIDEO_PROBE_H=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"VideoQsvBitrateKbps=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_VIDEO_QSV_KBPS=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"VideoRecordSegmentMB=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_VIDEO_SEG_MB=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"VideoRecordSegmentCount=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_VIDEO_SEG_COUNT=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"ManualFilter=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_HK_MANUAL=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"Benchmark=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_HK_BENCH=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"Status=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_HK_STATUS=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"Capture=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_HK_CAPTURE=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"VideoRecord=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_HK_VIDEO=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"TogglePresenter=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_HK_PRESENTER=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"ToggleHUD=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_HK_HUD=%%B"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /r /x /c:"FrameGeneration=F[1-9]" /c:"FrameGeneration=F1[0-9]" /c:"FrameGeneration=F2[0-4]" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_HK_FG=%%B"
  if /i "!KEEP_HK_HUD!"=="F11" set "KEEP_HK_HUD=CTRL+F11"
  rem P1FG7N safety contract: exact P1FG7L topology/pressure and J strong load-shed preserved; completed NVENC pair memory may only extend synthetic shedding; startup OFF; persistent telemetry; fail-open P1U46.
  set "KEEP_FG_ENABLED=0"
  set "KEEP_FG_REQUIRE_VSYNC=0"
  set "KEEP_FG_ASYNC=1"
  set "KEEP_FG_SEPARATE_DEVICE=1"
  set "KEEP_FG_SHARED_FLUSH=1"
  set "KEEP_FG_PRESENT_SYNC=1"
  set "KEEP_FG_ADAPTIVE=1"
  set "KEEP_FG_TARGET_FPS=60"
  set "KEEP_FG_ME_BUDGET=6000"
  set "KEEP_HK_FG=CTRL+F6"
  for /f "tokens=1,* delims==" %%A in ('%SystemRoot%\System32\findstr.exe /i /b /c:"FilterNext=" "!TARGET_DIR!win81_nis.ini"') do set "KEEP_HK_NEXT=%%B"
 )
if not "!KEEP_FG_QUALITY!"=="0" if not "!KEEP_FG_QUALITY!"=="1" if not "!KEEP_FG_QUALITY!"=="2" if not "!KEEP_FG_QUALITY!"=="3" set "KEEP_FG_QUALITY=2"
rem RC17: named quality profile remains authoritative. Q3 adds motion-discontinuity trust stabilization; legacy Guard/MEScale keys remain synchronized for readable diagnostics only.
if "!KEEP_FG_QUALITY!"=="0" set "KEEP_FG_BLEND_GUARD=65"
if "!KEEP_FG_QUALITY!"=="0" set "KEEP_FG_ME_SCALE=33"
if "!KEEP_FG_QUALITY!"=="1" set "KEEP_FG_BLEND_GUARD=50"
if "!KEEP_FG_QUALITY!"=="1" set "KEEP_FG_ME_SCALE=33"
if "!KEEP_FG_QUALITY!"=="2" set "KEEP_FG_BLEND_GUARD=35"
if "!KEEP_FG_QUALITY!"=="2" set "KEEP_FG_ME_SCALE=50"
if "!KEEP_FG_QUALITY!"=="3" set "KEEP_FG_BLEND_GUARD=25"
if "!KEEP_FG_QUALITY!"=="3" set "KEEP_FG_ME_SCALE=50"
rem RECORDERFIX1: production contract is profile 3. Do not inherit profile 1/2 from an older target INI.
set "KEEP_VIDEO_PROFILE=3"
if not "!KEEP_VIDEO_FPS!"=="30" if not "!KEEP_VIDEO_FPS!"=="60" set "KEEP_VIDEO_FPS=60"
if not defined KEEP_VIDEO_QSV_KBPS set "KEEP_VIDEO_QSV_KBPS=5000"
for /f "delims=0123456789" %%Z in ("!KEEP_VIDEO_QSV_KBPS!") do set "KEEP_VIDEO_QSV_KBPS=5000"
if !KEEP_VIDEO_QSV_KBPS! LSS 2000 set "KEEP_VIDEO_QSV_KBPS=2000"
if !KEEP_VIDEO_QSV_KBPS! GTR 30000 set "KEEP_VIDEO_QSV_KBPS=30000"
set "KEEP_SCALER=2"
set "KEEP_SHARPNESS=25"
if "!KEEP_FILTER!"=="0" set "KEEP_SCALER=0"
if "!KEEP_FILTER!"=="0" set "KEEP_SHARPNESS=0"
if "!KEEP_FILTER!"=="1" set "KEEP_SCALER=1"
if "!KEEP_FILTER!"=="1" set "KEEP_SHARPNESS=0"
if "!KEEP_FILTER!"=="2" set "KEEP_SCALER=2"
if "!KEEP_FILTER!"=="2" set "KEEP_SHARPNESS=0"

>"!TARGET_DIR!win81_nis.ini" echo ; Win81 Universal Spatial Reconstruction v0.41-P1FG7N-VBLANK2-PTARFG1B18K18-PROFILE123 - PTAR-NG MoE v01 + selectable QSV recorder profile 1/2/3
>>"!TARGET_DIR!win81_nis.ini" echo ; F12 0=POINT ^(FASTEST^), 1=BILINEAR, 2=PTAR-NG MOE V01 X1.5
>>"!TARGET_DIR!win81_nis.ini" echo [WIN81_NIS]
>>"!TARGET_DIR!win81_nis.ini" echo ; Politique P1FG7N : P1FG7L intact ; hysteresis WORKER_SUBMIT / MAILBOX_FLUSH inchangee ; paire NVENC de 50ms ou plus dans 3 sequences ou moins peut etendre REAL_ONLY 6/10/15 ; evenement isole ignore par N ; vraies frames prioritaires ; P1U46 restaure a l arret.
>>"!TARGET_DIR!win81_nis.ini" echo TargetExe=!TARGET_EXE_NAME!
>>"!TARGET_DIR!win81_nis.ini" echo Enabled=!KEEP_ENABLED!
>>"!TARGET_DIR!win81_nis.ini" echo UniversalSpatialPresenter=!KEEP_USR!
>>"!TARGET_DIR!win81_nis.ini" echo AutoRecovery=!KEEP_RECOVERY!
>>"!TARGET_DIR!win81_nis.ini" echo ExternalCaptureStable=1
(
 echo PresenterExclusive=0
 echo GraphicsCartographer=0
 echo RootAuthority=0
 echo AuthorityInternalScale=0
 echo OutputWidth=1920
 echo OutputHeight=1080
 echo RenderWidth=1280
 echo RenderHeight=720
 echo F12ProfilePct=0
 echo F12QualityProfile=!KEEP_FILTER!
 echo Sharpness=!KEEP_SHARPNESS!
 echo ScalerMode=!KEEP_SCALER!
 echo Overlay=!KEEP_OVERLAY!
 echo FrameGenerationQuality=!KEEP_FG_QUALITY!
 echo FrameGeneration=!KEEP_FG_ENABLED!
 echo FrameGenerationRequireVSync=!KEEP_FG_REQUIRE_VSYNC!
 echo FrameGenerationAsync=!KEEP_FG_ASYNC!
 echo FrameGenerationSeparateDevice=!KEEP_FG_SEPARATE_DEVICE!
 echo FrameGenerationSharedFlush=!KEEP_FG_SHARED_FLUSH!
 echo FrameGenerationPresentSync=1
 echo FrameGenerationTargetFPS=!KEEP_FG_TARGET_FPS!
 echo FrameGenerationVectorSign=!KEEP_FG_VECTOR_SIGN!
 echo FrameGenerationBlendGuard=!KEEP_FG_BLEND_GUARD!
 echo FrameGenerationDisableAfterErrors=!KEEP_FG_DISABLE_ERRORS!
 echo FrameGenerationMEScalePercent=!KEEP_FG_ME_SCALE!
 echo FrameGenerationMEBudgetUs=!KEEP_FG_ME_BUDGET!
 echo FrameGenerationAdaptiveDeadline=1
 echo ; RC17 FGQUALITY3: profile 3 adds motion-discontinuity trust stabilization; FrameGenerationQuality stays authoritative.
 echo NativeLowResSwapChain=0
 echo ExclusivePerformanceMode=0
 echo LogicalResolutionForce=0
 echo InternalFamilyScaler=0
 echo LockOutputTarget=0
 echo RTDiagnostics=0
 echo PerfTelemetry=0
 echo VBlankDiagnostics=0
 echo NativeInputEnvelope=1
 echo BenchmarkWarmupSeconds=3
 echo BenchmarkDurationSeconds=10
 echo ; ============================================================
 echo ; PTAR VIDEO RECORDER PROFILE - changer UNE seule valeur :
 echo ; 1 = QUALITY  : 1920x1080 / 30 FPS / 16000 kbps
 echo ; 2 = MOTION   : 1600x900  / 60 FPS / 17000 kbps
 echo ; 3 = COMBINED : 1920x1080 / 60 FPS / 22000 kbps
 echo ; COMBINED conserve la pleine qualite PTAR + les 60 FPS mais charge davantage le recorder/QSV.
 echo VideoRecordProfile=!KEEP_VIDEO_PROFILE!
 echo ; ============================================================
 echo ; Legacy segment keys preserved for rollback/history; PROFILE123 ignores old width/FPS/bitrate keys.
 echo VideoRecordSegmentMB=!KEEP_VIDEO_SEG_MB!
 echo VideoRecordSegmentCount=!KEEP_VIDEO_SEG_COUNT!
)>>"!TARGET_DIR!win81_nis.ini"
(
 echo.
 echo [HOTKEYS]
 echo ; RC17 CTRL+F8 menu: first press shows the profile name; repeat while visible selects next; F8 remains Status.
 echo ManualFilter=!KEEP_HK_MANUAL!
 echo Benchmark=!KEEP_HK_BENCH!
 echo Status=!KEEP_HK_STATUS!
 echo Capture=!KEEP_HK_CAPTURE!
 echo VideoRecord=!KEEP_HK_VIDEO!
 echo TogglePresenter=!KEEP_HK_PRESENTER!
 echo ToggleHUD=!KEEP_HK_HUD!
 echo FrameGeneration=!KEEP_HK_FG!
 echo FilterNext=!KEEP_HK_NEXT!
 echo.
 echo [INPUT]
 echo Diagnostics=0
 echo CoordinateMapping=Off
)>>"!TARGET_DIR!win81_nis.ini"
echo [F12] Profil : !KEEP_FILTER! ^(0 POINT, 1 BILINEAR, 2 PTAR-NG MOE V01 X1.5^)
echo [TOUCHES] CTRL+F6 generation de frames ; CTRL+F8 qualite FG ; F8 statut ; F9 capture BMP ; CTRL+F9 recorder QSV MP4 ; CTRL+F11 HUD.
echo [VIDEO] VideoRecordProfile=!KEEP_VIDEO_PROFILE! ^(1 QUALITY, 2 MOTION, 3 COMBINED^) - CADENCEFIX1 60FPS phase smoothing
echo [AFFICHAGE] Plein ecran exclusif neutralise ; jeu force en fenetre.
echo [SOURIS] INPUTMAP2 remappe uniquement les messages souris client vers le raster jeu ; Raw Input et GAME DIRECT restent intacts.
echo [FG REAL] FGREAL30 cadence les frames REAL a la moitie de FrameGenerationTargetFPS uniquement quand le chemin FG est actif ; FG OFF reste libre.
echo [FG QUALITY] FGQUALITY2 profil startup !KEEP_FG_QUALITY! : 0 LEGACY, 1 BALANCED, 2 QUALITY, 3 CONSERVATIVE. CTRL+F8 cycle en session.
echo [FG HUD] CTRL+F8 affiche CURR profil actif / SEL profil selectionne / P pending pendant 3 secondes.
echo [STEP 07/08] Merge runtime INI .......... PASS
echo [STEP 08/08] Finalize install markers ... RUN

rem Rotate the previous log so the next file must identify P1FG7N.
if exist "!TARGET_DIR!win81_nis.log" (
  set "OLDLOG="
  for /l %%N in (1,1,16) do if not defined OLDLOG if not exist "!TARGET_DIR!win81_nis.pre_p1fg7n_%%N.log" set "OLDLOG=!TARGET_DIR!win81_nis.pre_p1fg7n_%%N.log"
  if defined OLDLOG move /y "!TARGET_DIR!win81_nis.log" "!OLDLOG!" >nul
)
>"!TARGET_DIR!win81_nis_version.txt" echo WIN81_NIS_VERSION=P1FG7N-PRESENTDELIVERY1-DWMPHASE2-AUTOCOLLECT1-PTARFG1B18K18-FULLSTACK1
>>"!TARGET_DIR!win81_nis_version.txt" echo TARGET_EXE=!TARGET_EXE_NAME!
>>"!TARGET_DIR!win81_nis_version.txt" echo PRESENTATION_MAPPING=ASPECT_FIT
>>"!TARGET_DIR!win81_nis_version.txt" echo TEMPORAL_BASE=B14_EXACT_POLICIES_PRESERVED
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_MODE=NVENC_ME_ASYNC_EVENT_RING
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_TOPOLOGY=GAME_PLUS_DEDICATED_NVENC_PUBLISH_PLUS_ISOLATED_DISPLAY_SAME_ADAPTER
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_ME_PROFILE=SELECTABLE_FGQUALITY3_DIV3_OR_DIV2
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_NVENC_JOBS=4
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_DISPLAY_MAILBOX=4_SYNC_SHARED_R8G8B8A8_KEYED_PRIMARY_LEGACY_FALLBACK
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_PACING=QPC_TARGET_60HZ_SELECTION_PLUS_DXGI_PRESENT_SYNC1
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_PRESENT=SYNC1_FLAGS0_ISOLATED_DISPLAY_THREAD
 >>"!TARGET_DIR!win81_nis_version.txt" echo FG_VISIBLE_DELIVERY=PRESENTDELIVERY1_PREWAIT_REMOVED_SYNC1
 >>"!TARGET_DIR!win81_nis_version.txt" echo FG_DWM_PHASE_DIAG=DWMPHASE2_SAMPLE_EVERY_8_G_AND_R_AUTOCOLLECT1
 >>"!TARGET_DIR!win81_nis_version.txt" echo FG_DWM_PHASE_HOTKEY=CTRL_F4_SNAPSHOT_HUD_DWM_LAB
 >>"!TARGET_DIR!win81_nis_version.txt" echo FG_DWM_PHASE_AUTODUMP=F8_STATUS_EDGE_TO_LOG
 >>"!TARGET_DIR!win81_nis_version.txt" echo FG_VISIBLE_RESULT_CAPTURE=PTAR_VISIBLE_VERIFIER_LAST_OUTPUT_TXT
 >>"!TARGET_DIR!win81_nis_version.txt" echo FG_VISIBLE_TEST_HUD_HOTKEY=CTRL_F5_HUD_VISIB_TEST
 >>"!TARGET_DIR!win81_nis_version.txt" echo DWM_TIMING_INFO_SIZE=292_PACK4
 >>"!TARGET_DIR!win81_nis_version.txt" echo DWM_TIMING_HWND=NULL_WINDOWS_8_1
 >>"!TARGET_DIR!win81_nis_version.txt" echo FG_PRE_PRESENT_WAITFORVBLANK=DISABLED_BY_RC18_PATCH
 >>"!TARGET_DIR!win81_nis_version.txt" echo FG_PRESENT_CALL=IDXGISWAPCHAIN_PRESENT_SYNCINTERVAL_1_FLAGS_0
 >>"!TARGET_DIR!win81_nis_version.txt" echo RC18_FIELD_BASIS=VISIBLE_REAL_15_578_GENERATED_0_793_UNIQUE_16_372_HUD_APPROX_30
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_SPATIAL=PTAR_NG_MOE_V01_EXACT_X1_5_REAL_PLUS_GENERATED
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_PTAR_DXBC=EMBEDDED_VALIDATED
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_PTAR_FETCH_BUDGET=1_GATHERGREEN_PLUS_4_SAMPLELEVEL
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_PTAR_PASSES=ONE_PIXEL_PASS
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_PTAR_UAV=0
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_PTAR_INTERMEDIATE_TEXTURES=0
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_FAIL_OPEN=BILINEAR_KEEP_FG_ACTIVE
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_HUD_EFFECTIVE_FILTER=PTAR_X15_OR_BILINEAR_FAIL_OPEN
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_NIS_RUNTIME=RETIRED_ZERO_DISPATCH
>>"!TARGET_DIR!win81_nis_version.txt" echo F7_FG_AUDIT=DEVICE_C_ASYNC_TIMESTAMP_TYPE_BALANCED_REAL_GENERATED_STRIDE8_PER_CLASS_DONOTFLUSH
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_F9_CAPTURE=POST_SPLIT_SPATIAL_POST_HUD_ISOLATED_NATIVE_PRESENTER
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_F9_CAPTURE_NORMAL_PATH_COST=ZERO_WHEN_NOT_REQUESTED
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_F9_CAPTURE_IMPLEMENTATION=EXPLICIT_STAGING_READBACK_AND_BMP_ON_REQUEST
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_LOAD_SHED=ADAPTIVE_SYNTHETIC_ONLY_REAL_PRIORITY
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_NVENC_ADMISSION_AGE_US=30000
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_NVENC_ADMISSION_MAX_INFLIGHT=2
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_NVENC_RECOVERY=POST_COMPLETION_ADAPTIVE_SERIALIZATION
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_NVENC_RECOVERY_WINDOWS=12_30_60_SOURCE_FRAMES
>>"!TARGET_DIR!win81_nis_version.txt" echo FRAME_GENERATION_NVENC_RECOVERY_MAX_INFLIGHT=1
>>"!TARGET_DIR!win81_nis_version.txt" echo DLL_SHA256=3d4d777c943ced0f475df1371d3a2f9eeb5eeb80c66e9fb217c4d91057f32453
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_ENGINE=FGQUALITY3_Q3_MOTION_DISCONTINUITY_STABILIZER
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_STARTUP_PROFILE=!KEEP_FG_QUALITY!
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_0=LEGACY_ME_DIV3_GUARD65_SATGAT_STYLE
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_1=BALANCED_ME_DIV3_GUARD50
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_2=QUALITY_ME_DIV2_GUARD35_RC13_BASELINE
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_3=CONSERVATIVE_ME_DIV2_GUARD25_MOTION_DISCONTINUITY_STABILIZER
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_STABILIZER_SCOPE=PROFILE3_ONLY_GUARD_LT_030
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_STABILIZER_NEIGHBORS=RIGHT_PLUS_DOWN_MV_DISCONTINUITY
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_STABILIZER_POLICY=SQUARED_TRUST_ATTENUATION_TO_TRUTH_BLEND
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_HOTKEY=CTRL_F8_SHOW_THEN_CYCLE_WHILE_NOTICE_VISIBLE
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_MENU_FIRST_PRESS=SHOW_SELECTED_NAME_NO_CHANGE
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_MENU_REPEAT_VISIBLE=NEXT_PROFILE_AND_REFRESH
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_MENU_EXPIRED=SHOW_ONLY_AGAIN
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_HOT_SWITCH=BLEND_GUARD_LIVE_ME_TIER_ON_NEXT_FG_CONSTRUCTION
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_CROSS_TIER_POLICY=NO_AUTOMATIC_FG_REBUILD_CTRL_F6_OFF_ON_REQUIRED_WHILE_ACTIVE
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_F8_STATUS=PROFILE_LOGGED_AFTER_EXISTING_STATUS
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_HUD=QUALITYMENU1_PROFILE_NAME_ONLY_NOTICE
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_HUD_SCOPE=RUNTIME_ONLY_NO_STARTUP_PARSER_WRAPPER
>>"!TARGET_DIR!win81_nis_version.txt" echo RC17_STARTUP_PARSER_POLICY=BYTE_IDENTICAL_TO_RC16_AND_RC14_NO_EARLY_HUD_WRAPPER
>>"!TARGET_DIR!win81_nis_version.txt" echo RC18_PRESENTATION_POLICY=PRESENTDELIVERY1_SYNC1_NO_PREWAIT
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_NOTICE_ID=8
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_NOTICE_DURATION_MS=3000
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_NOTICE_LINE1=PROFILE_NAME_ONLY
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_NOTICE_LINE2=NONE
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_ACTIVE_TRACKING=LAST_FULLY_APPLIED_PROFILE
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_QUALITY_SATGAT_NOTE=PROFILE0_RETAINS_HISTORICAL_PARAMETERS_AND_Q3_STABILIZER_IS_DISABLED
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_SHARED_TRANSPORT=KEYED_PRIMARY_LEGACY_SHARED_END_TO_END_FALLBACK
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_SHARED_TRANSPORT_FALLBACK=E_INVALIDARG_EMPTY_TRANSPORT_SHAREDFLUSH_THROUGH_MAILBOX
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_SHARED_TRANSPORT_LEGACY_SYNC=CPU_SLOT_STATE_PLUS_EXISTING_TRANSPORT_AND_MAILBOX_FLUSH
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_SHARED_TRANSPORT_SCOPE=GAME_WORKER_TRANSPORT_PLUS_DISPLAY_MAILBOX
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_SHARED_TRANSPORT_RESET=MAILBOX_RETURN_PLUS_PRIMARY_PRECLEAR
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_SHARED_RC10_STATUS=REJECTED_MAILBOX_E_INVALIDARG_AND_FIELD_CRASH
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_SHARED_TRANSPORT_SATGAT_POLICY=KEYED_PRIMARY_FIRST_EVERY_ACTIVATION_NO_FALLBACK_ON_SUCCESS
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_SHARED_RC11_STATUS=REJECTED_GTX960M_RESIZEBUFFERS_TEARDOWN_REBUILD_CRASH
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_SHARED_FIELD_MACHINE=WINDOWS_8_1_X64_GTX_960M
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_SHARED_LEGACY_RUNTIME_MARKER=MAILBOX_STAGE_FALLBACK_FLAG_TO_LEGACY_ACTIVE
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_RESIZE_GUARD=RESIZEGUARD1_LEGACY_ACTIVE_AND_SCHEDULER_ONLY
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_RESIZE_GUARD_RESULT=DXGI_ERROR_INVALID_CALL_NO_RESOURCE_MUTATION
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_RESIZE_GUARD_KEYED_POLICY=ORIGINAL_RESIZEBUFFERS_PASS_THROUGH
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_RESIZE_GUARD_OFF_POLICY=ORIGINAL_RESIZEBUFFERS_PASS_THROUGH
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_SHARED_PROBE_FIELD_RESULT=0xE3000003_NORMAL_PLUS_LEGACY_SHARED
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_REAL_SOURCE_GOVERNOR=HALF_TARGET_QPC_RUNTIME_ENABLED_AND_SCHEDULER_ONLY
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_REAL_SOURCE_GATE=RUNTIME_ENABLED_AND_SCHEDULER_RUNNING
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_REAL_SOURCE_OFF_POLICY=PASS_THROUGH_NO_PACING
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_AUTO_VSYNC=NOT_IMPLEMENTED_RC17_FUTURE_CONTRACT_FG_ONLY
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_REAL_SOURCE_TARGET=FRAME_GENERATION_TARGET_FPS_DIV2
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_REAL_SOURCE_TARGET_AT_60=30
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_REAL_SOURCE_PACING=QPC_SLEEP1_ABOVE_2P5MS_SLEEP0_FINE
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_REAL_SOURCE_LATE_POLICY=IMMEDIATE_REAL_REBASE_NO_CATCHUP
>>"!TARGET_DIR!win81_nis_version.txt" echo FG_REAL_SOURCE_FAIL_OPEN=ORIGINAL_FGSUBMIT_ON_QPC_FAILURE
>>"!TARGET_DIR!win81_nis_version.txt" echo INQUISITOR_INPUTMAP=CLIENT_MOUSE_LPARAM_ASPECT_FIT_TO_REAL_GAME_BACKBUFFER
>>"!TARGET_DIR!win81_nis_version.txt" echo INQUISITOR_INPUTMAP_REV=INPUTMAP2_LPARAM64_PRESERVED
>>"!TARGET_DIR!win81_nis_version.txt" echo INQUISITOR_INPUTMAP_SCOPE=WM_MOUSEMOVE_BUTTON_MESSAGES_ONLY_GAME_DIRECT_PASSTHROUGH
>>"!TARGET_DIR!win81_nis_version.txt" echo INQUISITOR_INPUTMAP_RAW_INPUT=UNTOUCHED
>>"!TARGET_DIR!win81_nis_version.txt" echo INQUISITOR_INPUTMAP_WHEEL_SCREEN_COORDS=UNTOUCHED
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD=CTRL_F9_B18K18_FIXED_QPC_540P_INTEL_QSV_TIMELINE_TEST
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_DEFAULT_FPS=60
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_STORAGE=BOUNDED_RAM_QUEUE_TO_FFMPEG_STDIN_MP4
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_TARGET=960X540_NV12_LAYOUT_DEFAULT
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_STAGE_RING=8_Y_PLUS_UV_STAGING_PAIRS
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_PRESENTER_PATH=GPU_RGB_COPY_SHADER_Y_UV_STAGING_QUERY_DONOTFLUSH_MAP_DONOTWAIT
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_WORKER=RAM_NV12_LAYOUT_PACK_TO_32_FRAME_QUEUE
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_ENCODER=INTEL_QUICK_SYNC_H264_VIA_FFMPEG44_CHILD
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_QSV_QUEUE=32_FRAMES_DROP_ON_BACKPRESSURE
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_DEFAULT_BITRATE_KBPS=5000
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_FFMPEG_PRIORITY=BELOW_NORMAL
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_DISK_WRITES=FFMPEG_MP4_ONLY_LOW_PRIORITY_CHILD
>>"!TARGET_DIR!win81_nis_version.txt" echo VIDEO_RECORD_AUDIO=NONE_TEST_BRANCH
>>"!TARGET_DIR!win81_nis_version.txt" echo EXTERNAL_CAPTURE_STABILITY=STABLE_FG_PRESENTER_AUTO_TEARDOWN_GUARD
>>"!TARGET_DIR!win81_nis_version.txt" echo EXTERNAL_CAPTURE_TRACE=FINAL_PRESENTER_2S_HEARTBEAT_DXGI_EVENTS
>>"!TARGET_DIR!win81_nis_version.txt" echo EXTERNAL_CAPTURE_HOTKEY_CHANGES=CTRL_F8_RESERVED_FOR_FG_QUALITY

echo [STEP 08/08] Finalize install markers ... PASS
echo.
echo [OK] Installation LAB DWMPHASE2 AUTOCOLLECT1 sur base RC18 PRESENTDELIVERY1 terminee et verifiee.
echo [ENTREE] Aucun ancien wrapper reconnu ne reste actif hors de la cible.
echo [PROCESSUS] Seul !TARGET_EXE_NAME! peut activer les hooks; launcher/helpers restent en pass-through.
echo [RESOLUTION] Le jeu controle la resolution; F12 ne redimensionne rien.
echo [SPATIAL FG] PTAR-NG MoE v01 est utilise sur REAL et GENERATED en geometrie exacte x1.5 ; toute autre geometrie passe en BILINEAR fail-open.
echo [FG REAL] FGREAL30: cible 60 = 30 REAL/s avant interpolation ; desactivation CTRL+F6 retire immediatement ce governor.
echo [FG QUALITY] FrameGenerationQuality=0..3 ; CTRL+F8 cycle. Un passage /3-/2 pendant FG ne reconstruit jamais automatiquement le pipeline.
echo [HUD] Le profil F12 affiche PTAR pour le mode spatial x1.5 ; POINT et BILINEAR restent disponibles.
echo [VIDEO] F9 capture un BMP. CTRL+F9 demarre/arrete le recorder B18K18 : texture finale FG vers NV12 960x540 puis Intel Quick Sync H.264 vers recordings\*.mp4.
echo [QSV] Lance une fois INSTALL_B18K18_QSV_ENCODER_WIN81.bat avant le premier enregistrement et valide TEST_B18K18_QSV_ENCODER_5S.bat.
echo [PERF] Le FG garde la priorite absolue : ring GPU et queue RAM sont bornes ; la pression doit supprimer une frame VIDEO plutot que faire attendre le presenter. Le test materiel doit comparer DISPLAY FPS / GENERATED FPS avant et pendant REC.
echo [PRODUCTION] VBlankDiagnostics=0 par defaut.
echo [PREUVE] Au prochain lancement, la premiere ligne du nouveau win81_nis.log doit contenir P1FG7N-VBLANK2-PTARFG1B18K18.
echo.
pause
exit /b 0

:QUARANTINE
rem INSTALLFIX3: capture the argument outside parenthesized blocks.
rem Delayed expansion prevents Program Files (x86) from being reparsed as CMD syntax.
set "OLD=%~1"
for %%P in ("!OLD!") do set "OLD_DIR=%%~dpP"
set "Q="
for /l %%N in (1,1,16) do if not defined Q if not exist "!OLD_DIR!d3d11.win81nis_disabled_p1fg7n_%%N.dll" set "Q=!OLD_DIR!d3d11.win81nis_disabled_p1fg7n_%%N.dll"
if not defined Q (
  echo [ERREUR] Impossible de choisir un nom de quarantaine pour !OLD!
  set "QUAR_FAIL=1"
  exit /b 10
)
move /y "!OLD!" "!Q!" >nul
if errorlevel 1 (
  echo [ERREUR] Impossible de neutraliser l'ancienne copie : !OLD!
  set "QUAR_FAIL=1"
  exit /b 10
)
>>"!QUAR_FILE!" echo !OLD!^|!Q!
echo [OK] Ancienne copie neutralisee : !OLD!
exit /b 0

:COUNT
set "COUNT=0"
for /f %%N in ('%SystemRoot%\System32\find.exe /v /c "" ^< "%CAND%"') do set "COUNT=%%N"
exit /b 0

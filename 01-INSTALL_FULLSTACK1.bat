@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title PTAR LAB DWMPHASE2 AUTOCOLLECT1 RC18 - LIVETEE1

set "ROOT=%~dp0"
set "LOG=%ROOT%PTAR_INSTALL_LAST.log"
set "TEE=%ROOT%diag\PTAR_INSTALL_TEE_RAW.ps1"
set "CORE=%ROOT%tools\installer\PTAR_CORE_INSTALL.bat"
set "RC=0"
set "CORE_RC=NOT_RUN"

>"%LOG%" echo ============================================================
>>"%LOG%" echo PTAR INSTALLATION LOG - LIVETEE1
>>"%LOG%" echo ============================================================
>>"%LOG%" echo START_DATE=%DATE%
>>"%LOG%" echo START_TIME=%TIME%
>>"%LOG%" echo ROOT=%ROOT%
>>"%LOG%" echo COMSPEC=%ComSpec%
>>"%LOG%" echo PROCESSOR_ARCHITECTURE=%PROCESSOR_ARCHITECTURE%
>>"%LOG%" echo USERNAME=%USERNAME%
>>"%LOG%" echo.
>>"%LOG%" echo [WINDOWS]
>>"%LOG%" ver
>>"%LOG%" echo.

echo ============================================================
echo PTAR DISPLAY DELIVERY LAB DWMPHASE2 AUTOCOLLECT1 - RC18 PRESENTDELIVERY1
echo ============================================================
echo Tout le core sera visible ICI et copie simultanement dans :
echo "%LOG%"
echo.
echo Les questions interactives du core resteront visibles.
echo.

call :BOTH "[PACKAGE PRECHECK]"

if exist "%ROOT%win81_nis_dx11_x64.dll" (
  call :BOTH "[PASS] win81_nis_dx11_x64.dll present"
  certutil -hashfile "%ROOT%win81_nis_dx11_x64.dll" SHA256 >>"%LOG%" 2>&1
) else (
  call :BOTH "[FAIL] win81_nis_dx11_x64.dll absent"
)

if exist "%CORE%" (
  call :BOTH "[PASS] PTAR_CORE_INSTALL.bat present"
  certutil -hashfile "%CORE%" SHA256 >>"%LOG%" 2>&1
) else (
  call :BOTH "[FAIL] PTAR_CORE_INSTALL.bat absent"
  set "RC=42"
  goto :FINISH
)

if not exist "%TEE%" (
  call :BOTH "[FAIL] PTAR_INSTALL_TEE_RAW.ps1 absent"
  set "RC=43"
  goto :FINISH
)
call :BOTH "[PASS] raw console/log mirror present"

if not exist "%ROOT%_PTAR_UNINSTALL\reference\win81_nis.CADENCEFIX1_BASELINE.ini" (
  call :BOTH "[FAIL] INI CADENCEFIX1 reference absent"
  set "RC=40"
  goto :FINISH
)
call :BOTH "[PASS] INI CADENCEFIX1 reference present"

copy /y "%ROOT%_PTAR_UNINSTALL\reference\win81_nis.CADENCEFIX1_BASELINE.ini" "%ROOT%win81_nis.ini" >>"%LOG%" 2>&1
if errorlevel 1 (
  call :BOTH "[FAIL] Impossible de restaurer win81_nis.ini"
  set "RC=41"
  goto :FINISH
)
call :BOTH "[PASS] win81_nis.ini baseline restored"

call :BOTH ""
call :BOTH "============================================================"
call :BOTH "[CORE INSTALLER BEGIN - LIVE + LOG]"
call :BOTH "============================================================"
>>"%LOG%" echo CORE_START_DATE=%DATE%
>>"%LOG%" echo CORE_START_TIME=%TIME%

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%TEE%" -Core "%CORE%" -Log "%LOG%"
set "RC=!ERRORLEVEL!"
set "CORE_RC=!RC!"

>>"%LOG%" echo.
>>"%LOG%" echo ============================================================
>>"%LOG%" echo [CORE INSTALLER END]
>>"%LOG%" echo ============================================================
>>"%LOG%" echo CORE_RETURN_CODE=!RC!
>>"%LOG%" echo CORE_END_DATE=%DATE%
>>"%LOG%" echo CORE_END_TIME=%TIME%

echo.
echo ============================================================
echo [CORE INSTALLER END] RC=!RC!
echo ============================================================

:FINISH
>>"%LOG%" echo.
>>"%LOG%" echo [POSTCHECK]

if exist "%ROOT%win81_nis_install_target.txt" (
  >>"%LOG%" echo [PRESENT] win81_nis_install_target.txt
  >>"%LOG%" type "%ROOT%win81_nis_install_target.txt"
) else (
  >>"%LOG%" echo [ABSENT] win81_nis_install_target.txt
)

if exist "%ROOT%win81_nis_install_exe.txt" (
  >>"%LOG%" echo [PRESENT] win81_nis_install_exe.txt
  >>"%LOG%" type "%ROOT%win81_nis_install_exe.txt"
) else (
  >>"%LOG%" echo [ABSENT] win81_nis_install_exe.txt
)

if exist "%ROOT%win81_nis_install_target.txt" (
  set "TARGET="
  for /f "usebackq delims=" %%T in ("%ROOT%win81_nis_install_target.txt") do if not defined TARGET set "TARGET=%%~T"
  if defined TARGET (
    set "TARGET=!TARGET:"=!"
    if not "!TARGET:~-1!"=="\" set "TARGET=!TARGET!\"
    >>"%LOG%" echo TARGET_RESOLVED=!TARGET!
    if exist "!TARGET!d3d11.dll" (
      >>"%LOG%" echo [PRESENT] TARGET d3d11.dll
      certutil -hashfile "!TARGET!d3d11.dll" SHA256 >>"%LOG%" 2>&1
    ) else (
      >>"%LOG%" echo [ABSENT] TARGET d3d11.dll
    )
    if exist "!TARGET!win81_nis.ini" (
      >>"%LOG%" echo [PRESENT] TARGET win81_nis.ini
      %SystemRoot%\System32\findstr.exe /i /c:"TargetExe=" /c:"VideoRecordProfile=" /c:"ToggleHUD=" /c:"FrameGeneration=" "!TARGET!win81_nis.ini" >>"%LOG%" 2>&1
    ) else (
      >>"%LOG%" echo [ABSENT] TARGET win81_nis.ini
    )
  )
)

>>"%LOG%" echo.
>>"%LOG%" echo [FULLSTACK POST-INSTALL VERIFY]
set "VERIFY_RC=NOT_RUN"
if "!RC!"=="0" (
  echo.
  echo ============================================================
  echo [POST-INSTALL] Verification FULLSTACK1 automatique
  echo ============================================================
  call "%ROOT%VERIFY_FULLSTACK1_INSTALL.bat" /AUTO
  set "VERIFY_RC=!ERRORLEVEL!"
  >>"%LOG%" echo VERIFY_RETURN_CODE=!VERIFY_RC!
  >>"%LOG%" echo VERIFY_LOG=%ROOT%PTAR_VERIFY_LAST.log
  if not "!VERIFY_RC!"=="0" (
    set "RC=61"
    echo.
    echo [FAIL] Le core a ete installe mais la verification FULLSTACK1 a echoue.
    echo        Voir le resume ci-dessus et PTAR_VERIFY_LAST.log.
  )
) else (
  >>"%LOG%" echo VERIFY_RETURN_CODE=SKIPPED_CORE_FAILED
)

>>"%LOG%" echo.
>>"%LOG%" echo CORE_RETURN_CODE=!CORE_RC!
>>"%LOG%" echo FINAL_RETURN_CODE=!RC!
>>"%LOG%" echo END_DATE=%DATE%
>>"%LOG%" echo END_TIME=%TIME%
>>"%LOG%" echo ============================================================

echo.
echo ============================================================
if "!RC!"=="0" (
  echo [PASS] Installation + verification FULLSTACK1 terminees.
) else (
  echo [FAIL] Installation non validee - code !RC!.
)
echo Journal conserve :
echo "%LOG%"
echo ============================================================
echo.
echo Appuyez sur une touche lorsque vous avez fini de lire.
pause >nul
exit /b !RC!

:BOTH
echo %~1
>>"%LOG%" echo %~1
exit /b 0

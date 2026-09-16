@echo off
setlocal EnableExtensions
cd /d "%~dp0"
:menu
cls
echo ============================================================
echo PTAR - MARQUEUR FG / VBLANK (PETITS CARRES CLIGNOTANTS)
echo ============================================================
echo.
echo Ce reglage ne coupe PAS le HUD PTAR ni les FPS.
echo Il agit uniquement sur le marqueur visuel de cadence FG.
echo.
echo [1] Afficher l'etat actuel
echo [2] ACTIVER les petits carres FG
echo [3] DESACTIVER les petits carres FG
echo [4] Quitter
echo.
choice /C 1234 /N /M "Choix : "
if errorlevel 4 goto :eof
if errorlevel 3 goto off
if errorlevel 2 goto on
if errorlevel 1 goto status

:status
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0set_vblank_diagnostics.ps1" -Value -1
echo.
pause
goto menu

:on
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0set_vblank_diagnostics.ps1" -Value 1
echo.
echo Si le FG etait deja actif, faites CTRL+F6 OFF puis CTRL+F6 ON.
pause
goto menu

:off
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0set_vblank_diagnostics.ps1" -Value 0
echo.
echo Le HUD et les FPS restent actifs ; seul le marqueur FG est masque.
pause
goto menu

@echo off
setlocal EnableExtensions
set "HERE=%~dp0"
set "GAME=%~1"

if not defined GAME if exist "%CD%\conviction_game.exe" set "GAME=%CD%\conviction_game.exe"
if not defined GAME if exist "%CD%\src\system\conviction_game.exe" set "GAME=%CD%\src\system\conviction_game.exe"
if not defined GAME if defined ProgramFiles(x86) if exist "%ProgramFiles(x86)%\Steam\steamapps\common\Tom Clancy's Splinter Cell Conviction\src\system\conviction_game.exe" set "GAME=%ProgramFiles(x86)%\Steam\steamapps\common\Tom Clancy's Splinter Cell Conviction\src\system\conviction_game.exe"

if not defined GAME (
  echo [PTAR] conviction_game.exe non trouve automatiquement.
  echo Glissez conviction_game.exe sur ce BAT ou lancez:
  echo   01-INSTALL_CONVICTION_PTAR_X86.bat "C:\...\conviction_game.exe"
  exit /b 2
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HERE%install_x86_d3d9.ps1" -GameExe "%GAME%" -PackageRoot "%HERE%"
exit /b %ERRORLEVEL%

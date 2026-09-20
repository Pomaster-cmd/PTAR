@echo off
setlocal EnableExtensions
set "HERE=%~dp0"
set "GAME=%~1"
if not defined GAME if exist "%CD%\conviction_game.exe" set "GAME=%CD%\conviction_game.exe"
if not defined GAME if exist "%CD%\src\system\conviction_game.exe" set "GAME=%CD%\src\system\conviction_game.exe"
if not defined GAME if defined ProgramFiles(x86) if exist "%ProgramFiles(x86)%\Steam\steamapps\common\Tom Clancy's Splinter Cell Conviction\src\system\conviction_game.exe" set "GAME=%ProgramFiles(x86)%\Steam\steamapps\common\Tom Clancy's Splinter Cell Conviction\src\system\conviction_game.exe"
if not defined GAME (
  echo [PTAR] conviction_game.exe non trouve.
  exit /b 2
)
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HERE%uninstall_x86_d3d9.ps1" -GameExe "%GAME%"
exit /b %ERRORLEVEL%

@echo off
setlocal EnableExtensions
set "HERE=%~dp0"
set "GAME=%~1"

if not defined GAME if exist "%HERE%conviction_game.exe" set "GAME=%HERE%conviction_game.exe"
if not defined GAME if exist "%HERE%src\system\conviction_game.exe" set "GAME=%HERE%src\system\conviction_game.exe"

if defined GAME (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HERE%collect_x86_d3d9_diag.ps1" -GameExe "%GAME%"
) else (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%HERE%collect_x86_d3d9_diag.ps1"
)
echo.
echo Le ZIP de diagnostic est indique sur la ligne OUTPUT=...
pause
exit /b %ERRORLEVEL%

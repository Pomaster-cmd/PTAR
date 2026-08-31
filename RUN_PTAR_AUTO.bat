@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo PTAR v0.12.3 - Windows 8.1 autonomous runtime gate
echo ---------------------------------------------------
echo 1. Build unique du host autonome
echo 2. Audit des imports
echo 3. PATH sans cl.exe/fxc.exe
echo 4. 42 parites GPU + 512 paires de timing
echo.

call "%~dp0PREPARE_AUTONOMOUS_RUNTIME.bat"
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
  echo [PASS] Runtime autonome Windows 8.1 valide dans cet environnement.
  echo [NEXT] Envoyez le dossier cree sous runtime_autonomous\...\runs.
) else (
  echo [FAIL] Etape autonome code=%RC%
)
pause
exit /b %RC%

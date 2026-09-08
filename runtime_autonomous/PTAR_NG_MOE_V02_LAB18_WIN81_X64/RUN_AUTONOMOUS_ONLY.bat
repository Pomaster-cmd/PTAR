@echo off
setlocal EnableExtensions

for %%I in ("%~dp0.") do set "BUNDLE=%%~fI"
set "EXE=%BUNDLE%\bin\ptar_v02_runtime_bench.exe"

if not exist "%EXE%" (
  echo [FAIL] Binaire autonome introuvable: "%EXE%"
  exit /b 10
)

set "OUT=%BUNDLE%\results\hardware_%RANDOM%_%RANDOM%"
if exist "%OUT%\" (
  echo [FAIL] Dossier de resultat deja present: "%OUT%"
  exit /b 11
)
mkdir "%OUT%"
if errorlevel 1 exit /b 12

echo [PTAR] Bench cible: PTAR-NG MoE v02 LAB18 vs MoE v01 vs K185
echo [PTAR] Entree 1280x720 - sortie 1920x1080 - x1.5 exact
echo [PTAR] Resultats: "%OUT%"
echo.

"%EXE%" --bundle-root "%BUNDLE%" --out "%OUT%"
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
  echo.
  echo [FAIL] Bench autonome termine avec le code %RC%.
  exit /b %RC%
)

echo.
echo [PASS] Bench autonome termine.
echo [PASS] Envoyer uniquement le dossier de resultats suivant:
echo        "%OUT%"
exit /b 0

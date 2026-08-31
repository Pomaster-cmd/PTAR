@echo off
setlocal EnableExtensions

for %%I in ("%~dp0.") do set "BUNDLE=%%~fI"

set "SRC=%BUNDLE%\source\ptar_autonomous_validation.cpp"
set "INC=%BUNDLE%\include"
set "BIN=%BUNDLE%\bin"
set "EVID=%BUNDLE%\build_evidence"

if not exist "%SRC%" exit /b 10
if not exist "%INC%\PTARD3D11GpuTimerRing.h" exit /b 10

where cl.exe >nul 2>nul
if errorlevel 1 exit /b 11
where dumpbin.exe >nul 2>nul
if errorlevel 1 exit /b 12
where certutil.exe >nul 2>nul
if errorlevel 1 exit /b 13

if not exist "%BIN%\" mkdir "%BIN%"
if errorlevel 1 exit /b 14
if not exist "%EVID%\" mkdir "%EVID%"
if errorlevel 1 exit /b 15

if exist "%BIN%\ptar_autonomous_validation.exe" (
  echo [FAIL] Le binaire autonome existe deja. Aucun ecrasement automatique.
  exit /b 16
)

set "STAGE=%EVID%\stage_%RANDOM%_%RANDOM%"
if exist "%STAGE%\" exit /b 17
mkdir "%STAGE%"
if errorlevel 1 exit /b 18

echo [BUILD] Host autonome /MT / Windows 8.1 / precompiled DXBC runtime
echo [BUILD] Source="%SRC%"
echo [BUILD] Stage="%STAGE%"

cl.exe /nologo /EHsc /O2 /MT /W4 /WX ^
  /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /DUNICODE /D_UNICODE ^
  /I "%INC%" ^
  /Fo:"%STAGE%\ptar_autonomous_validation.obj" ^
  "%SRC%" /Fe:"%STAGE%\ptar_autonomous_validation.exe" ^
  /link /SUBSYSTEM:CONSOLE,6.03 d3d11.lib dxgi.lib windowscodecs.lib ole32.lib bcrypt.lib
if errorlevel 1 exit /b 30

if not exist "%STAGE%\ptar_autonomous_validation.exe" exit /b 31
for %%A in ("%STAGE%\ptar_autonomous_validation.exe") do if %%~zA LEQ 0 exit /b 31

dumpbin /dependents "%STAGE%\ptar_autonomous_validation.exe" > "%STAGE%\IMPORTS.txt"
if errorlevel 1 exit /b 32

findstr /I ^
  /C:"D3DCOMPILER" ^
  /C:"VCRUNTIME" ^
  /C:"MSVCP" ^
  /C:"UCRTBASE" ^
  /C:"API-MS-WIN-CRT" ^
  "%STAGE%\IMPORTS.txt" >nul
if not errorlevel 1 (
  echo [FAIL] Dependence runtime interdite detectee.
  type "%STAGE%\IMPORTS.txt"
  exit /b 33
)

if exist "%EVID%\IMPORTS_VALIDATED.txt" exit /b 34
copy "%STAGE%\IMPORTS.txt" "%EVID%\IMPORTS_VALIDATED.txt" >nul
if errorlevel 1 exit /b 35

copy "%STAGE%\ptar_autonomous_validation.exe" "%BIN%\ptar_autonomous_validation.exe" >nul
if errorlevel 1 exit /b 36
if not exist "%BIN%\ptar_autonomous_validation.exe" exit /b 37
for %%A in ("%BIN%\ptar_autonomous_validation.exe") do if %%~zA LEQ 0 exit /b 37

if exist "%EVID%\BINARY_SHA256.txt" exit /b 38
certutil -hashfile "%BIN%\ptar_autonomous_validation.exe" SHA256 > "%EVID%\BINARY_SHA256.txt"
if errorlevel 1 exit /b 39

echo [PASS] Host autonome construit.
echo [PASS] Objet .obj conserve dans le stage.
echo [PASS] Aucun import D3DCompiler/CRT dynamique detecte.
exit /b 0

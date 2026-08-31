@echo off
setlocal EnableExtensions

for %%I in ("%~dp0.") do set "ROOT=%%~fI"
set "BUNDLE=%ROOT%\runtime_autonomous\PTAR_NG_MOE_V01_WIN81_X64"

echo [PREPARE] root="%ROOT%"
echo [PREPARE] bundle="%BUNDLE%"

if not exist "%BUNDLE%\" exit /b 19

if exist "%BUNDLE%\bin\ptar_autonomous_validation.exe" (
  echo [INFO] Binaire autonome deja present: test direct.
  call "%BUNDLE%\RUN_AUTONOMOUS_ONLY.bat"
  exit /b %ERRORLEVEL%
)

set "VCVARS=C:\PTAR_Toolchain\VS2019BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VCVARS%" goto :havevc

if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  for /f "usebackq delims=" %%V in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%V\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%V\VC\Auxiliary\Build\vcvars64.bat"
  )
)

:havevc
if not exist "%VCVARS%" exit /b 20

echo [INFO] Import MSVC: "%VCVARS%"
call "%VCVARS%"
if errorlevel 1 exit /b 21

call "%BUNDLE%\BUILD_RUNTIME_ONCE_IN_DEV_ENV.bat"
set "BUILD_RC=%ERRORLEVEL%"
if not "%BUILD_RC%"=="0" exit /b %BUILD_RC%

if not exist "%BUNDLE%\bin\ptar_autonomous_validation.exe" (
  echo [FAIL] Builder PASS mais EXE final absent.
  exit /b 22
)

echo.
echo [INFO] Test immediat avec PATH nettoye de la toolchain...
call "%BUNDLE%\RUN_AUTONOMOUS_ONLY.bat"
exit /b %ERRORLEVEL%

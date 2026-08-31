@echo off
setlocal EnableExtensions
for %%I in ("%~dp0.") do set "BUNDLE=%%~fI"

echo PTAR - verification APRES desinstallation toolchain
echo ---------------------------------------------------
echo [VERIFY] bundle="%BUNDLE%"

if exist "C:\PTAR_Toolchain\VS2019BuildTools" (
  echo [INFO] C:\PTAR_Toolchain\VS2019BuildTools existe encore.
) else (
  echo [PASS] C:\PTAR_Toolchain\VS2019BuildTools absent.
)

if exist "%ProgramFiles(x86)%\Windows Kits\8.1" (
  echo [INFO] Windows Kits 8.1 existe encore.
) else (
  echo [PASS] Windows Kits 8.1 absent.
)

echo.
call "%BUNDLE%\RUN_AUTONOMOUS_ONLY.bat"
exit /b %ERRORLEVEL%

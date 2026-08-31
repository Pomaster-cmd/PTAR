@echo off
setlocal EnableExtensions

echo === PTAR Windows 8.1 / GTX hardware preflight ===
ver
where cl.exe
where powershell.exe

echo.
echo === GPU inventory ===
wmic path win32_videocontroller get Name,DriverVersion,AdapterRAM /format:list

echo.
echo Ce script ne modifie rien. Pour lancer la validation complete:
echo BUILD_AND_RUN_K185_HARDWARE_VALIDATION.bat
exit /b 0

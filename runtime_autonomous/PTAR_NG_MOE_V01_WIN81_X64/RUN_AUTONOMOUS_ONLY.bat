@echo off
setlocal EnableExtensions

rem Canonical directory WITHOUT trailing separator.
for %%I in ("%~dp0.") do set "BUNDLE=%%~fI"

rem Every child path adds its own separator explicitly.
set "EXE=%BUNDLE%\bin\ptar_autonomous_validation.exe"
set "SHADER_DIR=%BUNDLE%\shaders"
set "INPUT_DIR=%BUNDLE%\corpus\input_lr"
set "EXPECTED_DIR=%BUNDLE%\corpus\expected_moe_float32"
set "RUNS_DIR=%BUNDLE%\runs"

echo [PREFLIGHT] bundle="%BUNDLE%"
echo [PREFLIGHT] exe="%EXE%"

if not exist "%BUNDLE%\" (
  echo [FAIL] Bundle autonome introuvable: "%BUNDLE%"
  exit /b 39
)

if not exist "%EXE%" (
  echo [FAIL] Binaire autonome absent: "%EXE%"
  echo [INFO] Avant desinstallation de la toolchain, lancez RUN_PTAR_AUTO.bat depuis la racine PTAR.
  exit /b 40
)

for %%F in (
  "%SHADER_DIR%\ptar_vs.cso"
  "%SHADER_DIR%\ptar_moe_ng_v01_sf5_ps.cso"
  "%SHADER_DIR%\ptar_k185_control_ps.cso"
) do (
  if not exist "%%~F" (
    echo [FAIL] Shader absent: %%~F
    exit /b 41
  )
  if %%~zF LEQ 4 (
    echo [FAIL] Shader vide/invalide: %%~F
    exit /b 41
  )
)

if not exist "%INPUT_DIR%\" (
  echo [FAIL] Corpus input absent: "%INPUT_DIR%"
  exit /b 42
)
if not exist "%EXPECTED_DIR%\" (
  echo [FAIL] Corpus expected absent: "%EXPECTED_DIR%"
  exit /b 43
)

set "INPUT_COUNT=0"
for %%F in ("%INPUT_DIR%\*_LR.png") do (
  if exist "%%~F" set /a INPUT_COUNT+=1
)
set "EXPECTED_COUNT=0"
for %%F in ("%EXPECTED_DIR%\*.png") do (
  if exist "%%~F" set /a EXPECTED_COUNT+=1
)

echo [PREFLIGHT] input_png=%INPUT_COUNT% expected_png=%EXPECTED_COUNT%
if not "%INPUT_COUNT%"=="42" (
  echo [FAIL] 42 inputs attendus, %INPUT_COUNT% trouves.
  exit /b 44
)
if not "%EXPECTED_COUNT%"=="42" (
  echo [FAIL] 42 references attendues, %EXPECTED_COUNT% trouvees.
  exit /b 45
)

if not exist "%RUNS_DIR%\" mkdir "%RUNS_DIR%"
if errorlevel 1 (
  echo [FAIL] Creation impossible: "%RUNS_DIR%"
  exit /b 46
)

set "RUN=%RUNS_DIR%\autonomous_%RANDOM%_%RANDOM%"
if exist "%RUN%\" (
  echo [FAIL] Collision dossier run: "%RUN%"
  exit /b 47
)
mkdir "%RUN%"
if errorlevel 1 exit /b 48
mkdir "%RUN%\results"
if errorlevel 1 exit /b 49

rem Runtime proof: development executables are deliberately hidden.
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem"

where cl.exe >nul 2>nul
if errorlevel 1 (
  set "CL_VISIBLE=NO"
) else (
  set "CL_VISIBLE=YES"
)

where fxc.exe >nul 2>nul
if errorlevel 1 (
  set "FXC_VISIBLE=NO"
) else (
  set "FXC_VISIBLE=YES"
)

(
  echo runtime_mode=PRECOMPILED_DXBC_ONLY
  echo bundle=%BUNDLE%
  echo executable=%EXE%
  echo input_dir=%INPUT_DIR%
  echo expected_dir=%EXPECTED_DIR%
  echo input_count=%INPUT_COUNT%
  echo expected_count=%EXPECTED_COUNT%
  echo sanitized_path=%PATH%
  echo cl_visible=%CL_VISIBLE%
  echo fxc_visible=%FXC_VISIBLE%
  echo d3dcompiler_required=NO
  echo visual_studio_required=NO
  echo windows_sdk_required=NO
) > "%RUN%\AUTONOMOUS_ENVIRONMENT.txt"

if not "%CL_VISIBLE%"=="NO" (
  echo [FAIL] cl.exe est encore visible dans le PATH nettoye.
  exit /b 50
)
if not "%FXC_VISIBLE%"=="NO" (
  echo [FAIL] fxc.exe est encore visible dans le PATH nettoye.
  exit /b 51
)

echo [AUTONOMOUS] Aucun cl.exe/fxc.exe visible.
echo [AUTONOMOUS] Chargement direct des CSO valides.
echo [ARGS] bundle-root="%BUNDLE%"
echo [ARGS] out="%RUN%\results"

"%EXE%" --bundle-root "%BUNDLE%" --out "%RUN%\results"
set "RC=%ERRORLEVEL%"

echo runtime_exit_code=%RC%>> "%RUN%\AUTONOMOUS_ENVIRONMENT.txt"

if not "%RC%"=="0" (
  echo [FAIL] Runtime autonome code=%RC%
  echo [INFO] Resultats: "%RUN%"
  exit /b %RC%
)

if not exist "%RUN%\results\hardware_summary.txt" exit /b 52
if not exist "%RUN%\results\parity.csv" exit /b 53
if not exist "%RUN%\results\timing.csv" exit /b 54
if not exist "%RUN%\results\timing_pairs.csv" exit /b 55

findstr /C:"parity_cases=42" "%RUN%\results\hardware_summary.txt" >nul
if errorlevel 1 exit /b 56
findstr /C:"parity_failed_cases=0" "%RUN%\results\hardware_summary.txt" >nul
if errorlevel 1 exit /b 57
findstr /C:"parity_global_max_abs_lsb=1" "%RUN%\results\hardware_summary.txt" >nul
if errorlevel 1 exit /b 58
findstr /C:"paired_samples=512" "%RUN%\results\hardware_summary.txt" >nul
if errorlevel 1 exit /b 59
findstr /C:"gpu_png_persistence=PASS_42_NONZERO" "%RUN%\results\hardware_summary.txt" >nul
if errorlevel 1 exit /b 60
findstr /C:"runtime_shader_mode=PRECOMPILED_DXBC_ONLY" "%RUN%\results\hardware_summary.txt" >nul
if errorlevel 1 exit /b 61
findstr /C:"runtime_d3dcompiler_required=NO" "%RUN%\results\hardware_summary.txt" >nul
if errorlevel 1 exit /b 62
findstr /C:"runtime_visual_studio_required=NO" "%RUN%\results\hardware_summary.txt" >nul
if errorlevel 1 exit /b 63
findstr /C:"runtime_windows_sdk_required=NO" "%RUN%\results\hardware_summary.txt" >nul
if errorlevel 1 exit /b 64

set "GPU_PNG_COUNT=0"
for %%F in ("%RUN%\results\gpu_outputs\*.png") do (
  if exist "%%~F" (
    if %%~zF LEQ 0 (
      echo [FAIL] PNG GPU vide: %%~F
      exit /b 65
    )
    set /a GPU_PNG_COUNT+=1
  )
)
if not "%GPU_PNG_COUNT%"=="42" (
  echo [FAIL] 42 PNG GPU attendus, %GPU_PNG_COUNT% trouves.
  exit /b 66
)

echo [PASS] PTAR autonome: 42/42, 512 paires, 42 PNG, CSO precompiles.
echo [PASS] Aucun compilateur/SDK requis dans le runtime teste.
echo [INFO] Resultats: "%RUN%"
exit /b 0

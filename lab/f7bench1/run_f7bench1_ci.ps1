$ErrorActionPreference='Stop'

# Reconstruct and qualify the exact field-validated TEMPORALPAIR60 baseline first.
.\lab\hf6_temporalpair60\run_temporal_ci.ps1

Copy-Item artifact\d3d9.dll artifact\d3d9_temporalpair60_baseline.dll -Force
$baselineSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$beforePacer=(Get-FileHash compat\x86_d3d9\ptar_fg_pacer.h -Algorithm SHA256).Hash.ToLowerInvariant()
$beforeCoarse=(Get-FileHash compat\x86_d3d9\ptar_fg_me_coarse_ps.hlsl -Algorithm SHA256).Hash.ToLowerInvariant()
$beforeRefine=(Get-FileHash compat\x86_d3d9\ptar_fg_me_refine_ps.hlsl -Algorithm SHA256).Hash.ToLowerInvariant()
$beforeInterp=(Get-FileHash compat\x86_d3d9\ptar_fg_interpolate_ps.hlsl -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item compat\x86_d3d9\ptar_d3d9_proxy.cpp artifact\ptar_d3d9_proxy_temporal_base.cpp -Force

# Apply only the F7 measurement integration after TEMPORALPAIR60 is established.
python .\lab\f7bench1\apply_f7bench1.py | Tee-Object artifact\F7BENCH1_APPLY.txt
if($LASTEXITCODE){throw 'F7BENCH1 apply failed'}
$apply=Get-Content artifact\F7BENCH1_APPLY.txt -Raw
foreach($m in @(
  'F7BENCH1_PATCH=PASS',
  'F7BENCH1_SCOPE=PASSIVE_PRESENT_MEASUREMENT_ONLY',
  'F7BENCH1_DEFAULT_WINDOW=3S_SETTLE_PLUS_10S_RECORD',
  'F7BENCH1_OVERLAP=IGNORED',
  'F7BENCH1_VISIBLE_PRESENT_HOOKS=2',
  'F7BENCH1_FG_CORE_CHANGE=NONE',
  'F7BENCH1_PACER_CHANGE=NONE')){
  if($apply -notmatch [regex]::Escape($m)){throw "missing F7 apply gate $m"}
}

# Standalone state/timing model under the same Win8.1 x86 compiler contract.
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\f7bench1\f7_benchmark_harness.cpp /link /OUT:f7_benchmark_harness.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'F7 benchmark harness compile failed'}
& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\F7_BENCHMARK_HARNESS.txt
if($LASTEXITCODE){throw 'F7 benchmark harness failed'}
$h=Get-Content artifact\F7_BENCHMARK_HARNESS.txt -Raw
foreach($m in @(
  'F7BENCH_60HZ_RESULT=PASS','F7BENCH_60HZ_AVG=PASS','F7BENCH_60HZ_P99=PASS','F7BENCH_60HZ_1P_LOW=PASS',
  'F7BENCH_55HZ_RESULT=PASS','F7BENCH_55HZ_AVG=PASS','F7BENCH_REENTRY_SECOND_IGNORED=PASS',
  'F7BENCH_LONG_GAP_RESTARTS_SETTLING=PASS','F7BENCH_LONG_GAP_RESTART_COUNT=PASS','F7BENCH_LONG_GAP_CLEAN_AVG=PASS',
  'F7BENCH_FG_OFF_ZERO_GENERATED=PASS','F7BENCH_FG_OFF_ALL_REAL=PASS','F7BENCH_ALL_GATES=PASS')){
  if($h -notmatch [regex]::Escape($m)){throw "missing F7 harness gate $m"}
}

# F7 patch must leave the proven FG/pacing files byte-identical.
$afterPacer=(Get-FileHash compat\x86_d3d9\ptar_fg_pacer.h -Algorithm SHA256).Hash.ToLowerInvariant()
$afterCoarse=(Get-FileHash compat\x86_d3d9\ptar_fg_me_coarse_ps.hlsl -Algorithm SHA256).Hash.ToLowerInvariant()
$afterRefine=(Get-FileHash compat\x86_d3d9\ptar_fg_me_refine_ps.hlsl -Algorithm SHA256).Hash.ToLowerInvariant()
$afterInterp=(Get-FileHash compat\x86_d3d9\ptar_fg_interpolate_ps.hlsl -Algorithm SHA256).Hash.ToLowerInvariant()
if($beforePacer -ne $afterPacer){throw 'F7 changed pacer'}
if($beforeCoarse -ne $afterCoarse -or $beforeRefine -ne $afterRefine -or $beforeInterp -ne $afterInterp){throw 'F7 changed FG shader'}
'F7BENCH1_PACER_BYTE_IDENTICAL=PASS' | Set-Content artifact\F7_SCOPE_IDENTITY.txt -Encoding ascii
'F7BENCH1_FG_SHADERS_BYTE_IDENTICAL=PASS' | Add-Content artifact\F7_SCOPE_IDENTITY.txt -Encoding ascii

# Re-run the HF6 generation identity gate after the F7 source integration.
python -c "from pathlib import Path;import subprocess; b='9a47a93e1c3cb87975903eb21298408e26c2957a'; p=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp').read_text(); q=subprocess.check_output(['git','show',b+':compat/x86_d3d9/ptar_d3d9_proxy.cpp'],text=True); f=lambda s,a,z:s[s.index(a):s.index(z,s.index(a))]; assert f(p,'static HRESULT RenderFGMotionAndIntermediate','static HRESULT PtRefreshQueuedRealBackBuffer')==f(q,'static HRESULT RenderFGMotionAndIntermediate','static HRESULT RenderTextureToBackBuffer'); print('F7BENCH1_HF6_FG_CORE_IDENTICAL=PASS')" | Tee-Object -Append artifact\F7_SCOPE_IDENTITY.txt
if($LASTEXITCODE){throw 'F7 changed HF6 generation core'}

# Recompile the actual x86 D3D9 runtime. Shader bytecode already came from the fully validated TEMPORALPAIR60 build above.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_f7bench1.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'F7 runtime compile failed'}
dumpbin /headers artifact\d3d9_f7bench1.dll | Out-File artifact\F7_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_f7bench1.dll | Out-File artifact\F7_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_f7bench1.dll | Out-File artifact\F7_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\F7_HEADERS.txt -Raw;$ex=Get-Content artifact\F7_EXPORTS.txt -Raw;$dp=Get-Content artifact\F7_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'F7 Win8.1 x86 PE contract failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "F7 missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "F7 forbidden dependency $bad"}}
$f7Sha=(Get-FileHash artifact\d3d9_f7bench1.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$f7Sha | Set-Content artifact\D3D9_F7BENCH1_SHA256.txt -Encoding ascii

# Existing F5/F1 runtime-ring tests are run AGAINST THE NEW F7 BUILD, not only the baseline.
New-Item -ItemType Directory -Force smoke_f7 | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_f7\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'F7 ring harness compile failed'}
Copy-Item artifact\d3d9_f7bench1.dll smoke_f7\d3d9.dll -Force
$root=(Resolve-Path smoke_f7).Path;$game=(Resolve-Path smoke_f7\game.exe).Path;$dll=(Resolve-Path smoke_f7\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\F7_RING_HARNESS.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\F7_CTRL_F5_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'F7 build F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_F7_SMOKE'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\F7_CTRL_F1_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'F7 build F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}

# Scope markers in the final patched source.
$p=Get-Content compat\x86_d3d9\ptar_d3d9_proxy.cpp -Raw
$hud=Get-Content compat\x86_d3d9\ptar_hud.h -Raw
$bench=Get-Content compat\x86_d3d9\ptar_f7_benchmark.h -Raw
foreach($m in @('PTAR_TEMPORAL_SLOT_COUNT 3','PtTemporalPresenterThread','DISPLAY_ORDER=GENERATED_THEN_REAL')){
  if($p -notmatch [regex]::Escape($m)){throw "temporal marker lost $m"}
}
if($hud -match 'Benchmark backend is not yet ported to D3D9'){throw 'old F7 BUSY stub remains'}
foreach($m in @('PtF7BenchmarkStart(g_ptarHudFgEnabled)','PtF7BenchmarkConsumeResult','BenchmarkWarmupSeconds','BenchmarkDurationSeconds')){
  if($hud -notmatch [regex]::Escape($m)){throw "F7 HUD marker missing $m"}
}
foreach($m in @('PTAR_F7BENCH_LONG_GAP_MS 250u','PTAR_F7BENCH_MAX_INTERVALS 2048u','PTAR_F7_BENCHMARK_LAST.txt')){
  if($bench -notmatch [regex]::Escape($m)){throw "F7 module marker missing $m"}
}

Copy-Item artifact\d3d9_f7bench1.dll artifact\d3d9.dll -Force
Copy-Item compat\x86_d3d9\ptar_f7_benchmark.h artifact\ptar_f7_benchmark.h -Force
Copy-Item compat\x86_d3d9\ptar_hud.h artifact\ptar_hud_f7bench1.h -Force
Copy-Item compat\x86_d3d9\ptar_gw16i_hud_d3d9.h artifact\ptar_gw16i_hud_d3d9_f7bench1.h -Force
Copy-Item compat\x86_d3d9\ptar_d3d9_proxy.cpp artifact\ptar_d3d9_proxy_f7bench1.cpp -Force
@(
 'PTAR_D3D9_BRANCH=HF6_TEMPORALPAIR60_F7BENCH1',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('BASE_TEMPORALPAIR60_SHA256='+$baselineSha),
 ('D3D9_F7BENCH1_SHA256='+$f7Sha),
 'FG_ALGORITHM=EXACT_HF6',
 'TEMPORAL_SCHEDULER=UNCHANGED_TEMPORALPAIR60',
 'F7_BENCHMARK=PASSIVE_VISIBLE_PRESENTS',
 'F7_DEFAULT_SETTLE_SECONDS=3',
 'F7_DEFAULT_RECORD_SECONDS=10',
 'F7_METRICS=AVG_VISIBLE_FPS_P99_FRAME_1P_LOW_REAL_GENERATED',
 'F7_LONG_GAP_POLICY=RESTART_SETTLING',
 'F7_RESULT_FILE=PTAR_F7_BENCHMARK_LAST.txt',
 'CTRL_F5_F1=REGRESSION_PASS') | Set-Content artifact\PTAR_D3D9_HF6_TEMPORALPAIR60_F7BENCH1_VERSION.txt -Encoding ascii

Write-Host "F7BENCH1_RUNTIME_SHA256=$f7Sha"
Write-Host 'HF6_TEMPORALPAIR60_F7BENCH1_FULL_LAB=PASS'

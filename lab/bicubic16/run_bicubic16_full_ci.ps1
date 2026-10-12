$ErrorActionPreference='Stop'
$fullQualityRef='origin/lab/d3d9-ptar-fullquality1-20261012'
$labRef='origin/lab/d3d9-spec5-bicubic16-newrefs-20261012'
New-Item -ItemType Directory -Force artifact | Out-Null

# Preserve NEWREFS2 while reconstructing canonical FULLQUALITY1.
$candidateSource=Join-Path $env:RUNNER_TEMP 'ptar_bicubic16_candidate.hlsl'
Copy-Item compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl $candidateSource -Force
$baseSpec=git show ($fullQualityRef+':compat/x86_d3d9/ptar_spec5_d3d9_ps.hlsl')
if($LASTEXITCODE){throw 'cannot read canonical FULLQUALITY1 SPEC5 source'}
Set-Content compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl $baseSpec -Encoding utf8

# 1) Reconstruct and fully qualify canonical FULLQUALITY1 first.
.\lab\fullquality1\run_fullquality1_full_ci.ps1
if($LASTEXITCODE){throw 'FULLQUALITY1 baseline qualification failed'}
$baselineRuntimeSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item artifact\d3d9.dll artifact\d3d9_fullquality1_rebuilt_baseline.dll -Force

# Hard boundary: everything except Q720 spatial source/header is immutable.
$immutable=@(
 'compat/x86_d3d9/ptar_d3d9_proxy.cpp',
 'compat/x86_d3d9/ptar_qstage4diag2_d3d9_ps.hlsl',
 'compat/x86_d3d9/ptar_hq900_d3d9_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_pacer.h',
 'compat/x86_d3d9/ptar_fg_me_coarse_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_me_refine_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_interpolate_ps.hlsl',
 'compat/x86_d3d9/ptar_f7_benchmark.h',
 'compat/x86_d3d9/ptar_hud.h',
 'compat/x86_d3d9/ptar_gw16i_hud_d3d9.h',
 'compat/x86_d3d9/ptar_resolution_policy.h')
$before=@{}
foreach($rel in $immutable){$before[$rel]=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant()}
$q4HeaderBefore=(Get-FileHash build_x86\ptar_qstage4diag2_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant()
$hqHeaderBefore=(Get-FileHash build_x86\ptar_hq900_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant()

# 2) Restore Bicubic16 NEWREFS2 and compile only Q720 production bytecode.
Copy-Item $candidateSource compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl -Force
$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_spec5_d3d9_ps_bytecode.h /Vn g_ptarSpec5Ps /Fo artifact\BICUBIC16_PRODUCTION.cso /Fc artifact\BICUBIC16_PRODUCTION_ASM.txt compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'Bicubic16 production compile failed'}
$asm=Get-Content artifact\BICUBIC16_PRODUCTION_ASM.txt -Raw
$texld=([regex]::Matches($asm,'(?im)^\s*texld')).Count
$slots=0;$m=[regex]::Match($asm,'(?i)approximately\s+(\d+)\s+instruction slots');if($m.Success){$slots=[int]$m.Groups[1].Value}
if($texld -ne 16){throw "Bicubic16 expected 16 texld, got $texld"}
if($slots -gt 160 -or $slots -le 0){throw "Bicubic16 unexpected slot count $slots"}

# 3) Independently retrieve/compile branch source; production DXBC must be byte-identical.
$refSource=Join-Path $env:RUNNER_TEMP 'ptar_bicubic16_reference.hlsl'
$refText=git show ($labRef+':compat/x86_d3d9/ptar_spec5_d3d9_ps.hlsl')
if($LASTEXITCODE){throw 'cannot retrieve Bicubic16 lab reference source'}
Set-Content $refSource $refText -Encoding utf8
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fo artifact\BICUBIC16_LAB_REFERENCE.cso $refSource
if($LASTEXITCODE){throw 'Bicubic16 lab reference compile failed'}
$prodCso=(Get-FileHash artifact\BICUBIC16_PRODUCTION.cso -Algorithm SHA256).Hash.ToLowerInvariant()
$refCso=(Get-FileHash artifact\BICUBIC16_LAB_REFERENCE.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($prodCso -ne $refCso){throw "Bicubic16 DXBC identity failed prod=$prodCso ref=$refCso"}
@(
 'BICUBIC16_NEWREFS2_LAB_DXBC_IDENTITY=PASS',
 ('BICUBIC16_CSO_SHA256='+$prodCso),
 ('BICUBIC16_TEXLD='+$texld),
 ('BICUBIC16_SLOTS='+$slots),
 'BICUBIC16_KERNEL=CATMULL_ROM_KEYS_A_MINUS_0P5',
 'BICUBIC16_DETAIL_RESIDUAL_GAIN=0.25',
 'BICUBIC16_EXTRA_FULLSCREEN_PASS=0') | Set-Content artifact\BICUBIC16_IDENTITY.txt -Encoding ascii

# 4) Verify forbidden sources and other spatial bytecodes did not change.
foreach($rel in $immutable){$after=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant();if($after -ne $before[$rel]){throw "Bicubic16 modified forbidden source: $rel"}}
if((Get-FileHash build_x86\ptar_qstage4diag2_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant() -ne $q4HeaderBefore){throw 'QSTAGE4 bytecode changed'}
if((Get-FileHash build_x86\ptar_hq900_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hqHeaderBefore){throw 'HQ900 bytecode changed'}
@(
 'BICUBIC16_PROXY_IDENTICAL=PASS',
 'BICUBIC16_QSTAGE4_IDENTICAL=PASS',
 'BICUBIC16_HQ900_IDENTICAL=PASS',
 'BICUBIC16_HF6_IDENTICAL=PASS',
 'BICUBIC16_TEMPORALPAIR60_IDENTICAL=PASS',
 'BICUBIC16_F7_IDENTICAL=PASS',
 'BICUBIC16_SF5PARITY1_WRAPPER_IDENTICAL=PASS') | Set-Content artifact\BICUBIC16_SCOPE_IDENTITY.txt -Encoding ascii

# 5) Selector remains unchanged: Q720 -> SPEC5 shader object, whose bytecode is replaced.
python .\lab\fullquality1\validate_fullquality_selector.py 2>&1 | Tee-Object artifact\BICUBIC16_SELECTOR.txt
if($LASTEXITCODE){throw 'selector regression'}
if((Get-Content artifact\BICUBIC16_SELECTOR.txt -Raw) -notmatch 'FULLQUALITY_SELECTOR_ALL=PASS'){throw 'selector PASS marker missing'}

# 6) Re-run pixel oracle using a separate header so the production g_ptarSpec5Ps header stays intact.
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_bicubic16_oracle_bytecode.h /Vn g_ptarBicubic16Ps /Fo artifact\BICUBIC16_ORACLE_PRODUCTION.cso compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'production Bicubic16 oracle shader compile failed'}
$oracleCso=(Get-FileHash artifact\BICUBIC16_ORACLE_PRODUCTION.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($oracleCso -ne $prodCso){throw "oracle/production DXBC differs oracle=$oracleCso prod=$prodCso"}
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\bicubic16\bicubic16_gpu_oracle.cpp /link d3d9.lib user32.lib /OUT:artifact\bicubic16_production_oracle.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'production Bicubic16 oracle compile failed'}
& artifact\bicubic16_production_oracle.exe 2>&1 | Tee-Object artifact\BICUBIC16_PRODUCTION_ORACLE.txt
if($LASTEXITCODE){throw 'production Bicubic16 oracle failed'}
if((Get-Content artifact\BICUBIC16_PRODUCTION_ORACLE.txt -Raw) -notmatch 'BICUBIC16_PIXEL_PARITY=PASS'){throw 'production Bicubic16 parity marker missing'}

# 7) Compile actual runtime with only Q720 shader bytecode changed.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_bicubic16.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'Bicubic16 runtime compile failed'}
dumpbin /headers artifact\d3d9_bicubic16.dll | Out-File artifact\BICUBIC16_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_bicubic16.dll | Out-File artifact\BICUBIC16_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_bicubic16.dll | Out-File artifact\BICUBIC16_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\BICUBIC16_HEADERS.txt -Raw;$ex=Get-Content artifact\BICUBIC16_EXPORTS.txt -Raw;$dp=Get-Content artifact\BICUBIC16_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'Bicubic16 Win8.1 x86 PE contract failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "Bicubic16 missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "Bicubic16 forbidden dependency $bad"}}
$candidateSha=(Get-FileHash artifact\d3d9_bicubic16.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$candidateSha | Set-Content artifact\D3D9_BICUBIC16_SHA256.txt -Encoding ascii

# 8) CTRL+F5/F1 integration on the actual new runtime.
New-Item -ItemType Directory -Force smoke_bicubic16 | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_bicubic16\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'Bicubic16 ring harness compile failed'}
Copy-Item artifact\d3d9_bicubic16.dll smoke_bicubic16\d3d9.dll -Force
$root=(Resolve-Path smoke_bicubic16).Path;$game=(Resolve-Path smoke_bicubic16\game.exe).Path;$dll=(Resolve-Path smoke_bicubic16\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\BICUBIC16_RING_HARNESS.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\BICUBIC16_CTRL_F5_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'Bicubic16 F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_BICUBIC16'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\BICUBIC16_CTRL_F1_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'Bicubic16 F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}

# 9) F7 timing/state regression remains frozen.
& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\BICUBIC16_F7_HARNESS.txt
if($LASTEXITCODE){throw 'Bicubic16 F7 regression'}
if((Get-Content artifact\BICUBIC16_F7_HARNESS.txt -Raw) -notmatch 'F7BENCH_ALL_GATES=PASS'){throw 'Bicubic16 F7 PASS marker missing'}

# 10) Persist candidate/provenance. Field-reference metrics are descriptive, not universal quality percentages.
Copy-Item artifact\d3d9_bicubic16.dll artifact\d3d9.dll -Force
Copy-Item compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl artifact\ptar_bicubic16_q720_ps.hlsl -Force
@(
 'PTAR_D3D9_BRANCH=HF6_TEMPORALPAIR60_F7BENCH1_SF5PARITY1_FULLQUALITY1_BICUBIC16_NEWREFS2',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('BASE_FULLQUALITY1_REBUILT_SHA256='+$baselineRuntimeSha),
 ('D3D9_BICUBIC16_SHA256='+$candidateSha),
 ('BICUBIC16_CSO_SHA256='+$prodCso),
 'Q720_SPATIAL=BICUBIC16_NEWREFS2',
 'FIELD_REFERENCE_OBJECTIVE=NATIVE_1080P_VISUAL_RESEMBLANCE',
 'FIELD_REFERENCE_INPUT=INFERRED_REAL_GAME_1280X720_FROM_ALIGNED_CAPTURE',
 'FIELD_REFERENCE_METRICS=APPROX_ALIGNED_STATIC_ROI_V2',
 'FIELD_REFERENCE_CURRENT_NV2_SSIM_APPROX=0.85368',
 'FIELD_REFERENCE_BICUBIC16_SSIM_APPROX=0.862519',
 'FIELD_REFERENCE_CURRENT_NV2_RGB_MAE_APPROX=6.042',
 'FIELD_REFERENCE_BICUBIC16_RGB_MAE_APPROX=5.5882',
 'FIELD_REFERENCE_CURRENT_NV2_EDGE_MAE_APPROX=9.978',
 'FIELD_REFERENCE_BICUBIC16_EDGE_MAE_APPROX=9.1033',
 'FIELD_REFERENCE_CURRENT_NV2_GRADIENT_RATIO_APPROX=1.018',
 'FIELD_REFERENCE_BICUBIC16_GRADIENT_RATIO_APPROX=0.8345',
 'BICUBIC16_KERNEL=CATMULL_ROM_KEYS_A_MINUS_0P5',
 'BICUBIC16_DETAIL_RESIDUAL_GAIN=0.25',
 ('BICUBIC16_TEXLD='+$texld),
 ('BICUBIC16_SLOTS='+$slots),
 'QSTAGE4=UNCHANGED',
 'HQ900=UNCHANGED',
 'SF5PARITY1_SRGB_ISOLATION=UNCHANGED',
 'FG_ALGORITHM=EXACT_HF6_UNCHANGED',
 'TEMPORAL_SCHEDULER=UNCHANGED_TEMPORALPAIR60',
 'F7_BENCHMARK=UNCHANGED_F7BENCH1',
 'CTRL_F5_F1=REGRESSION_PASS') | Set-Content artifact\PTAR_D3D9_BICUBIC16_VERSION.txt -Encoding ascii
Write-Host "BICUBIC16_CSO_SHA256=$prodCso"
Write-Host "BICUBIC16_RUNTIME_SHA256=$candidateSha"
Write-Host 'FULLQUALITY1_BICUBIC16_NEWREFS2_FULL_LAB=PASS'

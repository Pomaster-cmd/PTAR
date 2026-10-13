$ErrorActionPreference='Stop'
$fullQualityRef='origin/lab/d3d9-ptar-fullquality1-20261012'
$labRef='origin/lab/bic16-r3'
New-Item -ItemType Directory -Force artifact,build_x86 | Out-Null

# Preserve R3 while fully rebuilding/qualifying canonical FULLQUALITY1.
$candidateSource=Join-Path $env:RUNNER_TEMP 'ptar_bic16_r3_candidate.hlsl'
Copy-Item compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl $candidateSource -Force
$baseSpec=git show ($fullQualityRef+':compat/x86_d3d9/ptar_spec5_d3d9_ps.hlsl')
if($LASTEXITCODE){throw 'cannot read canonical FULLQUALITY1 SPEC5 source'}
Set-Content compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl $baseSpec -Encoding utf8
.\lab\fullquality1\run_fullquality1_full_ci.ps1
if($LASTEXITCODE){throw 'FULLQUALITY1 baseline qualification failed'}
$baselineRuntimeSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item artifact\d3d9.dll artifact\d3d9_fullquality1_rebuilt.dll -Force

# Hard scope boundary: only Q720 spatial shader source/header may differ.
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

# Restore R3 and compile production Q720 bytecode.
Copy-Item $candidateSource compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl -Force
$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_spec5_d3d9_ps_bytecode.h /Vn g_ptarSpec5Ps /Fo artifact\BIC16_R3_PROD.cso /Fc artifact\BIC16_R3_PROD_ASM.txt compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'R3 production shader compile failed'}
$asm=Get-Content artifact\BIC16_R3_PROD_ASM.txt -Raw
$texld=([regex]::Matches($asm,'(?im)^\s*texld')).Count
$slots=0;$m=[regex]::Match($asm,'(?i)approximately\s+(\d+)\s+instruction slots');if($m.Success){$slots=[int]$m.Groups[1].Value}
if($texld -ne 16){throw "R3 expected 16 texld, got $texld"}
if($slots -ne 154){throw "R3 expected 154 slots, got $slots"}
$prodCso=(Get-FileHash artifact\BIC16_R3_PROD.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($prodCso -ne '134f513ad18af35336c8881ec0504881a51e0fadc9f311dfa50646e4e878cc46'){throw "unexpected R3 CSO $prodCso"}

# Independent branch-source compile must produce byte-identical DXBC.
$refSource=Join-Path $env:RUNNER_TEMP 'ptar_bic16_r3_reference.hlsl'
$refText=git show ($labRef+':compat/x86_d3d9/ptar_spec5_d3d9_ps.hlsl')
if($LASTEXITCODE){throw 'cannot retrieve R3 reference source'}
Set-Content $refSource $refText -Encoding utf8
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fo artifact\BIC16_R3_REF.cso $refSource
if($LASTEXITCODE){throw 'R3 reference shader compile failed'}
$refCso=(Get-FileHash artifact\BIC16_R3_REF.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($refCso -ne $prodCso){throw "R3 DXBC identity failed prod=$prodCso ref=$refCso"}
@(
 'BIC16_R3_DXBC_IDENTITY=PASS',
 ('CSO_SHA256='+$prodCso),
 'TEXLD=16',
 'SLOTS=154',
 'KERNEL=CATMULL_ROM_KEYS_A_MINUS_0P5',
 'DETAIL_RESIDUAL_GAIN=1.10',
 'LOCAL_CLAMP_MARGIN=0.10',
 'LOCAL_CLAMP_SOURCE=CENTRAL_2X2_MINMAX',
 'EXTRA_FULLSCREEN_PASS=0') | Set-Content artifact\BIC16_R3_IDENTITY.txt -Encoding ascii

# Scope identity after candidate insertion.
foreach($rel in $immutable){$after=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant();if($after -ne $before[$rel]){throw "R3 modified forbidden source: $rel"}}
if((Get-FileHash build_x86\ptar_qstage4diag2_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant() -ne $q4HeaderBefore){throw 'QSTAGE4 bytecode changed'}
if((Get-FileHash build_x86\ptar_hq900_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hqHeaderBefore){throw 'HQ900 bytecode changed'}
@(
 'PROXY_IDENTICAL=PASS',
 'QSTAGE4_IDENTICAL=PASS',
 'HQ900_IDENTICAL=PASS',
 'HF6_IDENTICAL=PASS',
 'TEMPORALPAIR60_IDENTICAL=PASS',
 'F7_IDENTICAL=PASS',
 'SF5PARITY1_SRGB_WRAPPER_IDENTICAL=PASS') | Set-Content artifact\BIC16_R3_SCOPE.txt -Encoding ascii

# Selector must remain exact FULLQUALITY1 routing.
python .\lab\fullquality1\validate_fullquality_selector.py 2>&1 | Tee-Object artifact\BIC16_R3_SELECTOR.txt
if($LASTEXITCODE){throw 'R3 selector regression'}
if((Get-Content artifact\BIC16_R3_SELECTOR.txt -Raw) -notmatch 'FULLQUALITY_SELECTOR_ALL=PASS'){throw 'R3 selector PASS marker missing'}

# GPU pixel oracle against independent CPU R3 reference.
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_bicubic16_oracle_bytecode.h /Vn g_ptarBicubic16Ps /Fo artifact\BIC16_R3_ORACLE.cso compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'R3 oracle shader compile failed'}
$oracleCso=(Get-FileHash artifact\BIC16_R3_ORACLE.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($oracleCso -ne $prodCso){throw "R3 oracle DXBC differs oracle=$oracleCso prod=$prodCso"}
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\bicubic16\bicubic16_gpu_oracle.cpp /link d3d9.lib user32.lib /OUT:artifact\bic16_r3_oracle.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R3 oracle compile failed'}
& artifact\bic16_r3_oracle.exe 2>&1 | Tee-Object artifact\BIC16_R3_ORACLE.txt
if($LASTEXITCODE){throw 'R3 GPU oracle failed'}
if((Get-Content artifact\BIC16_R3_ORACLE.txt -Raw) -notmatch 'BICUBIC16_PIXEL_PARITY=PASS'){throw 'R3 oracle PASS marker missing'}

# Isolated spatial performance regression check versus canonical SPEC5.
.\lab\bicubic16\run_bicubic16_perf.ps1
if($LASTEXITCODE){throw 'R3 spatial perf gate failed'}

# Compile actual x86 Win8.1 runtime with only Q720 bytecode changed.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_r3.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'R3 runtime compile failed'}
dumpbin /headers artifact\d3d9_r3.dll | Out-File artifact\R3_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_r3.dll | Out-File artifact\R3_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_r3.dll | Out-File artifact\R3_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\R3_HEADERS.txt -Raw;$ex=Get-Content artifact\R3_EXPORTS.txt -Raw;$dp=Get-Content artifact\R3_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'R3 Win8.1 x86 PE contract failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "R3 missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "R3 forbidden dependency $bad"}}
$candidateSha=(Get-FileHash artifact\d3d9_r3.dll -Algorithm SHA256).Hash.ToLowerInvariant()

# CTRL+F5/F1 integration on the actual R3 runtime.
New-Item -ItemType Directory -Force smoke_r3 | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_r3\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R3 ring harness compile failed'}
Copy-Item artifact\d3d9_r3.dll smoke_r3\d3d9.dll -Force
$root=(Resolve-Path smoke_r3).Path;$game=(Resolve-Path smoke_r3\game.exe).Path;$dll=(Resolve-Path smoke_r3\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\R3_RING.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\R3_F5.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R3 F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_R3'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\R3_F1.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R3 F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}

# Frozen F7 timing/state regression.
& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\R3_F7.txt
if($LASTEXITCODE){throw 'R3 F7 regression'}
if((Get-Content artifact\R3_F7.txt -Raw) -notmatch 'F7BENCH_ALL_GATES=PASS'){throw 'R3 F7 PASS marker missing'}

# Final candidate/provenance.
Copy-Item artifact\d3d9_r3.dll artifact\d3d9.dll -Force
Copy-Item compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl artifact\bic16_r3.hlsl -Force
@(
 'PTAR_D3D9_BRANCH=HF6_TEMPORALPAIR60_F7BENCH1_SF5PARITY1_FULLQUALITY1_BIC16_R3',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('BASE_FULLQUALITY1_REBUILT_SHA256='+$baselineRuntimeSha),
 ('D3D9_R3_SHA256='+$candidateSha),
 ('BIC16_R3_CSO_SHA256='+$prodCso),
 'Q720_SPATIAL=BIC16_R3',
 'OBJECTIVE=NATIVE_1080P_VISUAL_RESEMBLANCE',
 'FIELD_REFERENCE=ALIGNED_20261013_PTAR720_VS_NATIVE1080',
 'R3_GAIN=1.10',
 'R3_LOCAL_CLAMP_MARGIN=0.10',
 'R3_LOCAL_CLAMP_SOURCE=CENTRAL_2X2_MINMAX',
 'R3_TEXLD=16',
 'R3_SLOTS=154',
 'QSTAGE4=UNCHANGED',
 'HQ900=UNCHANGED',
 'SF5PARITY1_SRGB_ISOLATION=UNCHANGED',
 'FG_ALGORITHM=EXACT_HF6_UNCHANGED',
 'TEMPORAL_SCHEDULER=UNCHANGED_TEMPORALPAIR60',
 'F7_BENCHMARK=UNCHANGED_F7BENCH1',
 'CTRL_F5_F1=REGRESSION_PASS') | Set-Content artifact\PTAR_R3_VERSION.txt -Encoding ascii
Write-Host "BIC16_R3_CSO_SHA256=$prodCso"
Write-Host "BIC16_R3_RUNTIME_SHA256=$candidateSha"
Write-Host 'FULLQUALITY1_BIC16_R3_FULL_LAB=PASS'

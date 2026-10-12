$ErrorActionPreference='Stop'

$fullQualityRef='origin/lab/d3d9-ptar-fullquality1-20261012'
New-Item -ItemType Directory -Force artifact | Out-Null

# Preserve selected NativeVisual2 production source while rebuilding exact FULLQUALITY1.
$candidateSource=Join-Path $env:RUNNER_TEMP 'ptar_spec5_nativevisual2_candidate.hlsl'
Copy-Item compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl $candidateSource -Force

# Restore canonical FULLQUALITY1 SPEC5 only for baseline reconstruction.
$baseSpec=git show ($fullQualityRef+':compat/x86_d3d9/ptar_spec5_d3d9_ps.hlsl')
if($LASTEXITCODE){throw 'cannot read canonical FULLQUALITY1 SPEC5 source'}
Set-Content compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl $baseSpec -Encoding utf8

# 1) Reconstruct and fully qualify canonical FULLQUALITY1 lineage.
.\lab\fullquality1\run_fullquality1_full_ci.ps1
if($LASTEXITCODE){throw 'FULLQUALITY1 baseline qualification failed'}
$baselineRuntimeSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item artifact\d3d9.dll artifact\d3d9_fullquality1_rebuilt_baseline.dll -Force
Copy-Item compat\x86_d3d9\ptar_d3d9_proxy.cpp artifact\ptar_d3d9_proxy_fullquality1_baseline.cpp -Force

# Snapshot every subsystem forbidden to change in this spatial-only experiment.
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

# 2) Restore NativeVisual2 source and compile ONLY SPEC5 bytecode.
Copy-Item $candidateSource compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl -Force
$fxc=Get-Command fxc.exe -ErrorAction SilentlyContinue
if(-not $fxc){throw 'fxc.exe unavailable after FULLQUALITY1 qualification'}
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}

& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_spec5_d3d9_ps_bytecode.h /Vn g_ptarSpec5Ps /Fo artifact\SPEC5_NATIVEVISUAL2_PRODUCTION.cso /Fc artifact\SPEC5_NATIVEVISUAL2_PRODUCTION_ASM.txt compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'production NativeVisual2 compile failed'}
$asm=Get-Content artifact\SPEC5_NATIVEVISUAL2_PRODUCTION_ASM.txt -Raw
$texld=([regex]::Matches($asm,'(?im)^\s*texld')).Count
$slots=0;$m=[regex]::Match($asm,'(?i)approximately\s+(\d+)\s+instruction slots');if($m.Success){$slots=[int]$m.Groups[1].Value}
if($texld -ne 20){throw "NativeVisual2 expected 20 texld, got $texld"}
if($slots -ne 340){throw "NativeVisual2 expected 340 slots, got $slots"}

# 3) Independently compile exact lab reference and require byte-identical DXBC.
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fo artifact\SPEC5_NATIVEVISUAL2_LAB_REFERENCE.cso lab\nativevisual2\ptar_spec5_nativevisual2_ps.hlsl
if($LASTEXITCODE){throw 'NativeVisual2 lab reference compile failed'}
$prodCso=(Get-FileHash artifact\SPEC5_NATIVEVISUAL2_PRODUCTION.cso -Algorithm SHA256).Hash.ToLowerInvariant()
$refCso=(Get-FileHash artifact\SPEC5_NATIVEVISUAL2_LAB_REFERENCE.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($prodCso -ne $refCso){throw "production NativeVisual2 DXBC differs from lab reference prod=$prodCso ref=$refCso"}
@(
 'SPEC5_NATIVEVISUAL2_LAB_DXBC_IDENTITY=PASS',
 ('SPEC5_NATIVEVISUAL2_CSO_SHA256='+$prodCso),
 'SPEC5_NATIVEVISUAL2_TEXLD=20',
 'SPEC5_NATIVEVISUAL2_SLOTS=340',
 'SPEC5_NATIVEVISUAL2_EXTRA_TEXTURE_TAPS=0',
 'SPEC5_NATIVEVISUAL2_EXTRA_FULLSCREEN_PASS=0',
 'SPEC5_NATIVEVISUAL2_DIRECTIONAL_AA=EXISTING_CARDINALS_ONLY',
 'SPEC5_NATIVEVISUAL2_AA_MAX_STRENGTH=0.075') | Set-Content artifact\SPEC5_NATIVEVISUAL2_IDENTITY.txt -Encoding ascii

# 4) Hard identity boundary: proxy, QSTAGE4, HQ900, HF6, temporal and F7 must not move.
foreach($rel in $immutable){
  $after=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant()
  if($after -ne $before[$rel]){throw "NativeVisual2 modified forbidden source: $rel"}
}
if((Get-FileHash build_x86\ptar_qstage4diag2_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant() -ne $q4HeaderBefore){throw 'QSTAGE4 bytecode header changed'}
if((Get-FileHash build_x86\ptar_hq900_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hqHeaderBefore){throw 'HQ900 bytecode header changed'}
@(
 'SPEC5_NATIVEVISUAL2_PROXY_BYTE_IDENTICAL=PASS',
 'SPEC5_NATIVEVISUAL2_QSTAGE4_BYTE_IDENTICAL=PASS',
 'SPEC5_NATIVEVISUAL2_HQ900_BYTE_IDENTICAL=PASS',
 'SPEC5_NATIVEVISUAL2_HF6_BYTE_IDENTICAL=PASS',
 'SPEC5_NATIVEVISUAL2_TEMPORALPAIR60_BYTE_IDENTICAL=PASS',
 'SPEC5_NATIVEVISUAL2_F7_BYTE_IDENTICAL=PASS',
 'SPEC5_NATIVEVISUAL2_SF5PARITY1_WRAPPER_BYTE_IDENTICAL=PASS') | Set-Content artifact\SPEC5_NATIVEVISUAL2_SCOPE_IDENTITY.txt -Encoding ascii

# 5) Selector remains unchanged: exact Q720 still routes only through SPEC5.
python .\lab\fullquality1\validate_fullquality_selector.py 2>&1 | Tee-Object artifact\SPEC5_NATIVEVISUAL2_SELECTOR.txt
if($LASTEXITCODE){throw 'selector regression'}
if((Get-Content artifact\SPEC5_NATIVEVISUAL2_SELECTOR.txt -Raw) -notmatch 'FULLQUALITY_SELECTOR_ALL=PASS'){throw 'selector PASS marker missing'}

# 6) Compile actual D3D9 runtime with only selected SPEC5 bytecode changed.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_nativevisual2.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'NativeVisual2 runtime compile failed'}
dumpbin /headers artifact\d3d9_nativevisual2.dll | Out-File artifact\NATIVEVISUAL2_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_nativevisual2.dll | Out-File artifact\NATIVEVISUAL2_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_nativevisual2.dll | Out-File artifact\NATIVEVISUAL2_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\NATIVEVISUAL2_HEADERS.txt -Raw;$ex=Get-Content artifact\NATIVEVISUAL2_EXPORTS.txt -Raw;$dp=Get-Content artifact\NATIVEVISUAL2_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'NativeVisual2 Win8.1 x86 PE contract failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "NativeVisual2 missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "NativeVisual2 forbidden dependency $bad"}}
$candidateSha=(Get-FileHash artifact\d3d9_nativevisual2.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$candidateSha | Set-Content artifact\D3D9_NATIVEVISUAL2_SHA256.txt -Encoding ascii

# 7) Re-run CTRL+F5 and CTRL+F1 against NEW runtime.
New-Item -ItemType Directory -Force smoke_nativevisual2 | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_nativevisual2\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'NativeVisual2 ring harness compile failed'}
Copy-Item artifact\d3d9_nativevisual2.dll smoke_nativevisual2\d3d9.dll -Force
$root=(Resolve-Path smoke_nativevisual2).Path;$game=(Resolve-Path smoke_nativevisual2\game.exe).Path;$dll=(Resolve-Path smoke_nativevisual2\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\NATIVEVISUAL2_RING_HARNESS.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\NATIVEVISUAL2_CTRL_F5_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'NativeVisual2 F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_NATIVEVISUAL2'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\NATIVEVISUAL2_CTRL_F1_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'NativeVisual2 F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}

# 8) F7 state/timing regression.
& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\NATIVEVISUAL2_F7_HARNESS.txt
if($LASTEXITCODE){throw 'NativeVisual2 F7 regression'}
if((Get-Content artifact\NATIVEVISUAL2_F7_HARNESS.txt -Raw) -notmatch 'F7BENCH_ALL_GATES=PASS'){throw 'NativeVisual2 F7 PASS marker missing'}

# 9) Persist candidate and corrected visual-scoring provenance.
Copy-Item artifact\d3d9_nativevisual2.dll artifact\d3d9.dll -Force
Copy-Item compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl artifact\ptar_spec5_nativevisual2_d3d9_ps.hlsl -Force
@(
 'PTAR_D3D9_BRANCH=HF6_TEMPORALPAIR60_F7BENCH1_SF5PARITY1_FULLQUALITY1_NATIVEVISUAL2_AA075',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('BASE_FULLQUALITY1_REBUILT_SHA256='+$baselineRuntimeSha),
 ('D3D9_NATIVEVISUAL2_SHA256='+$candidateSha),
 ('SPEC5_NATIVEVISUAL2_CSO_SHA256='+$prodCso),
 'Q720_SPATIAL=SPEC5_NATIVEVISUAL2_AA075',
 'PRIMARY_OBJECTIVE=FINAL_VISUAL_RESEMBLANCE_TO_NATIVE_1080P',
 'OLD_MS_SSIM_ONLY_PERCENT=WITHDRAWN_AS_INSUFFICIENT',
 'FINAL_VISUAL_SCORE_INTERNAL_NOT_STANDARDIZED_PERCENT',
 'FINAL_VISUAL_SCORE_WEIGHTS=REAL1080_55_STRESS_ALIAS_25_TEMPORAL_20',
 'NATIVEVISUAL2_AA075_FINAL_SCORE=85.73311',
 'NATIVELIKE2_FINAL_SCORE=85.71883',
 'C14_NOAA_FINAL_SCORE=85.69987',
 'SPEC5_NATIVEVISUAL2_SHARP=1.40',
 'SPEC5_NATIVEVISUAL2_BASE_GAIN=0.70',
 'SPEC5_NATIVEVISUAL2_DETAIL_GAIN=0.15',
 'SPEC5_NATIVEVISUAL2_AA_MAX_STRENGTH=0.075',
 'SPEC5_NATIVEVISUAL2_AA_TAPS=0_EXTRA_EXISTING_CARDINALS_ONLY',
 'SPEC5_NATIVEVISUAL2_TOTAL_TEXLD=20',
 'SPEC5_NATIVEVISUAL2_SLOTS=340',
 'QSTAGE4=UNCHANGED',
 'HQ900=UNCHANGED',
 'SF5PARITY1_SRGB_ISOLATION=UNCHANGED',
 'FG_ALGORITHM=EXACT_HF6_UNCHANGED',
 'TEMPORAL_SCHEDULER=UNCHANGED_TEMPORALPAIR60',
 'F7_BENCHMARK=UNCHANGED_F7BENCH1',
 'CTRL_F5_F1=REGRESSION_PASS') | Set-Content artifact\PTAR_D3D9_NATIVEVISUAL2_VERSION.txt -Encoding ascii

Write-Host "NATIVEVISUAL2_RUNTIME_SHA256=$candidateSha"
Write-Host 'FULLQUALITY1_NATIVEVISUAL2_FULL_LAB=PASS'

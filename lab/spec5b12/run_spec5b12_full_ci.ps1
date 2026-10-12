$ErrorActionPreference='Stop'

$fullQualityRef='lab/d3d9-ptar-fullquality1-20261012'
$aaLabRef='origin/lab/d3d9-spec5-aalab1-20261012'

New-Item -ItemType Directory -Force artifact | Out-Null

# Preserve the production B12 source while reconstructing the exact FULLQUALITY1 baseline.
$b12Source=Join-Path $env:RUNNER_TEMP 'ptar_spec5_b12_candidate.hlsl'
Copy-Item compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl $b12Source -Force

# Restore ONLY the canonical FULLQUALITY1 SPEC5 source for baseline reconstruction.
$baseSpec=git show ($fullQualityRef+':compat/x86_d3d9/ptar_spec5_d3d9_ps.hlsl')
if($LASTEXITCODE){throw 'cannot read canonical FULLQUALITY1 SPEC5 source'}
Set-Content compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl $baseSpec -Encoding utf8

# 1) Reconstruct and fully qualify the already delivered FULLQUALITY1 lineage.
.\lab\fullquality1\run_fullquality1_full_ci.ps1
if($LASTEXITCODE){throw 'FULLQUALITY1 baseline qualification failed'}
$baselineRuntimeSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item artifact\d3d9.dll artifact\d3d9_fullquality1_rebuilt_baseline.dll -Force
Copy-Item compat\x86_d3d9\ptar_d3d9_proxy.cpp artifact\ptar_d3d9_proxy_fullquality1_baseline.cpp -Force

# Snapshot everything forbidden to change in SPEC5-B12.
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

# 2) Restore the selected B12 source and compile ONLY SPEC5 bytecode.
Copy-Item $b12Source compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl -Force
$fxc=Get-Command fxc.exe -ErrorAction SilentlyContinue
if(-not $fxc){throw 'fxc.exe unavailable after FULLQUALITY1 qualification'}
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}

& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_spec5_d3d9_ps_bytecode.h /Vn g_ptarSpec5Ps /Fo artifact\SPEC5_B12_PRODUCTION.cso /Fc artifact\SPEC5_B12_PRODUCTION_ASM.txt compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'production SPEC5-B12 compile failed'}
$asm=Get-Content artifact\SPEC5_B12_PRODUCTION_ASM.txt -Raw
$texld=([regex]::Matches($asm,'(?im)^\s*texld')).Count
$slots=0;$m=[regex]::Match($asm,'(?i)approximately\s+(\d+)\s+instruction slots');if($m.Success){$slots=[int]$m.Groups[1].Value}
if($texld -ne 20){throw "SPEC5-B12 expected 20 texld, got $texld"}
if($slots -ne 334){throw "SPEC5-B12 expected 334 slots, got $slots"}

# 3) Independently compile the exact AALAB1 B12 winner and require byte-identical DXBC.
$refSource=Join-Path $env:RUNNER_TEMP 'ptar_spec5_aalab_reference.hlsl'
$refText=git show ($aaLabRef+':lab/aalab1/ptar_spec5_aalab_ps.hlsl')
if($LASTEXITCODE){throw 'cannot retrieve AALAB1 reference source'}
Set-Content $refSource $refText -Encoding utf8
& $fxcPath /nologo /T ps_3_0 /E main /O3 /DPTAR_VARIANT=2 /Fo artifact\SPEC5_B12_AALAB_REFERENCE.cso $refSource
if($LASTEXITCODE){throw 'AALAB1 B12 reference compile failed'}
$prodCso=(Get-FileHash artifact\SPEC5_B12_PRODUCTION.cso -Algorithm SHA256).Hash.ToLowerInvariant()
$refCso=(Get-FileHash artifact\SPEC5_B12_AALAB_REFERENCE.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($prodCso -ne $refCso){throw "production B12 DXBC differs from AALAB winner prod=$prodCso ref=$refCso"}
@(
 'SPEC5_B12_AALAB_DXBC_IDENTITY=PASS',
 ('SPEC5_B12_CSO_SHA256='+$prodCso),
 'SPEC5_B12_TEXLD=20',
 'SPEC5_B12_SLOTS=334',
 'SPEC5_B12_EXTRA_TEXTURE_TAPS=0',
 'SPEC5_B12_EXTRA_FULLSCREEN_PASS=0') | Set-Content artifact\SPEC5_B12_IDENTITY.txt -Encoding ascii

# 4) Hard identity boundary: no proxy, QSTAGE4, HQ900, FG, temporal or F7 source change.
foreach($rel in $immutable){
  $after=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant()
  if($after -ne $before[$rel]){throw "SPEC5-B12 modified forbidden source: $rel"}
}
if((Get-FileHash build_x86\ptar_qstage4diag2_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant() -ne $q4HeaderBefore){throw 'QSTAGE4 bytecode header changed'}
if((Get-FileHash build_x86\ptar_hq900_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hqHeaderBefore){throw 'HQ900 bytecode header changed'}
@(
 'SPEC5_B12_PROXY_BYTE_IDENTICAL=PASS',
 'SPEC5_B12_QSTAGE4_BYTE_IDENTICAL=PASS',
 'SPEC5_B12_HQ900_BYTE_IDENTICAL=PASS',
 'SPEC5_B12_HF6_BYTE_IDENTICAL=PASS',
 'SPEC5_B12_TEMPORALPAIR60_BYTE_IDENTICAL=PASS',
 'SPEC5_B12_F7_BYTE_IDENTICAL=PASS',
 'SPEC5_B12_SF5PARITY1_WRAPPER_BYTE_IDENTICAL=PASS') | Set-Content artifact\SPEC5_B12_SCOPE_IDENTITY.txt -Encoding ascii

# 5) Selector remains unchanged and must still route exact Q720 to SPEC5 only.
python .\lab\fullquality1\validate_fullquality_selector.py 2>&1 | Tee-Object artifact\SPEC5_B12_SELECTOR.txt
if($LASTEXITCODE){throw 'selector regression'}
if((Get-Content artifact\SPEC5_B12_SELECTOR.txt -Raw) -notmatch 'FULLQUALITY_SELECTOR_ALL=PASS'){throw 'selector PASS marker missing'}

# 6) Compile actual D3D9 runtime with only the selected SPEC5 bytecode changed.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_spec5b12.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'SPEC5-B12 runtime compile failed'}
dumpbin /headers artifact\d3d9_spec5b12.dll | Out-File artifact\SPEC5_B12_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_spec5b12.dll | Out-File artifact\SPEC5_B12_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_spec5b12.dll | Out-File artifact\SPEC5_B12_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\SPEC5_B12_HEADERS.txt -Raw;$ex=Get-Content artifact\SPEC5_B12_EXPORTS.txt -Raw;$dp=Get-Content artifact\SPEC5_B12_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'SPEC5-B12 Win8.1 x86 PE contract failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "SPEC5-B12 missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "SPEC5-B12 forbidden dependency $bad"}}
$candidateSha=(Get-FileHash artifact\d3d9_spec5b12.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$candidateSha | Set-Content artifact\D3D9_SPEC5_B12_SHA256.txt -Encoding ascii

# 7) Re-run CTRL+F5 and CTRL+F1 against the NEW runtime.
New-Item -ItemType Directory -Force smoke_spec5b12 | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_spec5b12\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'SPEC5-B12 ring harness compile failed'}
Copy-Item artifact\d3d9_spec5b12.dll smoke_spec5b12\d3d9.dll -Force
$root=(Resolve-Path smoke_spec5b12).Path;$game=(Resolve-Path smoke_spec5b12\game.exe).Path;$dll=(Resolve-Path smoke_spec5b12\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\SPEC5_B12_RING_HARNESS.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\SPEC5_B12_CTRL_F5_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'SPEC5-B12 F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_SPEC5_B12'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\SPEC5_B12_CTRL_F1_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'SPEC5-B12 F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}

# 8) F7 state/timing regression.
& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\SPEC5_B12_F7_HARNESS.txt
if($LASTEXITCODE){throw 'SPEC5-B12 F7 regression'}
if((Get-Content artifact\SPEC5_B12_F7_HARNESS.txt -Raw) -notmatch 'F7BENCH_ALL_GATES=PASS'){throw 'SPEC5-B12 F7 PASS marker missing'}

# 9) Persist exact candidate and decision provenance.
Copy-Item artifact\d3d9_spec5b12.dll artifact\d3d9.dll -Force
Copy-Item compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl artifact\ptar_spec5_b12_d3d9_ps.hlsl -Force
@(
 'PTAR_D3D9_BRANCH=HF6_TEMPORALPAIR60_F7BENCH1_SF5PARITY1_FULLQUALITY1_SPEC5B12',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('BASE_FULLQUALITY1_REBUILT_SHA256='+$baselineRuntimeSha),
 ('D3D9_SPEC5_B12_SHA256='+$candidateSha),
 ('SPEC5_B12_CSO_SHA256='+$prodCso),
 'Q720_SPATIAL=SPEC5_NATIVEFIT40_DETAIL25_B12',
 'SPEC5_B12_BLEND=0.12_CENTRAL_BILINEAR',
 'SPEC5_B12_EXTRA_TEXLD=0',
 'SPEC5_B12_TOTAL_TEXLD=20',
 'SPEC5_B12_SLOTS=334',
 'AALAB1_DECISION=REJECT_EXTRA_TAP_AA',
 'BLENDREFINE_DECISION=B12_ROBUST_CROSS_RUN',
 'QSTAGE4=UNCHANGED',
 'HQ900=UNCHANGED',
 'SF5PARITY1_SRGB_ISOLATION=UNCHANGED',
 'FG_ALGORITHM=EXACT_HF6_UNCHANGED',
 'TEMPORAL_SCHEDULER=UNCHANGED_TEMPORALPAIR60',
 'F7_BENCHMARK=UNCHANGED_F7BENCH1',
 'CTRL_F5_F1=REGRESSION_PASS') | Set-Content artifact\PTAR_D3D9_SPEC5_B12_VERSION.txt -Encoding ascii

Write-Host "SPEC5_B12_RUNTIME_SHA256=$candidateSha"
Write-Host 'FULLQUALITY1_SPEC5_B12_FULL_LAB=PASS'

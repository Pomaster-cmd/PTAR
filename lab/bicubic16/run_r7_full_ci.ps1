$ErrorActionPreference='Stop'

# R7 is layered on the fully-qualified R6 runtime. This deliberately re-runs
# the entire R3 -> R4 -> R5 -> R6 lineage before changing the Q720 shader.
. .\lab\bicubic16\run_r6_full_ci.ps1
if($LASTEXITCODE){throw 'R6 base qualification failed'}
$r6RuntimeSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$r6CsoSha=(Get-FileHash artifact\BIC16_R6_PROD.cso -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item artifact\d3d9.dll artifact\d3d9_r6_base_for_r7.dll -Force
if(Test-Path artifact\BICUBIC16_PERF.txt){Copy-Item artifact\BICUBIC16_PERF.txt artifact\R6_PERF_BASE.txt -Force}

# Spatial-only scope boundary.
$immutable=@(
 'compat/x86_d3d9/ptar_d3d9_proxy.cpp',
 'compat/x86_d3d9/ptar_hud.h',
 'compat/x86_d3d9/ptar_gw16i_hud_d3d9.h',
 'compat/x86_d3d9/ptar_qstage4diag2_d3d9_ps.hlsl',
 'compat/x86_d3d9/ptar_hq900_d3d9_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_pacer.h',
 'compat/x86_d3d9/ptar_fg_me_coarse_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_me_refine_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_interpolate_ps.hlsl',
 'compat/x86_d3d9/ptar_f7_benchmark.h',
 'compat/x86_d3d9/ptar_resolution_policy.h')
$before=@{}
foreach($rel in $immutable){$before[$rel]=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant()}

# Install only R7 into the production Q720 spatial shader slot.
Copy-Item lab\bicubic16\bic16_r7_edgecubic1.hlsl compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl -Force
$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_spec5_d3d9_ps_bytecode.h /Vn g_ptarSpec5Ps /Fo artifact\BIC16_R7_PROD.cso /Fc artifact\BIC16_R7_PROD_ASM.txt compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'R7 production shader compile failed'}
$asm=Get-Content artifact\BIC16_R7_PROD_ASM.txt -Raw
$texld=([regex]::Matches($asm,'(?im)^\s*texld')).Count
$m=[regex]::Match($asm,'(?i)approximately\s+(\d+)\s+instruction slots')
if(-not $m.Success){throw 'R7 slot count missing'}
$slots=[int]$m.Groups[1].Value
if($texld -ne 16){throw "R7 expected 16 texld, got $texld"}
if($slots -gt 300){throw "R7 unexpected ALU growth: $slots slots"}
$r7CsoSha=(Get-FileHash artifact\BIC16_R7_PROD.cso -Algorithm SHA256).Hash.ToLowerInvariant()

# Independent compile identity.
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fo artifact\BIC16_R7_REF.cso lab\bicubic16\bic16_r7_edgecubic1.hlsl
if($LASTEXITCODE){throw 'R7 reference compile failed'}
$r7RefSha=(Get-FileHash artifact\BIC16_R7_REF.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($r7RefSha -ne $r7CsoSha){throw "R7 DXBC identity failed prod=$r7CsoSha ref=$r7RefSha"}

# Independent CPU/GPU pixel oracle.
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_bicubic16_r7_oracle_bytecode.h /Vn g_ptarBicubic16R7Ps /Fo artifact\BIC16_R7_ORACLE.cso lab\bicubic16\bic16_r7_edgecubic1.hlsl
if($LASTEXITCODE){throw 'R7 oracle shader compile failed'}
if((Get-FileHash artifact\BIC16_R7_ORACLE.cso -Algorithm SHA256).Hash.ToLowerInvariant() -ne $r7CsoSha){throw 'R7 oracle DXBC differs from production'}
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\bicubic16\bicubic16_r7_oracle.cpp /link d3d9.lib user32.lib /OUT:artifact\bic16_r7_oracle.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R7 oracle compile failed'}
& artifact\bic16_r7_oracle.exe 2>&1 | Tee-Object artifact\BIC16_R7_ORACLE.txt
if($LASTEXITCODE){throw 'R7 GPU oracle failed'}
if((Get-Content artifact\BIC16_R7_ORACLE.txt -Raw) -notmatch 'BIC16_R7_PIXEL_PARITY=PASS'){throw 'R7 oracle PASS marker missing'}

# Full-resolution GPU timing. Keep both R6 and R7 records from the same job.
.\lab\bicubic16\run_bicubic16_perf.ps1
if($LASTEXITCODE){throw 'R7 spatial perf gate failed'}
Copy-Item artifact\BICUBIC16_PERF.txt artifact\R7_PERF.txt -Force
$r6Perf=Get-Content artifact\R6_PERF_BASE.txt -Raw
$r7Perf=Get-Content artifact\R7_PERF.txt -Raw
$m6=[regex]::Match($r6Perf,'BIC16_PERF_BICUBIC16_MEDIAN_MS=([0-9.]+)')
$m7=[regex]::Match($r7Perf,'BIC16_PERF_BICUBIC16_MEDIAN_MS=([0-9.]+)')
if(-not $m6.Success -or -not $m7.Success){throw 'R6/R7 perf median parse failed'}
$r6Ms=[double]$m6.Groups[1].Value;$r7Ms=[double]$m7.Groups[1].Value
$r7VsR6=($r7Ms/$r6Ms-1.0)*100.0
if($r7Ms -gt $r6Ms*1.35){throw "R7 spatial cost regression too large: R6=$r6Ms R7=$r7Ms"}

# Actual Win8.1 x86 runtime.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_r7.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'R7 runtime compile failed'}
dumpbin /headers artifact\d3d9_r7.dll | Out-File artifact\R7_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_r7.dll | Out-File artifact\R7_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_r7.dll | Out-File artifact\R7_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\R7_HEADERS.txt -Raw;$ex=Get-Content artifact\R7_EXPORTS.txt -Raw;$dp=Get-Content artifact\R7_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'R7 Win8.1 x86 PE contract failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "R7 missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "R7 forbidden dependency $bad"}}
$r7RuntimeSha=(Get-FileHash artifact\d3d9_r7.dll -Algorithm SHA256).Hash.ToLowerInvariant()

foreach($rel in $immutable){$after=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant();if($after -ne $before[$rel]){throw "R7 modified forbidden source: $rel"}}

# R4 POINT/PTAR A-B controls remain mandatory.
$hud=Get-Content compat\x86_d3d9\ptar_hud.h -Raw
$proxy=Get-Content compat\x86_d3d9\ptar_d3d9_proxy.cpp -Raw
foreach($tok in @('g_ptarHudFilterMode=2','g_ptarHudFilterMode=(g_ptarHudFilterMode==0)?2:0','g_ptarHudFilterMode=(g_ptarHudFilterMode+1)%3')){if($hud -notmatch [regex]::Escape($tok)){throw "R7 lost R4 HUD token $tok"}}
foreach($tok in @('POINT_RAW720_REF','D3DTEXF_POINT','POINT_RAW720_STRETCHRECT_FAIL')){if($proxy -notmatch [regex]::Escape($tok)){throw "R7 lost R4 proxy token $tok"}}

# Runtime integration regressions on the actual R7 DLL.
New-Item -ItemType Directory -Force smoke_r7 | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_r7\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R7 ring harness compile failed'}
Copy-Item artifact\d3d9_r7.dll smoke_r7\d3d9.dll -Force
$root=(Resolve-Path smoke_r7).Path;$game=(Resolve-Path smoke_r7\game.exe).Path;$dll=(Resolve-Path smoke_r7\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\R7_RING.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\R7_F5.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R7 F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_R7'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\R7_F1.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R7 F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}
& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\R7_F7.txt
if($LASTEXITCODE){throw 'R7 F7 regression'}
if((Get-Content artifact\R7_F7.txt -Raw) -notmatch 'F7BENCH_ALL_GATES=PASS'){throw 'R7 F7 PASS marker missing'}

Copy-Item artifact\d3d9_r7.dll artifact\d3d9.dll -Force
Copy-Item lab\bicubic16\bic16_r7_edgecubic1.hlsl artifact\bic16_r7_edgecubic1.hlsl -Force
@(
 'PTAR_D3D9_BRANCH=BIC16_R7_EDGECUBIC1_OVER_R6_OVER_R4_POINTAB',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('R6_BASE_RUNTIME_SHA256='+$r6RuntimeSha),
 ('R6_BASE_CSO_SHA256='+$r6CsoSha),
 ('R7_CSO_SHA256='+$r7CsoSha),
 ('R7_RUNTIME_SHA256='+$r7RuntimeSha),
 ('R7_TEXLD='+$texld),
 ('R7_SLOTS='+$slots),
 ('R6_SPATIAL_MEDIAN_MS='+$r6Ms),
 ('R7_SPATIAL_MEDIAN_MS='+$r7Ms),
 ('R7_VS_R6_SPATIAL_PCT='+$r7VsR6),
 'Q720_SPATIAL=BIC16_R7_EDGECUBIC1',
 'KEYS_A_SIDE_PHASE=-1.35',
 'KEYS_A_HALF_PHASE=-1.20',
 'KEYS_A_STRONG_CROSS_EDGE=-0.50',
 'EDGE_AXIS_START=0.02',
 'EDGE_AXIS_SCALE=4.0',
 'CLAMP_MARGIN_LOW_CONTRAST=0.12',
 'CLAMP_MARGIN_HIGH_CONTRAST=0.04',
 'EXPLICIT_DETAIL_BOOST=0',
 'EXTRA_TEXTURE_FETCHES=0',
 'R4_POINTAB=UNCHANGED',
 'HF6=UNCHANGED',
 'TEMPORALPAIR60=UNCHANGED',
 'F7=PASS',
 'CTRL_F5_F1=PASS',
 'GPU_PIXEL_ORACLE=PASS',
 'WIN81_X86=PASS') | Set-Content artifact\PTAR_R7_VALIDATION.txt -Encoding ascii
Write-Host "R7_CSO_SHA256=$r7CsoSha"
Write-Host "R7_RUNTIME_SHA256=$r7RuntimeSha"
Write-Host "R7_SHADER=$slots`_SLOTS_$texld`_TEXLD"
Write-Host "R7_VS_R6_SPATIAL_PCT=$r7VsR6"
Write-Host 'BIC16_R7_EDGECUBIC1_FULL_LAB=PASS'

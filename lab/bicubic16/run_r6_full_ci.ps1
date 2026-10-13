$ErrorActionPreference='Stop'

# R6 is layered on the fully-qualified R5 runtime. R5 itself re-qualifies R4,
# and R4 re-qualifies exact R3, so the entire spatial lineage is checked first.
. .\lab\bicubic16\run_r5_full_ci.ps1
if($LASTEXITCODE){throw 'R5 base qualification failed'}
$r5RuntimeSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$r5CsoSha=(Get-FileHash artifact\BIC16_R5_PROD.cso -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item artifact\d3d9.dll artifact\d3d9_r5_base_for_r6.dll -Force

# R6 may change only the Q720 spatial shader. Freeze all runtime/control/
# temporal sources after the exact R5 qualification.
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

# Install R6 only into the production Q720 shader slot.
Copy-Item lab\bicubic16\bic16_r6_phasedetail1.hlsl compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl -Force
$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_spec5_d3d9_ps_bytecode.h /Vn g_ptarSpec5Ps /Fo artifact\BIC16_R6_PROD.cso /Fc artifact\BIC16_R6_PROD_ASM.txt compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'R6 production shader compile failed'}
$asm=Get-Content artifact\BIC16_R6_PROD_ASM.txt -Raw
$texld=([regex]::Matches($asm,'(?im)^\s*texld')).Count
$m=[regex]::Match($asm,'(?i)approximately\s+(\d+)\s+instruction slots')
if(-not $m.Success){throw 'R6 slot count missing'}
$slots=[int]$m.Groups[1].Value
if($texld -ne 16){throw "R6 expected 16 texld, got $texld"}
if($slots -gt 300){throw "R6 unexpected ALU growth: $slots slots"}
$r6CsoSha=(Get-FileHash artifact\BIC16_R6_PROD.cso -Algorithm SHA256).Hash.ToLowerInvariant()

# Independent source compile must be byte-identical to the production payload.
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fo artifact\BIC16_R6_REF.cso lab\bicubic16\bic16_r6_phasedetail1.hlsl
if($LASTEXITCODE){throw 'R6 reference compile failed'}
$r6RefSha=(Get-FileHash artifact\BIC16_R6_REF.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($r6RefSha -ne $r6CsoSha){throw "R6 DXBC identity failed prod=$r6CsoSha ref=$r6RefSha"}

# Independent CPU implementation vs actual GPU shader output.
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_bicubic16_r6_oracle_bytecode.h /Vn g_ptarBicubic16R6Ps /Fo artifact\BIC16_R6_ORACLE.cso lab\bicubic16\bic16_r6_phasedetail1.hlsl
if($LASTEXITCODE){throw 'R6 oracle shader compile failed'}
if((Get-FileHash artifact\BIC16_R6_ORACLE.cso -Algorithm SHA256).Hash.ToLowerInvariant() -ne $r6CsoSha){throw 'R6 oracle DXBC differs from production'}
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\bicubic16\bicubic16_r6_oracle.cpp /link d3d9.lib user32.lib /OUT:artifact\bic16_r6_oracle.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R6 oracle compile failed'}
& artifact\bic16_r6_oracle.exe 2>&1 | Tee-Object artifact\BIC16_R6_ORACLE.txt
if($LASTEXITCODE){throw 'R6 GPU oracle failed'}
if((Get-Content artifact\BIC16_R6_ORACLE.txt -Raw) -notmatch 'BIC16_R6_PIXEL_PARITY=PASS'){throw 'R6 oracle PASS marker missing'}

# Isolated full-resolution GPU timing against the canonical SPEC5 shader.
.\lab\bicubic16\run_bicubic16_perf.ps1
if($LASTEXITCODE){throw 'R6 spatial perf gate failed'}

# Compile the actual Win8.1 x86 runtime with R4 POINT controls + R6 spatial.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_r6.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'R6 runtime compile failed'}
dumpbin /headers artifact\d3d9_r6.dll | Out-File artifact\R6_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_r6.dll | Out-File artifact\R6_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_r6.dll | Out-File artifact\R6_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\R6_HEADERS.txt -Raw;$ex=Get-Content artifact\R6_EXPORTS.txt -Raw;$dp=Get-Content artifact\R6_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'R6 Win8.1 x86 PE contract failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "R6 missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "R6 forbidden dependency $bad"}}
$r6RuntimeSha=(Get-FileHash artifact\d3d9_r6.dll -Algorithm SHA256).Hash.ToLowerInvariant()

# Enforce the spatial-only scope boundary.
foreach($rel in $immutable)
{
    $after=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant()
    if($after -ne $before[$rel]){throw "R6 modified forbidden source: $rel"}
}

# R4 POINT/PTAR A-B controls must still be physically present.
$hud=Get-Content compat\x86_d3d9\ptar_hud.h -Raw
$proxy=Get-Content compat\x86_d3d9\ptar_d3d9_proxy.cpp -Raw
foreach($tok in @('g_ptarHudFilterMode=2','g_ptarHudFilterMode=(g_ptarHudFilterMode==0)?2:0','g_ptarHudFilterMode=(g_ptarHudFilterMode+1)%3')){if($hud -notmatch [regex]::Escape($tok)){throw "R6 lost R4 HUD token $tok"}}
foreach($tok in @('POINT_RAW720_REF','D3DTEXF_POINT','POINT_RAW720_STRETCHRECT_FAIL')){if($proxy -notmatch [regex]::Escape($tok)){throw "R6 lost R4 proxy token $tok"}}

# Runtime integration checks on the actual R6 DLL.
New-Item -ItemType Directory -Force smoke_r6 | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_r6\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R6 ring harness compile failed'}
Copy-Item artifact\d3d9_r6.dll smoke_r6\d3d9.dll -Force
$root=(Resolve-Path smoke_r6).Path;$game=(Resolve-Path smoke_r6\game.exe).Path;$dll=(Resolve-Path smoke_r6\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\R6_RING.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\R6_F5.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R6 F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_R6'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\R6_F1.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R6 F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}
& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\R6_F7.txt
if($LASTEXITCODE){throw 'R6 F7 regression'}
if((Get-Content artifact\R6_F7.txt -Raw) -notmatch 'F7BENCH_ALL_GATES=PASS'){throw 'R6 F7 PASS marker missing'}

Copy-Item artifact\d3d9_r6.dll artifact\d3d9.dll -Force
Copy-Item lab\bicubic16\bic16_r6_phasedetail1.hlsl artifact\bic16_r6_phasedetail1.hlsl -Force
@(
 'PTAR_D3D9_BRANCH=BIC16_R6_PHASEDETAIL1_OVER_R5_OVER_R4_POINTAB',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('R5_BASE_RUNTIME_SHA256='+$r5RuntimeSha),
 ('R5_BASE_CSO_SHA256='+$r5CsoSha),
 ('R6_CSO_SHA256='+$r6CsoSha),
 ('R6_RUNTIME_SHA256='+$r6RuntimeSha),
 ('R6_TEXLD='+$texld),
 ('R6_SLOTS='+$slots),
 'Q720_SPATIAL=BIC16_R6_PHASEDETAIL1',
 'RESIDUAL_GAIN_LOW_CONTRAST=0.70',
 'RESIDUAL_GAIN_HIGH_CONTRAST=0.60',
 'SOURCE_DETAIL_GAIN_LOW_CONTRAST=1.00',
 'SOURCE_DETAIL_GAIN_HIGH_CONTRAST=0.60',
 'DIRECTIONAL_CROSS_EDGE_SUPPRESSION_MAX=0.45',
 'PHASE_DETAIL_GAIN_MAX=1.20',
 'CLAMP_MARGIN_LOW_CONTRAST=0.18',
 'CLAMP_MARGIN_HIGH_CONTRAST=0.08',
 'EXTRA_TEXTURE_FETCHES=0',
 'R4_POINTAB=UNCHANGED',
 'HF6=UNCHANGED',
 'TEMPORALPAIR60=UNCHANGED',
 'F7=PASS',
 'CTRL_F5_F1=PASS',
 'GPU_PIXEL_ORACLE=PASS',
 'WIN81_X86=PASS') | Set-Content artifact\PTAR_R6_VALIDATION.txt -Encoding ascii
Write-Host "R6_CSO_SHA256=$r6CsoSha"
Write-Host "R6_RUNTIME_SHA256=$r6RuntimeSha"
Write-Host "R6_SHADER=$slots`_SLOTS_$texld`_TEXLD"
Write-Host 'BIC16_R6_PHASEDETAIL1_FULL_LAB=PASS'

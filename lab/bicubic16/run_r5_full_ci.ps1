$ErrorActionPreference='Stop'

# R5 is deliberately layered on the already-qualified R4 runtime so the
# POINT/PTAR A-B controls, temporal path, FG, F7 and Win8.1 contract remain
# unchanged. R4 itself first re-qualifies exact R3.
. .\lab\pointab\run_r4_full_ci.ps1
if($LASTEXITCODE){throw 'R4 base qualification failed'}
$r4RuntimeSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$r3CsoSha=(Get-FileHash artifact\BIC16_R3_PROD.cso -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item artifact\d3d9.dll artifact\d3d9_r4_base_for_r5.dll -Force

# Freeze every production source except the Q720 spatial shader.
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

# Install only the experimental R5 spatial source into the production Q720
# slot and compile its actual ps_3_0 bytecode/header.
Copy-Item lab\bicubic16\bic16_r5_nativefit1.hlsl compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl -Force
$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_spec5_d3d9_ps_bytecode.h /Vn g_ptarSpec5Ps /Fo artifact\BIC16_R5_PROD.cso /Fc artifact\BIC16_R5_PROD_ASM.txt compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'R5 production shader compile failed'}
$asm=Get-Content artifact\BIC16_R5_PROD_ASM.txt -Raw
$texld=([regex]::Matches($asm,'(?im)^\s*texld')).Count
$m=[regex]::Match($asm,'(?i)approximately\s+(\d+)\s+instruction slots')
if(-not $m.Success){throw 'R5 slot count missing'}
$slots=[int]$m.Groups[1].Value
if($texld -ne 16){throw "R5 expected 16 texld, got $texld"}
if($slots -gt 220){throw "R5 unexpected ALU growth: $slots slots"}
$r5CsoSha=(Get-FileHash artifact\BIC16_R5_PROD.cso -Algorithm SHA256).Hash.ToLowerInvariant()

# Independent compile must be byte-identical.
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fo artifact\BIC16_R5_REF.cso lab\bicubic16\bic16_r5_nativefit1.hlsl
if($LASTEXITCODE){throw 'R5 reference compile failed'}
$r5RefSha=(Get-FileHash artifact\BIC16_R5_REF.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($r5RefSha -ne $r5CsoSha){throw "R5 DXBC identity failed prod=$r5CsoSha ref=$r5RefSha"}

# GPU pixel oracle against an independent CPU implementation of NATIVEFIT1.
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_bicubic16_r5_oracle_bytecode.h /Vn g_ptarBicubic16R5Ps /Fo artifact\BIC16_R5_ORACLE.cso lab\bicubic16\bic16_r5_nativefit1.hlsl
if($LASTEXITCODE){throw 'R5 oracle shader compile failed'}
if((Get-FileHash artifact\BIC16_R5_ORACLE.cso -Algorithm SHA256).Hash.ToLowerInvariant() -ne $r5CsoSha){throw 'R5 oracle DXBC differs from production'}
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\bicubic16\bicubic16_r5_oracle.cpp /link d3d9.lib user32.lib /OUT:artifact\bic16_r5_oracle.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R5 oracle compile failed'}
& artifact\bic16_r5_oracle.exe 2>&1 | Tee-Object artifact\BIC16_R5_ORACLE.txt
if($LASTEXITCODE){throw 'R5 GPU oracle failed'}
if((Get-Content artifact\BIC16_R5_ORACLE.txt -Raw) -notmatch 'BIC16_R5_PIXEL_PARITY=PASS'){throw 'R5 oracle PASS marker missing'}

# Keep the existing isolated GPU performance guard versus canonical SPEC5.
.\lab\bicubic16\run_bicubic16_perf.ps1
if($LASTEXITCODE){throw 'R5 spatial perf gate failed'}

# Compile the actual R4+R5 Win8.1 x86 runtime.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_r5.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'R5 runtime compile failed'}
dumpbin /headers artifact\d3d9_r5.dll | Out-File artifact\R5_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_r5.dll | Out-File artifact\R5_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_r5.dll | Out-File artifact\R5_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\R5_HEADERS.txt -Raw;$ex=Get-Content artifact\R5_EXPORTS.txt -Raw;$dp=Get-Content artifact\R5_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'R5 Win8.1 x86 PE contract failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "R5 missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "R5 forbidden dependency $bad"}}
$r5RuntimeSha=(Get-FileHash artifact\d3d9_r5.dll -Algorithm SHA256).Hash.ToLowerInvariant()

# No production source outside the Q720 shader is allowed to move relative to
# the already-qualified R4 state.
foreach($rel in $immutable){$after=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant();if($after -ne $before[$rel]){throw "R5 modified forbidden source: $rel"}}

# POINT A/B contract from R4 must still be present in the runtime sources.
$hud=Get-Content compat\x86_d3d9\ptar_hud.h -Raw
$proxy=Get-Content compat\x86_d3d9\ptar_d3d9_proxy.cpp -Raw
foreach($tok in @('g_ptarHudFilterMode=2','g_ptarHudFilterMode=(g_ptarHudFilterMode==0)?2:0','g_ptarHudFilterMode=(g_ptarHudFilterMode+1)%3')){if($hud -notmatch [regex]::Escape($tok)){throw "R5 lost R4 HUD token $tok"}}
foreach($tok in @('POINT_RAW720_REF','D3DTEXF_POINT','POINT_RAW720_STRETCHRECT_FAIL')){if($proxy -notmatch [regex]::Escape($tok)){throw "R5 lost R4 proxy token $tok"}}

# Targeted F5/F1 runtime integration and frozen F7 regression on the new DLL.
New-Item -ItemType Directory -Force smoke_r5 | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_r5\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R5 ring harness compile failed'}
Copy-Item artifact\d3d9_r5.dll smoke_r5\d3d9.dll -Force
$root=(Resolve-Path smoke_r5).Path;$game=(Resolve-Path smoke_r5\game.exe).Path;$dll=(Resolve-Path smoke_r5\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\R5_RING.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\R5_F5.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R5 F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_R5'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\R5_F1.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R5 F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}
& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\R5_F7.txt
if($LASTEXITCODE){throw 'R5 F7 regression'}
if((Get-Content artifact\R5_F7.txt -Raw) -notmatch 'F7BENCH_ALL_GATES=PASS'){throw 'R5 F7 PASS marker missing'}

Copy-Item artifact\d3d9_r5.dll artifact\d3d9.dll -Force
Copy-Item lab\bicubic16\bic16_r5_nativefit1.hlsl artifact\bic16_r5_nativefit1.hlsl -Force
@(
 'PTAR_D3D9_BRANCH=BIC16_R5_NATIVEFIT1_OVER_R4_POINTAB',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('R4_BASE_RUNTIME_SHA256='+$r4RuntimeSha),
 ('R3_BASE_CSO_SHA256='+$r3CsoSha),
 ('R5_CSO_SHA256='+$r5CsoSha),
 ('R5_RUNTIME_SHA256='+$r5RuntimeSha),
 ('R5_TEXLD='+$texld),
 ('R5_SLOTS='+$slots),
 'Q720_SPATIAL=BIC16_R5_NATIVEFIT1',
 'DETAIL_GAIN_LOW_CONTRAST=1.85',
 'DETAIL_GAIN_HIGH_CONTRAST=1.00',
 'ADAPTIVE_LUMA_RANGE=0.03_TO_0.18',
 'CLAMP_MARGIN_LOW_CONTRAST=0.18',
 'CLAMP_MARGIN_HIGH_CONTRAST=0.07',
 'EXTRA_TEXTURE_FETCHES=0',
 'R4_POINTAB=UNCHANGED',
 'HF6=UNCHANGED',
 'TEMPORALPAIR60=UNCHANGED',
 'F7=PASS',
 'CTRL_F5_F1=PASS',
 'GPU_PIXEL_ORACLE=PASS',
 'WIN81_X86=PASS') | Set-Content artifact\PTAR_R5_VALIDATION.txt -Encoding ascii
Write-Host "R5_CSO_SHA256=$r5CsoSha"
Write-Host "R5_RUNTIME_SHA256=$r5RuntimeSha"
Write-Host "R5_SHADER=$slots`_SLOTS_$texld`_TEXLD"
Write-Host 'BIC16_R5_NATIVEFIT1_FULL_LAB=PASS'

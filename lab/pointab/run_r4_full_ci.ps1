$ErrorActionPreference='Stop'

# 1) Rebuild and fully qualify the exact Bic16 R3 base first.
. .\lab\bicubic16\run_r3_full_ci.ps1
if($LASTEXITCODE){throw 'R3 base qualification failed'}
$r3Sha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item artifact\d3d9.dll artifact\d3d9_r3_base.dll -Force
$r3Cso=(Get-FileHash artifact\BIC16_R3_PROD.cso -Algorithm SHA256).Hash.ToLowerInvariant()

# Freeze sources that POINT A/B is forbidden to modify.
$immutable=@(
 'compat/x86_d3d9/ptar_spec5_d3d9_ps.hlsl',
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

# 2) Apply only the manual POINT A/B delta to HUD input state + spatial selector.
python .\lab\pointab\apply_pointab.py 2>&1 | Tee-Object artifact\POINTAB_PATCH.txt
if($LASTEXITCODE){throw 'POINT A/B patch failed'}
$patch=Get-Content artifact\POINTAB_PATCH.txt -Raw
if($patch -notmatch 'POINTAB_PATCH=PASS'){throw 'POINT A/B PASS marker missing'}

foreach($rel in $immutable){
    $after=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant()
    if($after -ne $before[$rel]){throw "POINT A/B modified forbidden source: $rel"}
}

$hud=Get-Content compat\x86_d3d9\ptar_hud.h -Raw
$proxy=Get-Content compat\x86_d3d9\ptar_d3d9_proxy.cpp -Raw
foreach($tok in @(
 'g_ptarHudFilterMode=2',
 'PtHudPointRef(){return g_ptarHudFilterMode==0;}',
 'g_ptarHudFilterMode=(g_ptarHudFilterMode==0)?2:0',
 'g_ptarHudFilterMode=(g_ptarHudFilterMode+1)%3',
 'return 0; // POINT raw source reference')){
    if($hud -notmatch [regex]::Escape($tok)){throw "POINT A/B HUD token missing: $tok"}
}
foreach($tok in @('POINT_RAW720_REF','D3DTEXF_POINT','POINT_RAW720_STRETCHRECT_FAIL')){
    if($proxy -notmatch [regex]::Escape($tok)){throw "POINT A/B proxy token missing: $tok"}
}
if($proxy.IndexOf('POINT_RAW720_REF') -gt $proxy.IndexOf('static HRESULT RenderFGMotionAndIntermediate')){throw 'POINT A/B escaped spatial scope'}
@(
 'POINTAB_STATIC_SCOPE=PASS',
 'F6_DIRECT_TOGGLE=PTAR_POINT',
 'F12_CYCLE=PTAR_POINT_BILINEAR',
 'DEFAULT_MODE=PTAR',
 'POINT_FILTER=D3DTEXF_POINT',
 'POINT_FAILOPEN=BILINEAR',
 'R3_SPATIAL_SHADER=UNCHANGED',
 'HF6_TEMPORAL_F7=UNCHANGED_BY_SOURCE_HASH') | Set-Content artifact\POINTAB_STATIC.txt -Encoding ascii

# 3) GPU oracle: D3D9 POINT stretch must preserve exact source colours and differ from LINEAR.
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\pointab\point_stretch_oracle.cpp /link d3d9.lib user32.lib /OUT:artifact\point_stretch_oracle.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'POINT StretchRect oracle compile failed'}
& artifact\point_stretch_oracle.exe 2>&1 | Tee-Object artifact\POINT_STRETCH_ORACLE.txt
if($LASTEXITCODE){throw 'POINT StretchRect oracle failed'}
if((Get-Content artifact\POINT_STRETCH_ORACLE.txt -Raw) -notmatch 'POINT_STRETCH_ORACLE=PASS'){throw 'POINT oracle PASS marker missing'}

# 4) Compile the actual Win8.1 x86 runtime with the exact R3 spatial bytecode unchanged.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_r4.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'R4 runtime compile failed'}
dumpbin /headers artifact\d3d9_r4.dll | Out-File artifact\R4_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_r4.dll | Out-File artifact\R4_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_r4.dll | Out-File artifact\R4_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\R4_HEADERS.txt -Raw;$ex=Get-Content artifact\R4_EXPORTS.txt -Raw;$dp=Get-Content artifact\R4_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'R4 Win8.1 x86 PE contract failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "R4 missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "R4 forbidden dependency $bad"}}
$r4Sha=(Get-FileHash artifact\d3d9_r4.dll -Algorithm SHA256).Hash.ToLowerInvariant()

# Exact R3 spatial bytecode must still be the selected Q720 payload.
$r3HeaderCso=(Get-FileHash artifact\BIC16_R3_PROD.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($r3HeaderCso -ne $r3Cso){throw 'R3 CSO changed during POINT A/B work'}

# 5) Targeted runtime integration regressions on the new DLL.
New-Item -ItemType Directory -Force smoke_r4 | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_r4\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R4 ring harness compile failed'}
Copy-Item artifact\d3d9_r4.dll smoke_r4\d3d9.dll -Force
$root=(Resolve-Path smoke_r4).Path;$game=(Resolve-Path smoke_r4\game.exe).Path;$dll=(Resolve-Path smoke_r4\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\R4_RING_HARNESS.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\R4_CTRL_F5.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R4 F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_R4'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\R4_CTRL_F1.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R4 F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}

& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\R4_F7.txt
if($LASTEXITCODE){throw 'R4 F7 regression'}
if((Get-Content artifact\R4_F7.txt -Raw) -notmatch 'F7BENCH_ALL_GATES=PASS'){throw 'R4 F7 PASS marker missing'}

Copy-Item artifact\d3d9_r4.dll artifact\d3d9.dll -Force
$r4Sha | Set-Content artifact\D3D9_R4_SHA256.txt -Encoding ascii
@(
 'PTAR_D3D9_BRANCH=BIC16_R4_POINTAB',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('R3_BASE_RUNTIME_SHA256='+$r3Sha),
 ('R3_BIC16_CSO_SHA256='+$r3Cso),
 ('R4_RUNTIME_SHA256='+$r4Sha),
 'R3_IMAGE_QUALITY_ALGORITHM=UNCHANGED',
 'POINTAB_F6=PTAR_POINT_DIRECT',
 'POINTAB_F12=PTAR_POINT_BILINEAR_CYCLE',
 'POINTAB_POINT_PATH=STRETCHRECT_D3DTEXF_POINT',
 'POINTAB_OUTPUT=UNCHANGED_1080P_FIT',
 'POINTAB_FG_STATE=UNCHANGED_WHILE_TOGGLING',
 'POINTAB_HUD_FILTER_ID=0_POINT',
 'POINTAB_GPU_ORACLE=PASS',
 'CTRL_F5_F1=PASS',
 'F7=PASS',
 'WIN81_X86=PASS') | Set-Content artifact\PTAR_R4_VALIDATION.txt -Encoding ascii
Write-Host "R3_BASE_RUNTIME_SHA256=$r3Sha"
Write-Host "R3_BIC16_CSO_SHA256=$r3Cso"
Write-Host "R4_RUNTIME_SHA256=$r4Sha"
Write-Host 'BIC16_R4_POINTAB_FULL_LAB=PASS'

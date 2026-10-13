$ErrorActionPreference='Stop'

# Re-qualify the complete spatial lineage first. R8 starts from exact R7 and
# adds a fail-soft temporal SR overlay; the qualified FG path stays frozen.
. .\lab\bicubic16\run_r7_full_ci.ps1
if($LASTEXITCODE){throw 'R7 base qualification failed'}
$r7RuntimeSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$r7ShaderSha=(Get-FileHash artifact\BIC16_R7_PROD.cso -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item artifact\d3d9.dll artifact\d3d9_r7_base_for_r8.dll -Force

$immutable=@(
 'compat/x86_d3d9/ptar_spec5_d3d9_ps.hlsl',
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
$r7ProxySha=(Get-FileHash compat\x86_d3d9\ptar_d3d9_proxy.cpp -Algorithm SHA256).Hash.ToLowerInvariant()

$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
New-Item -ItemType Directory -Force artifact,build_x86 | Out-Null

& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_tsr1_motion_subpixel_ps_bytecode.h /Vn g_ptarTsr1MotionSubpixelPs /Fo artifact\TSR1_MOTION_SUBPIXEL.cso /Fc artifact\TSR1_MOTION_SUBPIXEL_ASM.txt lab\tsr1\ptar_tsr1_motion_subpixel_ps.hlsl
if($LASTEXITCODE){throw 'TSR1 subpixel motion compile failed'}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fo artifact\TSR1_MOTION_SUBPIXEL_REF.cso lab\tsr1\ptar_tsr1_motion_subpixel_ps.hlsl
if($LASTEXITCODE){throw 'TSR1 subpixel reference compile failed'}
if((Get-FileHash artifact\TSR1_MOTION_SUBPIXEL.cso -Algorithm SHA256).Hash -ne (Get-FileHash artifact\TSR1_MOTION_SUBPIXEL_REF.cso -Algorithm SHA256).Hash){throw 'TSR1 subpixel compile identity failed'}

& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_tsr1_resolve_ps_bytecode.h /Vn g_ptarTsr1ResolvePs /Fo artifact\TSR1_RESOLVE.cso /Fc artifact\TSR1_RESOLVE_ASM.txt lab\tsr1\ptar_tsr1_resolve_ps.hlsl
if($LASTEXITCODE){throw 'TSR1 resolve compile failed'}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fo artifact\TSR1_RESOLVE_REF.cso lab\tsr1\ptar_tsr1_resolve_ps.hlsl
if($LASTEXITCODE){throw 'TSR1 resolve reference compile failed'}
if((Get-FileHash artifact\TSR1_RESOLVE.cso -Algorithm SHA256).Hash -ne (Get-FileHash artifact\TSR1_RESOLVE_REF.cso -Algorithm SHA256).Hash){throw 'TSR1 resolve compile identity failed'}

$subAsm=Get-Content artifact\TSR1_MOTION_SUBPIXEL_ASM.txt -Raw
$resAsm=Get-Content artifact\TSR1_RESOLVE_ASM.txt -Raw
$subTex=([regex]::Matches($subAsm,'(?im)^\s*texld')).Count
$resTex=([regex]::Matches($resAsm,'(?im)^\s*texld')).Count
$subSlots=[int]([regex]::Match($subAsm,'(?i)approximately\s+(\d+)\s+instruction slots').Groups[1].Value)
$resSlots=[int]([regex]::Match($resAsm,'(?i)approximately\s+(\d+)\s+instruction slots').Groups[1].Value)
if($subTex -lt 7 -or $subTex -gt 12){throw "unexpected TSR1 subpixel texld count $subTex"}
# Optimized resolve: four current + four history + one motion + one exact-R7 read.
if($resTex -lt 9 -or $resTex -gt 12){throw "unexpected optimized TSR1 resolve texld count $resTex"}
if($resSlots -gt 240){throw "optimized TSR1 resolve ps_3_0 growth too large: $resSlots"}

.\lab\tsr1\apply_r8_proxy_patch.ps1
if($LASTEXITCODE){throw 'R8 proxy patch failed'}
$r8ProxySha=(Get-FileHash compat\x86_d3d9\ptar_d3d9_proxy.cpp -Algorithm SHA256).Hash.ToLowerInvariant()
if($r8ProxySha -eq $r7ProxySha){throw 'R8 proxy patch did not change proxy'}
foreach($rel in $immutable){$after=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant();if($after -ne $before[$rel]){throw "R8 modified forbidden source: $rel"}}

$proxy=Get-Content compat\x86_d3d9\ptar_d3d9_proxy.cpp -Raw
foreach($tok in @(
 'PtTsr1Release()',
 'Initialize_CreateTSR1',
 'PtTsr1CanResolve()',
 'PtTsr1Resolve(',
 'g_ptar.currentRealTexture,g_ptar.generatedSurface',
 'PRESENT_REAL_TSR1',
 'PtTsr1StoreSource(',
 'const bool fgSession=',
 'RenderFGMotionAndIntermediate(self)'))
{
    if($proxy -notmatch [regex]::Escape($tok)){throw "R8 proxy integration token missing: $tok"}
}

& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_r8.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'R8 runtime compile failed'}
dumpbin /headers artifact\d3d9_r8.dll | Out-File artifact\R8_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_r8.dll | Out-File artifact\R8_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_r8.dll | Out-File artifact\R8_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\R8_HEADERS.txt -Raw;$ex=Get-Content artifact\R8_EXPORTS.txt -Raw;$dp=Get-Content artifact\R8_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'R8 Win8.1 x86 PE contract failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "R8 missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "R8 forbidden dependency $bad"}}
$r8RuntimeSha=(Get-FileHash artifact\d3d9_r8.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$subSha=(Get-FileHash artifact\TSR1_MOTION_SUBPIXEL.cso -Algorithm SHA256).Hash.ToLowerInvariant()
$resSha=(Get-FileHash artifact\TSR1_RESOLVE.cso -Algorithm SHA256).Hash.ToLowerInvariant()

& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\d3d9_fg_gpu_pipeline_smoke.cpp /link d3d9.lib user32.lib /OUT:artifact\r8_fg_gpu_smoke.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R8 FG smoke compile failed'}
& artifact\r8_fg_gpu_smoke.exe 2>&1 | Tee-Object artifact\R8_FG_GPU_SMOKE.txt
if($LASTEXITCODE){throw 'R8 FG GPU smoke failed'}
if((Get-Content artifact\R8_FG_GPU_SMOKE.txt -Raw) -notmatch 'D3D9_FG_GPU_PIPELINE_SMOKE=PASS'){throw 'R8 FG smoke marker missing'}

New-Item -ItemType Directory -Force smoke_r8 | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_r8\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R8 ring harness compile failed'}
Copy-Item artifact\d3d9_r8.dll smoke_r8\d3d9.dll -Force
$root=(Resolve-Path smoke_r8).Path;$game=(Resolve-Path smoke_r8\game.exe).Path;$dll=(Resolve-Path smoke_r8\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\R8_RING.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\R8_F5.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R8 F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_R8'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\R8_F1.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'R8 F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}
& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\R8_F7.txt
if($LASTEXITCODE){throw 'R8 F7 regression'}
if((Get-Content artifact\R8_F7.txt -Raw) -notmatch 'F7BENCH_ALL_GATES=PASS'){throw 'R8 F7 PASS marker missing'}

Copy-Item artifact\d3d9_r8.dll artifact\d3d9.dll -Force
Copy-Item lab\tsr1\ptar_tsr1_motion_subpixel_ps.hlsl artifact\ptar_tsr1_motion_subpixel_ps.hlsl -Force
Copy-Item lab\tsr1\ptar_tsr1_resolve_ps.hlsl artifact\ptar_tsr1_resolve_ps.hlsl -Force
Copy-Item compat\x86_d3d9\ptar_tsr1_runtime.h artifact\ptar_tsr1_runtime.h -Force
Copy-Item compat\x86_d3d9\ptar_tsr1_runtime.inl artifact\ptar_tsr1_runtime.inl -Force
@(
 'PTAR_D3D9_BRANCH=R8_TSR1_OVER_R7_EDGECUBIC1',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('R7_BASE_RUNTIME_SHA256='+$r7RuntimeSha),
 ('R7_BASE_SHADER_SHA256='+$r7ShaderSha),
 ('R8_RUNTIME_SHA256='+$r8RuntimeSha),
 ('TSR1_SUBPIXEL_CSO_SHA256='+$subSha),
 ('TSR1_RESOLVE_CSO_SHA256='+$resSha),
 ('TSR1_SUBPIXEL_TEXLD='+$subTex),
 ('TSR1_SUBPIXEL_SLOTS='+$subSlots),
 ('TSR1_RESOLVE_TEXLD='+$resTex),
 ('TSR1_RESOLVE_SLOTS='+$resSlots),
 'Q720_SPATIAL_FALLBACK=R7_EDGECUBIC1_EXACT',
 'TSR1_RESOLVE_REUSES_R7_CURRENT=YES',
 'TSR1_SOURCE_HISTORY_FRAMES=1',
 'TSR1_MOTION_DOMAIN=SOURCE_PIXELS',
 'TSR1_SUBPIXEL_QUANTIZATION_APPROX=0.251_SOURCE_PIXEL',
 'TSR1_UNION_KERNEL_SIGMA=0.25_SOURCE_PIXEL',
 'TSR1_PHASE_DIVERSITY_GATED=YES',
 'TSR1_FG_OFF_ONLY=YES',
 'FG_PATH=UNCHANGED',
 'HF6=UNCHANGED',
 'TEMPORALPAIR60=UNCHANGED',
 'F7=PASS',
 'CTRL_F5_F1=PASS',
 'FG_GPU_SMOKE=PASS',
 'WIN81_X86=PASS') | Set-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
Write-Host "R8_RUNTIME_SHA256=$r8RuntimeSha"
Write-Host "TSR1_SUBPIXEL=$subSlots`_SLOTS_$subTex`_TEXLD"
Write-Host "TSR1_RESOLVE=$resSlots`_SLOTS_$resTex`_TEXLD"
Write-Host 'R8_TSR1_FULL_LAB=PASS'

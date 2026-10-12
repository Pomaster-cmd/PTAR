$ErrorActionPreference='Stop'

# 1) Reconstruct and fully qualify the exact field baseline first.
.\lab\f7bench1\run_f7bench1_ci.ps1
if($LASTEXITCODE){throw 'F7BENCH1 baseline qualification failed'}
$expectedBaseline='be14145ee25227569b81b9e5361b3b135bd1a52b85218018aa96a41a236e605f'
$baselineSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
if($baselineSha -ne $expectedBaseline){throw "unexpected F7BENCH1 baseline sha $baselineSha"}
Copy-Item artifact\d3d9.dll artifact\d3d9_f7bench1_field_baseline.dll -Force
Copy-Item compat\x86_d3d9\ptar_d3d9_proxy.cpp artifact\ptar_d3d9_proxy_before_sf5parity1.cpp -Force

# Immutable subsystem/source identities before the spatial-only patch.
$before=@{}
foreach($rel in @(
 'compat/x86_d3d9/ptar_fg_pacer.h',
 'compat/x86_d3d9/ptar_fg_me_coarse_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_me_refine_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_interpolate_ps.hlsl',
 'compat/x86_d3d9/ptar_f7_benchmark.h',
 'compat/x86_d3d9/ptar_hud.h',
 'compat/x86_d3d9/ptar_gw16i_hud_d3d9.h',
 'compat/x86_d3d9/ptar_moe_d3d9_ps.hlsl',
 'compat/x86_d3d9/ptar_moe_universal_d3d9_ps.hlsl')){
  $before[$rel]=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant()
}

# 2) Apply ONLY the SF5 colour-state isolation.
python .\lab\sf5_parity1\apply_sf5_parity1.py 2>&1 | Tee-Object artifact\SF5PARITY1_APPLY.txt
if($LASTEXITCODE){throw 'SF5PARITY1 apply failed'}
$ap=Get-Content artifact\SF5PARITY1_APPLY.txt -Raw
foreach($m in @(
 'SF5PARITY1_PATCH=PASS',
 'SF5PARITY1_SHADER_CHANGE=NONE',
 'SF5PARITY1_SCOPE=SPATIAL_SRGB_STATE_ISOLATION_ONLY',
 'SF5PARITY1_FAILURE_POLICY=LEGACY_FAIL_OPEN_WITHOUT_STATE_MUTATION',
 'SF5PARITY1_RESTORE_BEFORE_HF6=PASS')){
  if($ap -notmatch [regex]::Escape($m)){throw "missing patch gate $m"}
}

# 3) Byte identity: FG, pacer, F7, HUD and BOTH MoE shader sources are unchanged.
foreach($rel in $before.Keys){
  $after=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant()
  if($after -ne $before[$rel]){throw "unexpected non-spatial-state source change: $rel"}
}
@(
 'SF5PARITY1_HF6_PACER_BYTE_IDENTICAL=PASS',
 'SF5PARITY1_HF6_SHADERS_BYTE_IDENTICAL=PASS',
 'SF5PARITY1_F7_SOURCE_BYTE_IDENTICAL=PASS',
 'SF5PARITY1_HUD_SOURCE_BYTE_IDENTICAL=PASS',
 'SF5PARITY1_MOE_SHADER_SOURCE_BYTE_IDENTICAL=PASS') | Set-Content artifact\SF5PARITY1_IDENTITY.txt -Encoding ascii

# Proxy-level identity for the HF6 implementation block itself.
python -c "from pathlib import Path; a=Path('artifact/ptar_d3d9_proxy_before_sf5parity1.cpp').read_text(); b=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp').read_text(); cut=lambda s:s[s.index('static HRESULT RenderFGMotionAndIntermediate'):s.index('static unsigned long g_ptarHudFrameSequence')]; assert cut(a)==cut(b); print('SF5PARITY1_HF6_RUNTIME_BLOCK_IDENTICAL=PASS')" | Tee-Object -Append artifact\SF5PARITY1_IDENTITY.txt
if($LASTEXITCODE){throw 'HF6 runtime block changed'}

# The temporal presenter code must be byte-identical too.
python -c "from pathlib import Path; a=Path('artifact/ptar_d3d9_proxy_before_sf5parity1.cpp').read_text(); b=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp').read_text(); st='static DWORD WINAPI PtTemporalPresenterThread'; en='static bool PtStartTemporalPresenter'; f=lambda s:s[s.index(st):s.index(en,s.index(st))]; assert f(a)==f(b); print('SF5PARITY1_TEMPORAL_PRESENTER_BLOCK_IDENTICAL=PASS')" | Tee-Object -Append artifact\SF5PARITY1_IDENTITY.txt
if($LASTEXITCODE){throw 'TEMPORALPAIR60 presenter changed'}

# 4) Recompile the actual runtime. No shader is recompiled/replaced by this patch:
# build_x86 contains the exact already-qualified F7/TEMPORALPAIR60 bytecode.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_sf5parity1.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'SF5PARITY1 runtime compile failed'}

dumpbin /headers artifact\d3d9_sf5parity1.dll | Out-File artifact\SF5PARITY1_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_sf5parity1.dll | Out-File artifact\SF5PARITY1_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_sf5parity1.dll | Out-File artifact\SF5PARITY1_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\SF5PARITY1_HEADERS.txt -Raw;$ex=Get-Content artifact\SF5PARITY1_EXPORTS.txt -Raw;$dp=Get-Content artifact\SF5PARITY1_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'Win8.1 x86 PE gate failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "forbidden dependency $bad"}}
$candidateSha=(Get-FileHash artifact\d3d9_sf5parity1.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$candidateSha | Set-Content artifact\D3D9_SF5PARITY1_SHA256.txt -Encoding ascii

# 5) Re-run CTRL+F5 and CTRL+F1 ring integration AGAINST the new binary.
New-Item -ItemType Directory -Force smoke_sf5 | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_sf5\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'ring harness compile failed'}
Copy-Item artifact\d3d9_sf5parity1.dll smoke_sf5\d3d9.dll -Force
$root=(Resolve-Path smoke_sf5).Path;$game=(Resolve-Path smoke_sf5\game.exe).Path;$dll=(Resolve-Path smoke_sf5\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\SF5PARITY1_RING_HARNESS.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\SF5PARITY1_CTRL_F5_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'candidate F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_SF5PARITY1'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\SF5PARITY1_CTRL_F1_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'candidate F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}

# 6) Re-run F7 pure benchmark state machine after the patch (source is byte-identical).
& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\SF5PARITY1_F7_HARNESS.txt
if($LASTEXITCODE){throw 'candidate F7 benchmark regression'}
if((Get-Content artifact\SF5PARITY1_F7_HARNESS.txt -Raw) -notmatch 'F7BENCH_ALL_GATES=PASS'){throw 'F7 PASS marker missing'}

# 7) Re-run D3D11-semantic SF5 oracle and sRGB pipeline model.
$fxc=Get-Command fxc.exe -ErrorAction SilentlyContinue
if(-not $fxc){throw 'fxc.exe missing after baseline CI'}
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_old_ps_bytecode.h /Vn g_ptarOldPs compat\x86_d3d9\ptar_moe_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'oracle shader compile failed'}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_sf5_parity_ps_bytecode.h /Vn g_ptarSf5ParityPs compat\x86_d3d9\ptar_moe_sf5_parity_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'oracle parity shader compile failed'}
& cl.exe /nologo /O2 /MT /W4 /WX /wd4505 /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\sf5_parity1\sf5_parity_gpu_harness.cpp /link d3d9.lib user32.lib /OUT:artifact\sf5_parity_gpu_harness.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'oracle harness compile failed'}
& artifact\sf5_parity_gpu_harness.exe 2>&1 | Tee-Object artifact\SF5PARITY1_GPU_ORACLE.txt
if($LASTEXITCODE){throw 'SF5 semantic oracle failed'}
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\sf5_parity1\sf5_srgb_state_harness.cpp /link d3d9.lib user32.lib /OUT:artifact\sf5_srgb_state_harness.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'sRGB model compile failed'}
& artifact\sf5_srgb_state_harness.exe 2>&1 | Tee-Object artifact\SF5PARITY1_SRGB_MODEL.txt
if($LASTEXITCODE){throw 'sRGB model failed'}
& cl.exe /nologo /O2 /MT /W4 /WX /wd4505 /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\sf5_parity1\sf5_two_pass_srgb_harness.cpp /link d3d9.lib user32.lib /OUT:artifact\sf5_two_pass_srgb_harness.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'two-pass model compile failed'}
& artifact\sf5_two_pass_srgb_harness.exe 2>&1 | Tee-Object artifact\SF5PARITY1_TWO_PASS_MODEL.txt
if($LASTEXITCODE){throw 'two-pass model failed'}

# 8) Final static contract: only the spatial pass calls the UNORM scoped wrapper.
$p=Get-Content compat\x86_d3d9\ptar_d3d9_proxy.cpp -Raw
foreach($m in @(
 'DrawSf5UnormPassRect',
 'D3DSAMP_SRGBTEXTURE,FALSE',
 'D3DRS_SRGBWRITEENABLE,FALSE',
 'PtSf5RestoreColorState(dev,state)',
 'restore the exact game state BEFORE HF6')){
  if($p -notmatch [regex]::Escape($m)){throw "missing SF5 parity source marker $m"}
}
if(([regex]::Matches($p,'DrawSf5UnormPassRect\(')).Count -ne 2){throw 'UNORM wrapper must have exactly definition + one spatial call'}

Copy-Item artifact\d3d9_sf5parity1.dll artifact\d3d9.dll -Force
Copy-Item compat\x86_d3d9\ptar_d3d9_proxy.cpp artifact\ptar_d3d9_proxy_sf5parity1.cpp -Force
@(
 'PTAR_D3D9_BRANCH=HF6_TEMPORALPAIR60_F7BENCH1_SF5PARITY1',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('BASE_F7BENCH1_SHA256='+$baselineSha),
 ('D3D9_SF5PARITY1_SHA256='+$candidateSha),
 'SPATIAL_ALGORITHM=PTAR_NG_MOE_V01_SF5_UNCHANGED',
 'SPATIAL_SHADER_BYTECODE=UNCHANGED_FROM_FIELD_BASE',
 'SPATIAL_PARITY=D3D11_UNORM_STATE_ISOLATION',
 'SPATIAL_SRGB_SCOPE=SPATIAL_PASS_ONLY_SAVE_FORCE_OFF_RESTORE',
 'FG_ALGORITHM=EXACT_HF6_UNCHANGED',
 'TEMPORAL_SCHEDULER=UNCHANGED_TEMPORALPAIR60',
 'F7_BENCHMARK=UNCHANGED_F7BENCH1',
 'CTRL_F5_F1=REGRESSION_PASS') | Set-Content artifact\PTAR_D3D9_HF6_TEMPORALPAIR60_F7BENCH1_SF5PARITY1_VERSION.txt -Encoding ascii

Write-Host "SF5PARITY1_RUNTIME_SHA256=$candidateSha"
Write-Host 'HF6_TEMPORALPAIR60_F7BENCH1_SF5PARITY1_FULL_LAB=PASS'

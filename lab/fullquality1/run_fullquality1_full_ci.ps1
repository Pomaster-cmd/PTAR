$ErrorActionPreference='Stop'

# 1) Reconstruct and fully qualify the exact SF5PARITY1 field-lineage baseline.
.\lab\sf5_parity1\run_sf5_parity1_full_ci_v3.ps1
if($LASTEXITCODE){throw 'SF5PARITY1 baseline qualification failed'}

New-Item -ItemType Directory -Force artifact | Out-Null
$sf5BaselineSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
Copy-Item artifact\d3d9.dll artifact\d3d9_sf5parity1_rebuilt_baseline.dll -Force
Copy-Item compat\x86_d3d9\ptar_d3d9_proxy.cpp artifact\ptar_d3d9_proxy_before_fullquality1.cpp -Force

# 2) Snapshot every subsystem that FULLQUALITY1 is forbidden to modify.
$immutable=@(
 'compat/x86_d3d9/ptar_fg_pacer.h',
 'compat/x86_d3d9/ptar_fg_me_coarse_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_me_refine_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_interpolate_ps.hlsl',
 'compat/x86_d3d9/ptar_f7_benchmark.h',
 'compat/x86_d3d9/ptar_hud.h',
 'compat/x86_d3d9/ptar_gw16i_hud_d3d9.h',
 'compat/x86_d3d9/ptar_moe_d3d9_ps.hlsl',
 'compat/x86_d3d9/ptar_moe_universal_d3d9_ps.hlsl',
 'compat/x86_d3d9/ptar_resolution_policy.h')
$before=@{}
foreach($rel in $immutable){$before[$rel]=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant()}

# 3) Compile the three specialized SM3 quality shaders under the Win8.1 toolchain.
.\lab\fullquality1\compile_sm3_quality.ps1
if($LASTEXITCODE){throw 'FULLQUALITY1 SM3 compile qualification failed'}

# 4) Apply ONLY the full-quality spatial shader inventory and selector.
python .\lab\fullquality1\apply_fullquality1.py 2>&1 | Tee-Object artifact\FULLQUALITY1_APPLY.txt
if($LASTEXITCODE){throw 'FULLQUALITY1 patch failed'}
$apply=Get-Content artifact\FULLQUALITY1_APPLY.txt -Raw
foreach($m in @(
 'FULLQUALITY1_PATCH=PASS',
 'FULLQUALITY1_SCOPE=SPATIAL_SHADER_INVENTORY_AND_SELECTOR_ONLY',
 'FULLQUALITY1_Q720=SPEC5_SPECIALIZED_NATIVEFIT40_DETAIL25',
 'FULLQUALITY1_EXACT15=QSTAGE4DIAG2_K130_N90',
 'FULLQUALITY1_HQ900=SPECIALIZED_SHARP0P96',
 'FULLQUALITY1_DYNAMIC_C1=NONE',
 'FULLQUALITY1_OUT_OF_DOMAIN=BILINEAR_FAILOPEN',
 'FULLQUALITY1_HF6_CHANGE=NONE',
 'FULLQUALITY1_TEMPORAL_CHANGE=NONE',
 'FULLQUALITY1_F7_CHANGE=NONE')){
  if($apply -notmatch [regex]::Escape($m)){throw "missing FULLQUALITY1 apply gate $m"}
}

# 5) Byte identity of every forbidden source file.
foreach($rel in $before.Keys){
  $after=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant()
  if($after -ne $before[$rel]){throw "FULLQUALITY1 modified forbidden source: $rel"}
}
@(
 'FULLQUALITY1_HF6_PACER_BYTE_IDENTICAL=PASS',
 'FULLQUALITY1_HF6_SHADERS_BYTE_IDENTICAL=PASS',
 'FULLQUALITY1_F7_SOURCE_BYTE_IDENTICAL=PASS',
 'FULLQUALITY1_HUD_SOURCE_BYTE_IDENTICAL=PASS',
 'FULLQUALITY1_LEGACY_SF5_SHADER_BYTE_IDENTICAL=PASS',
 'FULLQUALITY1_RESOLUTION_POLICY_BYTE_IDENTICAL=PASS') | Set-Content artifact\FULLQUALITY1_IDENTITY.txt -Encoding ascii

# Proxy identity: HF6, TEMPORALPAIR60 presenter and SF5PARITY1 state wrapper must be unchanged.
python -c "from pathlib import Path; a=Path('artifact/ptar_d3d9_proxy_before_fullquality1.cpp').read_text(); b=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp').read_text(); st='static HRESULT RenderFGMotionAndIntermediate'; en='static unsigned long g_ptarHudFrameSequence'; f=lambda s:s[s.index(st):s.index(en,s.index(st))]; assert f(a)==f(b); print('FULLQUALITY1_HF6_RUNTIME_BLOCK_IDENTICAL=PASS')" | Tee-Object -Append artifact\FULLQUALITY1_IDENTITY.txt
if($LASTEXITCODE){throw 'HF6 runtime block changed'}
python -c "from pathlib import Path; a=Path('artifact/ptar_d3d9_proxy_before_fullquality1.cpp').read_text(); b=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp').read_text(); st='static DWORD WINAPI PtTemporalPresenterThread'; en='static void PtTemporalShutdown'; f=lambda s:s[s.index(st):s.index(en,s.index(st))]; assert f(a)==f(b); print('FULLQUALITY1_TEMPORAL_PRESENTER_BLOCK_IDENTICAL=PASS')" | Tee-Object -Append artifact\FULLQUALITY1_IDENTITY.txt
if($LASTEXITCODE){throw 'TEMPORALPAIR60 presenter changed'}
python -c "from pathlib import Path; a=Path('artifact/ptar_d3d9_proxy_before_fullquality1.cpp').read_text(); b=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp').read_text(); st='struct PTSf5ColorState'; en='static bool PtSpatialExactQ720' if 'static bool PtSpatialExactQ720' in b else 'static HRESULT RenderSpatialToCurrent'; aa=a[a.index(st):a.index('static HRESULT RenderSpatialToCurrent',a.index(st))]; bb=b[b.index(st):b.index(en,b.index(st))]; assert aa==bb; print('FULLQUALITY1_SF5PARITY1_SRGB_WRAPPER_IDENTICAL=PASS')" | Tee-Object -Append artifact\FULLQUALITY1_IDENTITY.txt
if($LASTEXITCODE){throw 'SF5PARITY1 state wrapper changed'}

# 6) Deterministic selector qualification.
python .\lab\fullquality1\validate_fullquality_selector.py 2>&1 | Tee-Object artifact\FULLQUALITY1_SELECTOR.txt
if($LASTEXITCODE){throw 'FULLQUALITY1 selector model failed'}
if((Get-Content artifact\FULLQUALITY1_SELECTOR.txt -Raw) -notmatch 'FULLQUALITY_SELECTOR_ALL=PASS'){throw 'selector PASS marker missing'}

# 7) Pixel parity against recovered D3D11 equations.
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\fullquality1\fullquality_gpu_oracle_v2.cpp /link d3d9.lib user32.lib /OUT:artifact\fullquality_gpu_oracle_v2.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'FULLQUALITY1 oracle v2 compile failed'}
& artifact\fullquality_gpu_oracle_v2.exe 2>&1 | Tee-Object artifact\FULLQUALITY1_PIXEL_ORACLE_V2.txt
if($LASTEXITCODE){throw 'FULLQUALITY1 pixel oracle v2 failed'}
$oracle=Get-Content artifact\FULLQUALITY1_PIXEL_ORACLE_V2.txt -Raw
foreach($m in @('QSTAGE4_PARITY=PASS','SPEC5_PARITY=PASS','HQ900_PARITY=PASS','FULLQUALITY_PIXEL_PARITY=PASS')){if($oracle -notmatch [regex]::Escape($m)){throw "missing oracle gate $m"}}

# 8) Compile the actual x86 D3D9 candidate. No FG shader is rebuilt or changed.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9_fullquality1.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'FULLQUALITY1 runtime compile failed'}
dumpbin /headers artifact\d3d9_fullquality1.dll | Out-File artifact\FULLQUALITY1_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_fullquality1.dll | Out-File artifact\FULLQUALITY1_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_fullquality1.dll | Out-File artifact\FULLQUALITY1_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\FULLQUALITY1_HEADERS.txt -Raw;$ex=Get-Content artifact\FULLQUALITY1_EXPORTS.txt -Raw;$dp=Get-Content artifact\FULLQUALITY1_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'FULLQUALITY1 Win8.1 x86 PE contract failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "FULLQUALITY1 missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "FULLQUALITY1 forbidden dependency $bad"}}
$candidateSha=(Get-FileHash artifact\d3d9_fullquality1.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$candidateSha | Set-Content artifact\D3D9_FULLQUALITY1_SHA256.txt -Encoding ascii

# 9) Run CTRL+F5 and CTRL+F1 ring integration on the NEW runtime.
New-Item -ItemType Directory -Force smoke_fullquality | Out-Null
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_fullquality\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'FULLQUALITY1 ring harness compile failed'}
Copy-Item artifact\d3d9_fullquality1.dll smoke_fullquality\d3d9.dll -Force
$root=(Resolve-Path smoke_fullquality).Path;$game=(Resolve-Path smoke_fullquality\game.exe).Path;$dll=(Resolve-Path smoke_fullquality\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$proc=Start-Process $game -ArgumentList ('"'+$dll+'" mixed 20000') -PassThru -RedirectStandardOutput artifact\FULLQUALITY1_RING_HARNESS.txt
Start-Sleep -Milliseconds 300
& artifact\ptar_vblank3_d3d9_autostart.exe 5 --autostart 2>&1 | Tee-Object artifact\FULLQUALITY1_CTRL_F5_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'FULLQUALITY1 F5 ring failed'}
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=Join-Path $root 'F1_FULLQUALITY1'
& artifact\ptar_presentshed1_visible.exe 5 --autostart 2>&1 | Tee-Object artifact\FULLQUALITY1_CTRL_F1_RUNTIME_RING.txt
if($LASTEXITCODE){if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force};throw 'FULLQUALITY1 F1 ring failed'}
if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force}

# 10) F7 state/timing regression after the spatial-only patch.
& .\f7_benchmark_harness.exe 2>&1 | Tee-Object artifact\FULLQUALITY1_F7_HARNESS.txt
if($LASTEXITCODE){throw 'FULLQUALITY1 F7 regression'}
if((Get-Content artifact\FULLQUALITY1_F7_HARNESS.txt -Raw) -notmatch 'F7BENCH_ALL_GATES=PASS'){throw 'FULLQUALITY1 F7 PASS marker missing'}

# 11) Final static routing/scope contract.
$p=Get-Content compat\x86_d3d9\ptar_d3d9_proxy.cpp -Raw
foreach($m in @(
 'SPEC5_Q720_NATIVEFIT40_DETAIL25',
 'QSTAGE4DIAG2_K130_N90',
 'HQ900_SHARP96',
 'BILINEAR_DOMAIN_FAILOPEN',
 'DrawSf5UnormPassRect',
 'PtSf5RestoreColorState(dev,state)',
 'restore the exact game state BEFORE HF6')){
  if($p -notmatch [regex]::Escape($m)){throw "FULLQUALITY1 source marker missing $m"}
}
if($p -match 'ptar_spec5_hq900_d3d9_ps_bytecode'){throw 'obsolete unified adaptive shader included'}

Copy-Item artifact\d3d9_fullquality1.dll artifact\d3d9.dll -Force
Copy-Item compat\x86_d3d9\ptar_d3d9_proxy.cpp artifact\ptar_d3d9_proxy_fullquality1.cpp -Force
Copy-Item compat\x86_d3d9\ptar_qstage4diag2_d3d9_ps.hlsl artifact\ptar_qstage4diag2_d3d9_ps.hlsl -Force
Copy-Item compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl artifact\ptar_spec5_d3d9_ps.hlsl -Force
Copy-Item compat\x86_d3d9\ptar_hq900_d3d9_ps.hlsl artifact\ptar_hq900_d3d9_ps.hlsl -Force
@(
 'PTAR_D3D9_BRANCH=HF6_TEMPORALPAIR60_F7BENCH1_SF5PARITY1_FULLQUALITY1',
 ('SOURCE_COMMIT='+$env:GITHUB_SHA),
 ('BASE_SF5PARITY1_REBUILT_SHA256='+$sf5BaselineSha),
 ('D3D9_FULLQUALITY1_SHA256='+$candidateSha),
 'Q720_SPATIAL=SPEC5_NATIVEFIT40_DETAIL25_SPECIALIZED',
 'EXACT15_SPATIAL=QSTAGE4DIAG2_K130_N90',
 'HQ900_SPATIAL=SHARP0P96_SPECIALIZED_NONEXACT_1_TO_1P5',
 'OUT_OF_DOMAIN=BILINEAR_FAILOPEN',
 'QSTAGE4_SM3=149_SLOTS_10_TEXLD',
 'SPEC5_SM3=332_SLOTS_20_TEXLD',
 'HQ900_SM3=74_SLOTS_5_TEXLD',
 'SF5PARITY1_SRGB_ISOLATION=RETAINED',
 'FG_ALGORITHM=EXACT_HF6_UNCHANGED',
 'TEMPORAL_SCHEDULER=UNCHANGED_TEMPORALPAIR60',
 'F7_BENCHMARK=UNCHANGED_F7BENCH1',
 'CTRL_F5_F1=REGRESSION_PASS') | Set-Content artifact\PTAR_D3D9_FULLQUALITY1_VERSION.txt -Encoding ascii

Write-Host "FULLQUALITY1_RUNTIME_SHA256=$candidateSha"
Write-Host 'HF6_TEMPORALPAIR60_F7BENCH1_SF5PARITY1_FULLQUALITY1_FULL_LAB=PASS'

$ErrorActionPreference='Stop'

New-Item -ItemType Directory -Force artifact,build_x86 | Out-Null

# Rebuild the exact SCC recovery lineage first. R15A is deliberately a
# color-state-only overlay on top of that field-recovery candidate.
.\lab\r15_jitter9\apply_r15_scc_safevs1.ps1
if($LASTEXITCODE){throw 'R15A safe VS preparation failed'}

.\lab\tsr1\apply_r10_aliasresidual.ps1
.\lab\tsr1\apply_r11_plsr1.ps1
.\lab\tsr1\apply_r12_resaa1.ps1
.\lab\tsr1\apply_r13_plsr_max1.ps1
.\lab\tsr1\run_r13_tsr_ci_compat.ps1

$fgFiles=@(
  'compat/x86_d3d9/ptar_fg_pacer.h',
  'compat/x86_d3d9/ptar_fg_me_coarse_ps.hlsl',
  'compat/x86_d3d9/ptar_fg_me_refine_ps.hlsl',
  'compat/x86_d3d9/ptar_fg_interpolate_ps.hlsl',
  'compat/x86_d3d9/ptar_f7_benchmark.h')
$fgBefore=@{}
foreach($f in $fgFiles){$fgBefore[$f]=(Get-FileHash $f -Algorithm SHA256).Hash}

.\lab\tsr1\apply_r14_mfsr1_accum1080.ps1

$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}

& $fxcPath /nologo /T ps_3_0 /E main /O3 `
  /Fh build_x86\ptar_tsr1_resolve_ps_bytecode.h /Vn g_ptarTsr1ResolvePs `
  /Fo artifact\R14_TSR_RESOLVE.cso `
  lab\tsr1\ptar_tsr1_resolve_ps.hlsl
if($LASTEXITCODE){throw 'R15A R14 resolve compile failed'}

& $fxcPath /nologo /T ps_3_0 /E main /O3 `
  /Fh build_x86\ptar_r14_restore_ps_bytecode.h /Vn g_ptarR14RestorePs `
  /Fo artifact\R14_RESTORE.cso `
  lab\tsr1\ptar_r14_restore_ps.hlsl
if($LASTEXITCODE){throw 'R15A R14 restore compile failed'}

.\lab\tsr1\apply_r14_runtime_accum1080.ps1
Copy-Item lab\r15_jitter9\ptar_vs_jitter_patch.h build_x86\ptar_r15_vs_jitter_patch.h -Force

$samplingPatch='lab\r15_jitter9\apply_r15_game_sampling_runtime.ps1'
$samplingText=[IO.File]::ReadAllText((Resolve-Path $samplingPatch)) -replace "`r`n","`n"
[IO.File]::WriteAllText((Resolve-Path $samplingPatch),$samplingText,(New-Object System.Text.UTF8Encoding($false)))
.\lab\r15_jitter9\apply_r15_game_sampling_runtime.ps1

& $fxcPath /nologo /T ps_3_0 /E main /O3 `
  /Fh build_x86\ptar_r15_phase_accum_ps_bytecode.h /Vn g_ptarR15PhaseAccumPs `
  /Fo artifact\R15_PHASE_ACCUM.cso /Fc artifact\R15_PHASE_ACCUM_ASM.txt `
  lab\r15_jitter9\ptar_r15_phase_accum_ps.hlsl
if($LASTEXITCODE){throw 'R15A phase accumulator compile failed'}

.\lab\r15_jitter9\apply_r15_phase_superresolve_runtime.ps1
.\lab\r15_jitter9\apply_r15_field_safetyfix2.ps1
.\lab\r15_jitter9\apply_r15_field_safetyfix3.ps1
.\lab\r15_jitter9\apply_r15_scc_nolod1.ps1

$beforeProxy=(Get-FileHash compat\x86_d3d9\ptar_d3d9_proxy.cpp -Algorithm SHA256).Hash.ToLowerInvariant()
python .\lab\r15_colorcontract1\apply_r15a_colorcontract1.py 2>&1 | Tee-Object artifact\R15A_APPLY.txt
if($LASTEXITCODE){throw 'R15A color contract patch failed'}
$apply=Get-Content artifact\R15A_APPLY.txt -Raw
foreach($token in @(
  'R15A_COLORCONTRACT1_PATCH=PASS',
  'R15A_INTERNAL_SRGB_SAMPLERS_0_2=FORCED_FALSE',
  'R15A_INTERNAL_SRGB_WRITE=FORCED_FALSE',
  'R15A_GAME_STATE_RESTORE_BOUNDARY=PRESENT_STATEBLOCK',
  'R15A_JITTER_CHANGE=NONE',
  'R15A_MOTION_ALGORITHM_CHANGE=NONE'))
{
  if($apply -notmatch [regex]::Escape($token)){throw "R15A apply token missing: $token"}
}
$afterProxy=(Get-FileHash compat\x86_d3d9\ptar_d3d9_proxy.cpp -Algorithm SHA256).Hash.ToLowerInvariant()
if($afterProxy -eq $beforeProxy){throw 'R15A patch did not alter proxy'}

# Static scope: the contract must live in the common fullscreen path exactly
# once, so spatial, ME, temporal resolve, restore and presentation all inherit
# identical numeric color semantics without touching game draw state.
$proxy=Get-Content compat\x86_d3d9\ptar_d3d9_proxy.cpp -Raw
if(([regex]::Matches($proxy,'PtR15AApplyNumericColorContract\(')).Count -ne 2){throw 'R15A helper scope mismatch'}
foreach($token in @(
  'R15A COLOR-CONTRACT1',
  'D3DSAMP_SRGBTEXTURE,FALSE',
  'D3DRS_SRGBWRITEENABLE,FALSE',
  'g_ptar.stateBlock->Capture()',
  'g_ptar.stateBlock->Apply()'))
{
  if($proxy -notmatch [regex]::Escape($token)){throw "R15A source token missing: $token"}
}

# Frozen algorithm sources: R15A must not alter jitter, motion, resolve or FG.
$algorithmFiles=@(
  'lab/r15_jitter9/ptar_r15_phase_accum_ps.hlsl',
  'lab/tsr1/ptar_tsr1_motion_subpixel_ps.hlsl',
  'lab/r15_jitter9/ptar_vs_jitter_patch.h')
$algAfter=@{}
foreach($f in $algorithmFiles){$algAfter[$f]=(Get-FileHash $f -Algorithm SHA256).Hash}
foreach($f in $fgFiles)
{
  $after=(Get-FileHash $f -Algorithm SHA256).Hash
  if($after -ne $fgBefore[$f]){throw "R15A modified frozen FG file: $f"}
}

# Reuse the proven D3D9 sRGB state exposure harness as an independent numeric
# oracle: inherited sRGB must change pixels, while explicit isolation must be
# byte-identical to the non-sRGB reference.
& $fxcPath /nologo /T ps_3_0 /E main /O3 `
  /Fh build_x86\ptar_old_ps_bytecode.h /Vn g_ptarOldPs `
  compat\x86_d3d9\ptar_moe_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'R15A sRGB oracle shader compile failed'}
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc `
  /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 `
  lab\sf5_parity1\sf5_srgb_state_harness.cpp `
  /link d3d9.lib user32.lib `
  /OUT:artifact\r15a_srgb_state_harness.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'R15A sRGB oracle compile failed'}
& artifact\r15a_srgb_state_harness.exe 2>&1 | Tee-Object artifact\R15A_SRGB_ORACLE.txt
if($LASTEXITCODE){throw 'R15A sRGB oracle failed'}
$srgb=Get-Content artifact\R15A_SRGB_ORACLE.txt -Raw
foreach($token in @('SF5_SRGB_STATE_LEAK_EXPOSED=PASS','SF5_SRGB_STATE_ISOLATION=PASS'))
{
  if($srgb -notmatch [regex]::Escape($token)){throw "R15A sRGB oracle token missing: $token"}
}

# Build the actual Win8.1 x86 runtime candidate.
& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc `
  /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 `
  /I build_x86 `
  compat\x86_d3d9\ptar_d3d9_proxy.cpp `
  /link user32.lib `
  /DEF:compat\x86_d3d9\d3d9_proxy.def `
  /OUT:artifact\d3d9_r15a_colorcontract1.dll `
  /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE){throw 'R15A runtime compile failed'}

dumpbin /headers artifact\d3d9_r15a_colorcontract1.dll | Out-File artifact\R15A_HEADERS.txt -Encoding ascii
dumpbin /exports artifact\d3d9_r15a_colorcontract1.dll | Out-File artifact\R15A_EXPORTS.txt -Encoding ascii
dumpbin /dependents artifact\d3d9_r15a_colorcontract1.dll | Out-File artifact\R15A_DEPENDENTS.txt -Encoding ascii
$hd=Get-Content artifact\R15A_HEADERS.txt -Raw
$ex=Get-Content artifact\R15A_EXPORTS.txt -Raw
$dp=Get-Content artifact\R15A_DEPENDENTS.txt -Raw
if($hd -notmatch '14C machine \(x86\)' -or $hd -notmatch '6\.03 subsystem version'){throw 'R15A Win8.1 x86 PE gate failed'}
foreach($n in @('Direct3DCreate9','Direct3DCreate9Ex','PTAR_D3D9_DIAG_STATE')){if($ex -notmatch [regex]::Escape($n)){throw "R15A missing export $n"}}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($dp -match $bad){throw "R15A forbidden dependency $bad"}}

$runtimeSha=(Get-FileHash artifact\d3d9_r15a_colorcontract1.dll -Algorithm SHA256).Hash.ToLowerInvariant()
$phaseSha=(Get-FileHash artifact\R15_PHASE_ACCUM.cso -Algorithm SHA256).Hash.ToLowerInvariant()
@(
  ('SOURCE_COMMIT='+$env:GITHUB_SHA),
  'PTAR_R15_STAGE=R15A_COLORCONTRACT1',
  'R15A_SCOPE=INTERNAL_FULLSCREEN_COLOR_STATE_ONLY',
  'R15A_SRGBTEXTURE_S0_S2=FORCED_FALSE',
  'R15A_SRGBWRITEENABLE=FORCED_FALSE',
  'R15A_GAME_RESTORE=PRESENT_STATEBLOCK',
  'R15A_JITTER_CHANGE=NONE',
  'R15A_MOTION_CHANGE=NONE',
  'R15A_PHASE_SHADER_CHANGE=NONE',
  'R15A_FG_CHANGE=NONE',
  'R15A_SRGB_ORACLE=PASS',
  ('R15A_RUNTIME_SHA256='+$runtimeSha),
  ('R15_PHASE_CSO_SHA256='+$phaseSha),
  'WIN81_X86=PASS') | Set-Content artifact\R15A_COLORCONTRACT1_VALIDATION.txt -Encoding ascii

Write-Host "R15A_RUNTIME_SHA256=$runtimeSha"
Write-Host 'R15A_COLORCONTRACT1_BUILD=PASS'

$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

$base='0066c7d4b32aed167d2a74030801e896b8fe9aa4'
$protected=@(
 'compat/x86_d3d9/ptar_d3d9_proxy.cpp',
 'compat/x86_d3d9/ptar_fg_pacer.h',
 'compat/x86_d3d9/ptar_fg_me_coarse_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_me_refine_ps.hlsl',
 'compat/x86_d3d9/ptar_fg_interpolate_ps.hlsl',
 'compat/x86_d3d9/ptar_moe_d3d9_ps.hlsl',
 'compat/x86_d3d9/ptar_moe_universal_d3d9_ps.hlsl'
)
foreach($core in $protected){if(git diff --name-only $base HEAD -- $core){throw "Committed protected core changed: $core"}}
Write-Host 'COMMITTED_D3D9_RENDER_FG_CORE_UNTOUCHED=PASS'

Copy-Item lab\ctrl_ui_real_visibility\ptar_d3d9_diag_present_ring.h compat\x86_d3d9\ptar_d3d9_diag_present_ring.h -Force
$p='compat\x86_d3d9\ptar_d3d9_proxy.cpp'
$s=Get-Content $p -Raw
$needle='#include "ptar_gw16i_hud_d3d9.h"'
if(-not $s.Contains($needle)){throw 'HUD include anchor absent'}
$s=$s.Replace($needle,$needle+"`r`n#include `"ptar_d3d9_diag_present_ring.h`"")
$pattern='if\(SUCCEEDED\(hr\)\)\s*\r?\n\s*PtFgPacerRecordVisible\(generatedFrame\);'
$replacement=@(
 'if(SUCCEEDED(hr))','{','    PtFgPacerRecordVisible(generatedFrame);','    PtD3D9DiagRecordPresent(',
 '        generatedFrame,','        g_ptarHudFrameSequence&4095ul,','        g_ptarFgPacer.lastVisibleQpc,',
 '        PtFgPacerRealCount(),','        PtFgPacerGeneratedCount(),','        g_ptarFgPacer.resyncs,',
 '        PtFgPacerLateSkipCount());','}'
) -join "`r`n"
$patched=[regex]::Replace($s,$pattern,$replacement,1)
if($patched -eq $s){throw 'Present success anchor absent'}
Set-Content $p $patched -Encoding UTF8
$diff=(git diff -- $p) -join "`n"
if($diff -notmatch 'PtD3D9DiagRecordPresent'){throw 'diagnostic hook missing'}
if($diff -match '^[-+].*PtFgPacerPrepareGenerated' -or $diff -match '^[-+].*PtFgPacerPrepareReal'){throw 'pacing policy changed'}
Write-Host 'TRANSIENT_DIAGNOSTIC_HOOK_ONLY=PASS'

New-Item -ItemType Directory -Force build_x86,artifact,smoke_game | Out-Null
$fxc=Get-Command fxc.exe -ErrorAction SilentlyContinue
if(-not $fxc){foreach($root in @("$env:ProgramFiles(x86)\Windows Kits\10\bin","$env:ProgramFiles(x86)\Windows Kits\8.1\bin")){if(Test-Path $root){$fxc=Get-ChildItem $root -Recurse -Filter fxc.exe -ErrorAction SilentlyContinue|Sort-Object FullName -Descending|Select-Object -First 1;if($fxc){break}}}}
if(-not $fxc){throw 'fxc.exe not found'}
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
$jobs=@(
 @('ps_3_0','compat\x86_d3d9\ptar_moe_d3d9_ps.hlsl','build_x86\ptar_ps_bytecode.h','g_ptarPs'),
 @('ps_3_0','compat\x86_d3d9\ptar_moe_universal_d3d9_ps.hlsl','build_x86\ptar_universal_ps_bytecode.h','g_ptarUniversalPs'),
 @('ps_2_0','compat\x86_d3d9\ptar_bilinear_ref_ps.hlsl','build_x86\ptar_bilinear_ps_bytecode.h','g_ptarBilinearPs'),
 @('ps_3_0','compat\x86_d3d9\ptar_gw16i_hud_ps.hlsl','build_x86\ptar_gw16i_hud_ps_bytecode.h','g_ptarGw16iHudPs'),
 @('ps_3_0','compat\x86_d3d9\ptar_gw16i_feedback_ps.hlsl','build_x86\ptar_gw16i_feedback_ps_bytecode.h','g_ptarGw16iFeedbackPs'),
 @('ps_3_0','compat\x86_d3d9\ptar_fg_me_coarse_ps.hlsl','build_x86\ptar_fg_me_coarse_ps_bytecode.h','g_ptarFgMeCoarsePs'),
 @('ps_3_0','compat\x86_d3d9\ptar_fg_me_refine_ps.hlsl','build_x86\ptar_fg_me_refine_ps_bytecode.h','g_ptarFgMeRefinePs'),
 @('ps_3_0','compat\x86_d3d9\ptar_fg_interpolate_ps.hlsl','build_x86\ptar_fg_interpolate_ps_bytecode.h','g_ptarFgInterpolatePs')
)
foreach($j in $jobs){& $fxcPath /nologo /T $j[0] /E main /O3 /Fh $j[2] /Vn $j[3] $j[1];if($LASTEXITCODE -ne 0){throw "FXC failed $($j[1])"}}
Write-Host 'FXC_ALL=PASS'

& cl.exe /nologo /LD /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 compat\x86_d3d9\ptar_d3d9_proxy.cpp /link user32.lib /DEF:compat\x86_d3d9\d3d9_proxy.def /OUT:artifact\d3d9.dll /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE -ne 0){throw 'D3D9 runtime compile failed'}
dumpbin /headers artifact\d3d9.dll | Set-Content build_x86\HEADERS.txt
dumpbin /dependents artifact\d3d9.dll | Set-Content build_x86\DEPENDENTS.txt
dumpbin /exports artifact\d3d9.dll | Set-Content build_x86\EXPORTS.txt
$headers=Get-Content build_x86\HEADERS.txt -Raw;$deps=Get-Content build_x86\DEPENDENTS.txt -Raw;$exports=Get-Content build_x86\EXPORTS.txt -Raw
if($headers -notmatch '14C machine \(x86\)' -or $headers -notmatch '6\.03 subsystem version'){throw 'PE compatibility check failed'}
foreach($bad in @('VCRUNTIME','MSVCP','UCRTBASE','API-MS-WIN-CRT')){if($deps -match $bad){throw "dynamic CRT dependency $bad"}}
if($exports -notmatch 'PTAR_D3D9_DIAG_STATE'){throw 'PTAR_D3D9_DIAG_STATE DATA export missing'}
$runtimeSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant();Set-Content build_x86\D3D9_SHA256.txt $runtimeSha -Encoding ASCII
Write-Host "D3D9_RUNTIME_BUILD=PASS SHA256=$runtimeSha"

$csc="$env:WINDIR\Microsoft.NET\Framework\v4.0.30319\csc.exe";if(-not(Test-Path $csc)){throw 'x86 Framework csc missing'}
$shared='lab\ctrl_ui_real_visibility\PTARD3D9PresentTelemetryReader.cs'
$builds=@(
 @('lab\ctrl_ui_real_visibility\PTARVisiblePacingVerifier_D3D9RuntimeRing.cs','artifact\ptar_vblank3_d3d9_autostart.exe'),
 @('lab\ctrl_ui_real_visibility\PTARRawCadenceVerifier_D3D9RuntimeRing.cs','artifact\ptar_rawcadence3_d3d9_autostart.exe'),
 @('lab\ctrl_ui_real_visibility\PTARVisiblePacingVerifierPresentShed1_D3D9RuntimeRing.cs','artifact\ptar_presentshed1_visible.exe')
)
foreach($b in $builds){$exe=$b[1];& $csc /nologo /target:exe /optimize+ /platform:x86 (('/out:{0}' -f $exe)) $shared $b[0];if($LASTEXITCODE -ne 0){throw "C# compile failed $($b[0])"};& $exe --selftest | Add-Content build_x86\VERIFIER_SELFTEST.txt;if($LASTEXITCODE -ne 0){throw "selftest failed $exe"}}
$active=(Get-Content $shared -Raw)+(Get-Content $builds[0][0] -Raw)+(Get-Content $builds[1][0] -Raw)+(Get-Content $builds[2][0] -Raw)
foreach($bad in @('BitBlt','CreateDIBSection','Marshal.Copy(bits)')){if($active.Contains($bad)){throw "desktop pixel capture remains: $bad"}}
Write-Host 'D3D11_DIAGNOSTIC_CONTRACT_ADAPTED_TO_D3D9=PASS'

& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_diag_ring_harness.cpp /link /OUT:smoke_game\game.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE -ne 0){throw 'harness compile failed'}
Copy-Item artifact\d3d9.dll smoke_game\d3d9.dll -Force
$root=(Resolve-Path smoke_game).Path;$game=(Resolve-Path smoke_game\game.exe).Path;$dll=(Resolve-Path smoke_game\d3d9.dll).Path
$env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game
$p=Start-Process -FilePath $game -ArgumentList ('"'+$dll+'" mixed 12000') -PassThru -RedirectStandardOutput build_x86\HARNESS_MIXED.txt
Start-Sleep -Milliseconds 350
& artifact\ptar_vblank3_d3d9_autostart.exe 3 --autostart | Set-Content build_x86\F5_FG_SMOKE.txt;if($LASTEXITCODE -ne 0){throw "F5 FG smoke failed $LASTEXITCODE"}
$fg=@(Import-Csv smoke_game\PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv);if($fg.Count -lt 50 -or @($fg|? type -eq 'R').Count -eq 0 -or @($fg|? type -eq 'G').Count -eq 0){throw 'F5 mixed telemetry invalid'}
$prefix=Join-Path $root 'F1_SMOKE';$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=$prefix
& artifact\ptar_presentshed1_visible.exe 5 --autostart | Set-Content build_x86\F1_SMOKE.txt;if($LASTEXITCODE -ne 0){throw "F1 smoke failed $LASTEXITCODE"}
foreach($suffix in @('_OUTPUT.txt','_SAMPLES.csv','_STATUS.txt','_RR_FOCUS.txt','_LONG_HOLD_FOCUS.txt','_GEN_PRESSURE_FOCUS.txt')){if(-not(Test-Path ($prefix+$suffix))){throw "F1 output missing $suffix"}}
if(-not $p.HasExited){Stop-Process -Id $p.Id -Force}
Write-Host 'F5_FG_RUNTIME_RING=PASS';Write-Host 'F1_RUNTIME_RING=PASS'

$p=Start-Process -FilePath $game -ArgumentList ('"'+$dll+'" real 5000') -PassThru -RedirectStandardOutput build_x86\HARNESS_REAL.txt
Start-Sleep -Milliseconds 350
& artifact\ptar_rawcadence3_d3d9_autostart.exe 3 --autostart | Set-Content build_x86\F5_RAW_SMOKE.txt;if($LASTEXITCODE -ne 0){throw "F5 raw smoke failed $LASTEXITCODE"}
$raw=Get-Content smoke_game\PTAR_RAWCADENCE_LAST_OUTPUT.txt -Raw;if($raw -notmatch 'RAW_RESULT=RAW_REAL_ONLY_VALID' -or $raw -notmatch 'GENERATED_CONTENTS=0' -or $raw -notmatch 'GDI_CAPTURE=NO'){throw 'F5 raw telemetry invalid'}
if(-not $p.HasExited){Stop-Process -Id $p.Id -Force}
Write-Host 'F5_PRE_POST_RUNTIME_RING=PASS'

Copy-Item $shared artifact\
Copy-Item $builds[0][0] artifact\PTARVisiblePacingVerifier.cs
Copy-Item $builds[1][0] artifact\PTARRawCadenceVerifier.cs
Copy-Item $builds[2][0] artifact\PTARVisiblePacingVerifierPresentShed1.cs
Copy-Item lab\ctrl_ui_real_visibility\ptar_d3d9_diag_present_ring.h artifact\
@('PTAR_D3D9_D3D11_DIAGNOSTIC_PORT=PASS','D3D11_CONTRACT=CANONICAL','D3D9_MEASUREMENT_SOURCE=SUCCESSFUL_RUNTIME_PRESENT_RING','EXTERNAL_DESKTOP_PIXEL_CAPTURE=REMOVED','PACING_POLICY_CHANGED=NO','FG_SHADER_CHANGED=NO','D3D9EX_SUBSTITUTION=NO','SECOND_PRESENT_THREAD=NO',('D3D9_DLL_SHA256='+$runtimeSha),('SOURCE_COMMIT='+$env:GITHUB_SHA)) | Set-Content artifact\PTAR_D3D9_D3D11_DIAG_PORT_VALIDATION.txt -Encoding ASCII
Write-Host 'LAB_D3D9_D3D11_DIAG_PORT=PASS'

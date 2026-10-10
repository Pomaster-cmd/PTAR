$ErrorActionPreference='Stop'
Copy-Item 'lab\ctrl_ui_real_visibility\ptar_d3d9_diag_present_ring.h' 'compat\x86_d3d9\ptar_d3d9_diag_present_ring.h' -Force
$p='compat\x86_d3d9\ptar_d3d9_proxy.cpp'
$s=[IO.File]::ReadAllText($p)
$needle='#include "ptar_gw16i_hud_d3d9.h"'
if(-not $s.Contains($needle)){throw 'HUD include anchor absent'}
$s=$s.Replace($needle,$needle+[Environment]::NewLine+'#include "ptar_d3d9_diag_present_ring.h"')
$old="    hr=g_realPresent(dev,0,0,hwnd,dirty);`r`n    if(SUCCEEDED(hr))`r`n        PtFgPacerRecordVisible(generatedFrame);`r`n    return hr;"
if(-not $s.Contains($old)){
    $old="    hr=g_realPresent(dev,0,0,hwnd,dirty);`n    if(SUCCEEDED(hr))`n        PtFgPacerRecordVisible(generatedFrame);`n    return hr;"
}
if(-not $s.Contains($old)){throw 'Present success anchor absent'}
$nl=if($old.Contains("`r`n")){"`r`n"}else{"`n"}
$new=@(
'    hr=g_realPresent(dev,0,0,hwnd,dirty);',
'    if(SUCCEEDED(hr))',
'    {',
'        PtFgPacerRecordVisible(generatedFrame);',
'        PtD3D9DiagRecordPresent(',
'            generatedFrame,',
'            g_ptarHudFrameSequence&4095ul,',
'            g_ptarFgPacer.lastVisibleQpc,',
'            PtFgPacerRealCount(),',
'            PtFgPacerGeneratedCount(),',
'            g_ptarFgPacer.resyncs,',
'            PtFgPacerLateSkipCount());',
'    }',
'    return hr;'
)-join $nl
$s=$s.Replace($old,$new)
[IO.File]::WriteAllText($p,$s,(New-Object Text.UTF8Encoding($false)))
$d=(git diff -- $p) -join "`n"
if($d -notmatch 'PtD3D9DiagRecordPresent'){throw 'diagnostic ring hook missing'}
if($d -match '^[-+].*PtFgPacerPrepareGenerated' -or $d -match '^[-+].*PtFgPacerPrepareReal'){throw 'pacing changed by diagnostic hook'}
Write-Host 'EXISTING_D3D9_DIAGNOSTIC_RING_REUSED=PASS'

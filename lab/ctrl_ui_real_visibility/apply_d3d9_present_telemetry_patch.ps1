$ErrorActionPreference='Stop'
$path='compat\x86_d3d9\ptar_d3d9_proxy.cpp'
if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw 'ptar_d3d9_proxy.cpp missing'}
$text=[IO.File]::ReadAllText($path)
$includeNeedle='#include "ptar_fg_pacer.h"'
$includeReplacement=$includeNeedle+[Environment]::NewLine+'#include "ptar_present_telemetry.h"'
if($text.Contains('#include "ptar_present_telemetry.h"')){throw 'telemetry include already present'}
if(-not $text.Contains($includeNeedle)){throw 'pacer include anchor missing'}
$text=$text.Replace($includeNeedle,$includeReplacement)
$old=@"
    hr=g_realPresent(dev,0,0,hwnd,dirty);
    if(SUCCEEDED(hr))
        PtFgPacerRecordVisible(generatedFrame);
    return hr;
"@
$new=@"
    hr=g_realPresent(dev,0,0,hwnd,dirty);
    if(SUCCEEDED(hr))
    {
        PtFgPacerRecordVisible(generatedFrame);
        PtPresentTelemetryRecord(generatedFrame);
    }
    return hr;
"@
$count=([regex]::Matches($text,[regex]::Escape($old))).Count
if($count -ne 1){throw ('present success anchor count='+$count+' expected=1')}
$text=$text.Replace($old,$new)
[IO.File]::WriteAllText($path,$text,(New-Object Text.UTF8Encoding($false)))
$diff=git diff -- $path
if($LASTEXITCODE -ne 0){throw 'git diff failed'}
if(($diff | Select-String -SimpleMatch 'PtPresentTelemetryRecord(generatedFrame);').Count -ne 1){throw 'telemetry record diff missing'}
Write-Host 'D3D9_PRESENT_TELEMETRY_PATCH=PASS'

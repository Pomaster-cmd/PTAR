$ErrorActionPreference='Stop'

# The complete R3->R7 qualification chain can legitimately add cleanup work
# inside ReleasePTARResources(). The original R8 patcher anchored on four
# consecutive lines there, which made the R8 overlay depend on incidental
# formatting/order from an earlier source state. Adapt only that one hook to a
# stable unique anchor, then execute the canonical R8 patcher unchanged.
$proxyPath='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$proxy=[IO.File]::ReadAllText((Resolve-Path $proxyPath)) -replace "`r`n","`n"
$stateBlockRelease='    if(g_ptar.stateBlock){g_ptar.stateBlock->Release();g_ptar.stateBlock=0;}'
$first=$proxy.IndexOf($stateBlockRelease,[StringComparison]::Ordinal)
if($first -lt 0){throw 'R8 compat patch: stateBlock release anchor missing'}
$second=$proxy.IndexOf($stateBlockRelease,$first+$stateBlockRelease.Length,[StringComparison]::Ordinal)
if($second -ge 0){throw 'R8 compat patch: stateBlock release anchor not unique'}
if($proxy.IndexOf('PtTsr1Release();',[StringComparison]::Ordinal) -ge 0){throw 'R8 compat patch: TSR release already present before overlay'}
$proxy=$proxy.Substring(0,$first)+'    PtTsr1Release();' + "`n" + $proxy.Substring($first)
$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $proxyPath),$proxy,$utf8)

# Remove only the now-redundant brittle release-hook stanza from a temporary
# copy of the canonical patcher. All other R8 anchors and mutations remain
# exactly those maintained in apply_r8_proxy_patch.ps1.
$canonical='lab/tsr1/apply_r8_proxy_patch.ps1'
$patch=[IO.File]::ReadAllText((Resolve-Path $canonical)) -replace "`r`n","`n"
$blockStart="Replace-One @'`n    g_ptar.active=false;`n    g_ptar.spatialActive=false;`n    g_ptar.previousRealValid=false;`n    if(g_ptar.stateBlock){g_ptar.stateBlock->Release();g_ptar.stateBlock=0;}`n'@ @'"
$start=$patch.IndexOf($blockStart,[StringComparison]::Ordinal)
if($start -lt 0){throw 'R8 compat patch: canonical TSR release stanza start missing'}
$endMarker="'@ 'TSR release hook'"
$end=$patch.IndexOf($endMarker,$start,[StringComparison]::Ordinal)
if($end -lt 0){throw 'R8 compat patch: canonical TSR release stanza end missing'}
$end+=$endMarker.Length
while($end -lt $patch.Length -and ($patch[$end] -eq "`n" -or $patch[$end] -eq "`r")){$end++}
$patch=$patch.Substring(0,$start)+$patch.Substring($end)

$temp='lab/tsr1/.apply_r8_proxy_patch_compat_generated.ps1'
[IO.File]::WriteAllText((Join-Path (Get-Location) $temp),$patch,$utf8)
try
{
    & $temp
    if($LASTEXITCODE){throw "R8 canonical proxy patch failed with exit code $LASTEXITCODE"}
}
finally
{
    Remove-Item $temp -Force -ErrorAction SilentlyContinue
}

$final=[IO.File]::ReadAllText((Resolve-Path $proxyPath))
foreach($token in @('PtTsr1Release();','PtTsr1Initialize(','PtTsr1Resolve(','PtTsr1StoreSource('))
{
    if($final -notmatch [regex]::Escape($token)){throw "R8 compat patch missing final token: $token"}
}
Write-Host 'R8_PROXY_PATCH_COMPAT=PASS'

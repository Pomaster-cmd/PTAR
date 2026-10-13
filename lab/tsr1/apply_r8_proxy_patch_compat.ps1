$ErrorActionPreference='Stop'

# R8 is applied after the complete R3->R7 qualification lineage. Two anchors
# in the original R8 patcher describe an earlier proxy shape: cleanup order and
# the pre-TEMPORALPAIR60 synchronous Present block. Handle only those integration
# points here; all other canonical R8 mutations still run unchanged.
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

$canonical='lab/tsr1/apply_r8_proxy_patch.ps1'
$patch=[IO.File]::ReadAllText((Resolve-Path $canonical)) -replace "`r`n","`n"

# Remove the now-redundant brittle release-hook stanza from the temporary copy.
$blockStart="Replace-One @'`n    g_ptar.active=false;`n    g_ptar.spatialActive=false;`n    g_ptar.previousRealValid=false;`n    if(g_ptar.stateBlock){g_ptar.stateBlock->Release();g_ptar.stateBlock=0;}`n'@ @'"
$start=$patch.IndexOf($blockStart,[StringComparison]::Ordinal)
if($start -lt 0){throw 'R8 compat patch: canonical TSR release stanza start missing'}
$endMarker="'@ 'TSR release hook'"
$end=$patch.IndexOf($endMarker,$start,[StringComparison]::Ordinal)
if($end -lt 0){throw 'R8 compat patch: canonical TSR release stanza end missing'}
$end+=$endMarker.Length
while($end -lt $patch.Length -and ($patch[$end] -eq "`n" -or $patch[$end] -eq "`r")){$end++}
$patch=$patch.Substring(0,$start)+$patch.Substring($end)

# The canonical R8 Present replacement targets the old synchronous FG block.
# R7 qualification has already replaced that block with TEMPORALPAIR60. Strip
# only this obsolete replacement from the temporary copy; a dedicated adapter
# below patches the qualified presenter semantically and keeps its mailbox path.
$presentStartMarker='$oldPresent=@'+[char]39
$presentStart=$patch.IndexOf($presentStartMarker,[StringComparison]::Ordinal)
if($presentStart -lt 0){throw 'R8 compat patch: canonical Present stanza start missing'}
$presentEndMarker='Replace-One $oldPresent $newPresent '+[char]39+'Present temporal resolve block'+[char]39
$presentEnd=$patch.IndexOf($presentEndMarker,$presentStart,[StringComparison]::Ordinal)
if($presentEnd -lt 0){throw 'R8 compat patch: canonical Present stanza end missing'}
$presentEnd+=$presentEndMarker.Length
while($presentEnd -lt $patch.Length -and ($patch[$presentEnd] -eq "`n" -or $patch[$presentEnd] -eq "`r")){$presentEnd++}
$patch=$patch.Substring(0,$presentStart)+$patch.Substring($presentEnd)

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

# Integrate TSR1 into the post-R7 TEMPORALPAIR60 HookPresent without replacing
# or weakening its already-qualified asynchronous FG publisher.
& .\lab\tsr1\apply_r8_hookpresent_temporalpair60.ps1
if($LASTEXITCODE){throw 'R8 TEMPORALPAIR60 HookPresent integration failed'}

$final=[IO.File]::ReadAllText((Resolve-Path $proxyPath))
foreach($token in @(
    'PtTsr1Release();',
    'PtTsr1Initialize(',
    'PtTsr1InvalidateHistory();',
    'PtTsr1Resolve(',
    'PtTsr1StoreSource(',
    'PtTemporalPublishPair(self,hwnd)',
    'PRESENT_REAL_TSR1'))
{
    if($final -notmatch [regex]::Escape($token)){throw "R8 compat patch missing final token: $token"}
}
Write-Host 'R8_PROXY_PATCH_COMPAT=PASS'

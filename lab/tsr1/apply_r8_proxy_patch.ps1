$ErrorActionPreference='Stop'

# Canonical R8 integration is intentionally split in two orthogonal pieces:
# resource/shader lifetime wiring, then the qualified TEMPORALPAIR60 Present
# integration. This avoids carrying an obsolete synchronous Present snapshot.
& .\lab\tsr1\apply_r8_proxy_core.ps1
if($LASTEXITCODE){throw 'R8 proxy core patch failed'}

& .\lab\tsr1\apply_r8_hookpresent_temporalpair60.ps1
if($LASTEXITCODE){throw 'R8 TEMPORALPAIR60 HookPresent integration failed'}

$proxyPath='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$final=[IO.File]::ReadAllText((Resolve-Path $proxyPath))
foreach($token in @(
    'PtTsr1Release();',
    'PtTsr1Initialize(',
    'PtTsr1InvalidateHistory();',
    'PtTsr1Resolve(',
    'g_ptar.currentRealTexture,g_ptar.generatedSurface',
    'PtTsr1StoreSource(',
    'PtTemporalPublishPair(self,hwnd)',
    'PRESENT_REAL_TSR1'))
{
    if($final -notmatch [regex]::Escape($token)){throw "R8 canonical proxy patch missing final token: $token"}
}
if($final -match [regex]::Escape('g_ptar.currentRealTexture=g_ptar.generatedTexture'))
{
    throw 'R8 canonical proxy patch contaminates qualified FG history with TSR output'
}

Write-Host 'R8_PROXY_PATCH=PASS'
Write-Host 'R8_PROXY_PATCH_TOPOLOGY=CORE_PLUS_TEMPORALPAIR60_SEMANTIC'

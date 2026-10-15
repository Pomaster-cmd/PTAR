$ErrorActionPreference='Stop'
$path='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$utf8=New-Object System.Text.UTF8Encoding($false)
$p=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

if($p.IndexOf('R15_FIELD_SAFETYFIX3',[StringComparison]::Ordinal) -lt 0)
{ throw 'R15 SCC NOLOD1 requires SAFETYFIX3' }
if($p.IndexOf('R15_SCC_NOLOD1',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 SCC NOLOD1 already applied' }

$pattern='(?s)static DWORD PtR15NativeLodBiasState\(DWORD raw\)\s*\{.*?\n\}'
$m=[regex]::Matches($p,$pattern)
if($m.Count -ne 1){throw "R15 SCC NOLOD1 bias function count=$($m.Count)"}
$replacement=@'
// R15_SCC_NOLOD1
// First SCC recovery build keeps the game's exact sampler LOD state. The field
// failure is a black-screen/AppHang, so geometry sampling and phase accumulation
// must be proven stable before native-footprint mip bias is reintroduced.
static DWORD PtR15NativeLodBiasState(DWORD raw)
{
    return raw;
}
'@
$p=$p.Substring(0,$m[0].Index)+$replacement+$p.Substring($m[0].Index+$m[0].Length)

if($p.IndexOf('R15_SCC_NOLOD1',[StringComparison]::Ordinal) -lt 0 -or
   $p.IndexOf('return raw;',[StringComparison]::Ordinal) -lt 0)
{ throw 'R15 SCC NOLOD1 final gate failed' }

[IO.File]::WriteAllText((Resolve-Path $path),$p,$utf8)
Write-Host 'R15_SCC_NOLOD1=PASS'
Write-Host 'R15_SCC_TEXTURE_LOD_BIAS=DISABLED_FOR_STARTUP_ISOLATION'

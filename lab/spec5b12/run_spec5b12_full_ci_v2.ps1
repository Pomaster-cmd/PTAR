$ErrorActionPreference='Stop'
$src=Get-Content .\lab\spec5b12\run_spec5b12_full_ci.ps1 -Raw
$old="$fullQualityRef='lab/d3d9-ptar-fullquality1-20261012'"
$new="$fullQualityRef='origin/lab/d3d9-ptar-fullquality1-20261012'"
if(-not $src.Contains($old)){throw 'expected FULLQUALITY1 local ref marker not found'}
$src=$src.Replace($old,$new)
$tmp=Join-Path $env:RUNNER_TEMP 'run_spec5b12_full_ci_v2_expanded.ps1'
Set-Content $tmp $src -Encoding utf8
& $tmp
if($LASTEXITCODE){exit $LASTEXITCODE}

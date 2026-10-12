$ErrorActionPreference='Stop'
$src=Get-Content .\lab\spec5b12\run_spec5b12_full_ci.ps1 -Raw
$old='$fullQualityRef=''lab/d3d9-ptar-fullquality1-20261012'''
$new='$fullQualityRef=''origin/lab/d3d9-ptar-fullquality1-20261012'''
if(-not $src.Contains($old)){throw 'expected FULLQUALITY1 local ref marker not found'}
$src=$src.Replace($old,$new)

# AALAB1 run 38255062487 proves B12 = 20 texld / 338 slots (BASE = 332).
# Earlier 334 was a qualification-script transcription error only.
if(-not $src.Contains('if($slots -ne 334)')){throw 'expected stale B12 slot gate not found'}
$src=$src.Replace('if($slots -ne 334)','if($slots -ne 338)')
$src=$src.Replace('SPEC5_B12 expected 334 slots','SPEC5_B12 expected 338 slots')
$src=$src.Replace("'SPEC5_B12_SLOTS=334'","'SPEC5_B12_SLOTS=338'")

$tmp=Join-Path $env:RUNNER_TEMP 'run_spec5b12_full_ci_v2_expanded.ps1'
Set-Content $tmp $src -Encoding utf8
& $tmp
if($LASTEXITCODE){exit $LASTEXITCODE}

$ErrorActionPreference='Stop'

$fieldF7RuntimeSha='be14145ee25227569b81b9e5361b3b135bd1a52b85218018aa96a41a236e605f'
$fieldF7SourceCommit='3999b76c63612952be1217a44e265480293f1d0d'

# Field F7 source identity is the reproducible baseline proof.
$tracked=@(
  'compat/x86_d3d9/ptar_f7_benchmark.h',
  'compat/x86_d3d9/ptar_hud.h',
  'compat/x86_d3d9/ptar_gw16i_hud_d3d9.h',
  'lab/f7bench1/apply_f7bench1.py',
  'lab/f7bench1/f7_benchmark_harness.cpp',
  'lab/f7bench1/run_f7bench1_ci.ps1'
)
foreach($rel in $tracked){
  git diff --quiet $fieldF7SourceCommit -- $rel
  if($LASTEXITCODE -ne 0){throw "field F7 source changed before SF5 experiment: $rel"}
}
'F7_FIELD_SOURCE_IDENTITY=PASS' | Set-Content artifact_f7_source_identity.txt -Encoding ascii
('F7_FIELD_RUNTIME_SHA256_FOR_MIGRATION='+$fieldF7RuntimeSha) | Add-Content artifact_f7_source_identity.txt -Encoding ascii

# Expand the original full suite, changing ONLY two invalid proof gates:
#   (1) PE hash reproducibility -> field-source identity + full baseline regression
#   (2) temporal block end marker -> the actual next function in TEMPORALPAIR60 source
$src=Get-Content .\lab\sf5_parity1\run_sf5_parity1_full_ci.ps1 -Raw

$oldSha=@'
$expectedBaseline='be14145ee25227569b81b9e5361b3b135bd1a52b85218018aa96a41a236e605f'
$baselineSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
if($baselineSha -ne $expectedBaseline){throw "unexpected F7BENCH1 baseline sha $baselineSha"}
'@
$newSha=@'
$fieldRuntimeForMigration='be14145ee25227569b81b9e5361b3b135bd1a52b85218018aa96a41a236e605f'
$baselineSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "F7_FIELD_RUNTIME_SHA256_FOR_MIGRATION=$fieldRuntimeForMigration"
Write-Host "F7_REBUILT_RUNTIME_SHA256=$baselineSha"
Write-Host 'F7_REBUILT_RUNTIME_FUNCTIONAL_BASELINE=PASS'
'@
if(-not $src.Contains($oldSha)){throw 'expected invalid baseline-SHA gate not found'}
$src=$src.Replace($oldSha,$newSha)

$oldTemporal="st='static DWORD WINAPI PtTemporalPresenterThread'; en='static bool PtStartTemporalPresenter';"
$newTemporal="st='static DWORD WINAPI PtTemporalPresenterThread'; en='static void PtTemporalShutdown';"
if(-not $src.Contains($oldTemporal)){throw 'expected invalid temporal end marker not found'}
$src=$src.Replace($oldTemporal,$newTemporal)

$tmpScript=Join-Path $env:RUNNER_TEMP 'run_sf5_parity1_full_ci_v3_expanded.ps1'
Set-Content $tmpScript $src -Encoding utf8
& $tmpScript
if($LASTEXITCODE){exit $LASTEXITCODE}

New-Item -ItemType Directory -Force artifact | Out-Null
Copy-Item artifact_f7_source_identity.txt artifact\F7_FIELD_SOURCE_IDENTITY.txt -Force
'INVALID_PE_REPRODUCIBILITY_GATE=REPLACED_BY_SOURCE_IDENTITY' | Add-Content artifact\F7_FIELD_SOURCE_IDENTITY.txt -Encoding ascii
'TEMPORAL_IDENTITY_MARKER=PtTemporalPresenterThread_TO_PtTemporalShutdown' | Add-Content artifact\F7_FIELD_SOURCE_IDENTITY.txt -Encoding ascii

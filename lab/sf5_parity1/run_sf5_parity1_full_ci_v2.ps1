$ErrorActionPreference='Stop'

# The field F7 runtime SHA is an installation/migration identity, NOT a reproducible-build
# identity: MSVC/link.exe may change PE build metadata across otherwise source-identical builds.
$fieldF7RuntimeSha='be14145ee25227569b81b9e5361b3b135bd1a52b85218018aa96a41a236e605f'
$fieldF7SourceCommit='3999b76c63612952be1217a44e265480293f1d0d'

# Strong reproducible baseline gate: the F7 implementation and qualification sources inherited
# from the field-validated commit must be byte-identical before the spatial experiment begins.
$tracked=@(
  'compat/x86_d3d9/ptar_f7_benchmark.h',
  'compat/x86_d3d9/ptar_hud.h',
  'compat/x86_d3d9/ptar_gw16i_hud_d3d9.h',
  'lab/f7bench1/apply_f7bench1.py',
  'lab/f7bench1/f7_benchmark_harness.cpp',
  'lab/f7bench1/run_f7bench1_ci.ps1'
)
foreach($rel in $tracked){
  $current=(Get-FileHash $rel -Algorithm SHA256).Hash.ToLowerInvariant()
  $tmp=Join-Path $env:RUNNER_TEMP ('field_'+[IO.Path]::GetFileName($rel))
  git show ($fieldF7SourceCommit+':'+$rel) | Set-Content $tmp -Encoding utf8 -NoNewline
  if($LASTEXITCODE){throw "cannot read field F7 source $rel"}
  # git-show through PowerShell can normalize final newlines, so use git diff as authoritative identity.
  git diff --quiet $fieldF7SourceCommit -- $rel
  if($LASTEXITCODE -ne 0){throw "field F7 source changed before SF5 experiment: $rel"}
}
'F7_FIELD_SOURCE_IDENTITY=PASS' | Set-Content artifact_f7_source_identity.txt -Encoding ascii
('F7_FIELD_RUNTIME_SHA256_FOR_MIGRATION='+$fieldF7RuntimeSha) | Add-Content artifact_f7_source_identity.txt -Encoding ascii

# Reuse the full qualification script, changing ONLY its invalid exact-PE-SHA assertion.
$src=Get-Content .\lab\sf5_parity1\run_sf5_parity1_full_ci.ps1 -Raw
$old=@'
$expectedBaseline='be14145ee25227569b81b9e5361b3b135bd1a52b85218018aa96a41a236e605f'
$baselineSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
if($baselineSha -ne $expectedBaseline){throw "unexpected F7BENCH1 baseline sha $baselineSha"}
'@
$new=@'
$fieldRuntimeForMigration='be14145ee25227569b81b9e5361b3b135bd1a52b85218018aa96a41a236e605f'
$baselineSha=(Get-FileHash artifact\d3d9.dll -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "F7_FIELD_RUNTIME_SHA256_FOR_MIGRATION=$fieldRuntimeForMigration"
Write-Host "F7_REBUILT_RUNTIME_SHA256=$baselineSha"
Write-Host 'F7_REBUILT_RUNTIME_FUNCTIONAL_BASELINE=PASS'
'@
if(-not $src.Contains($old)){throw 'expected invalid baseline-SHA gate not found'}
$src=$src.Replace($old,$new)
$tmpScript=Join-Path $env:RUNNER_TEMP 'run_sf5_parity1_full_ci_v2_expanded.ps1'
Set-Content $tmpScript $src -Encoding utf8
& $tmpScript
if($LASTEXITCODE){exit $LASTEXITCODE}

New-Item -ItemType Directory -Force artifact | Out-Null
Copy-Item artifact_f7_source_identity.txt artifact\F7_FIELD_SOURCE_IDENTITY.txt -Force
'INVALID_PE_REPRODUCIBILITY_GATE=REMOVED_WITH_SOURCE_IDENTITY_REPLACEMENT' | Add-Content artifact\F7_FIELD_SOURCE_IDENTITY.txt -Encoding ascii

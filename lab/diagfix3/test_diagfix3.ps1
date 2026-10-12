$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot 'set_vblank_diagnostics_diagfix3.ps1'
function Gate([bool]$ok,[string]$name){if(-not $ok){throw $name};Write-Host ($name+'=PASS')}
$base=Join-Path $env:RUNNER_TEMP ('ptar_diagfix3_'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $base | Out-Null
try {
  # 1) STATUS with no INI: default visible, no file created.
  & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $helper -GameRoot $base -Value -1 | Out-Host
  Gate ($LASTEXITCODE -eq 0) 'DIAGFIX3_STATUS_ABSENT_INI_RC'
  Gate (-not(Test-Path (Join-Path $base 'win81_nis.ini'))) 'DIAGFIX3_STATUS_ABSENT_INI_NO_CREATE'

  # 2) ON with no INI: same default, still no file.
  & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $helper -GameRoot $base -Value 1 | Out-Host
  Gate ($LASTEXITCODE -eq 0) 'DIAGFIX3_ON_ABSENT_INI_RC'
  Gate (-not(Test-Path (Join-Path $base 'win81_nis.ini'))) 'DIAGFIX3_ON_ABSENT_INI_NO_CREATE'

  # 3) OFF with no INI: create exact minimal file and verify Win32 readback.
  & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $helper -GameRoot $base -Value 0 | Out-Host
  Gate ($LASTEXITCODE -eq 0) 'DIAGFIX3_OFF_ABSENT_INI_RC'
  $ini=Join-Path $base 'win81_nis.ini'
  Gate (Test-Path $ini) 'DIAGFIX3_OFF_CREATES_INI'
  $lines=[IO.File]::ReadAllLines($ini)
  Gate ($lines.Length -eq 2 -and $lines[0] -eq '[WIN81_NIS]' -and $lines[1] -eq 'VBlankDiagnostics=0') 'DIAGFIX3_MINIMAL_INI_EXACT'

  # 4) ON updates only target key and creates backup.
  & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $helper -GameRoot $base -Value 1 | Out-Host
  Gate ($LASTEXITCODE -eq 0) 'DIAGFIX3_ON_EXISTING_RC'
  $text=[IO.File]::ReadAllText($ini)
  Gate ($text -match 'VBlankDiagnostics=1') 'DIAGFIX3_ON_EXISTING_READBACK'
  $backups=@(Get-ChildItem (Join-Path $base 'PTAR_D3D9_VERIFIER_BACKUPS') -Filter '*.ini' -ErrorAction Stop)
  Gate ($backups.Count -ge 1) 'DIAGFIX3_BACKUP_CREATED'

  # 5) Preserve unrelated settings.
  [IO.File]::WriteAllLines($ini,[string[]]@('[WIN81_NIS]','Overlay=1','VBlankDiagnostics=1','Diagnostics=1'),[Text.Encoding]::ASCII)
  & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $helper -GameRoot $base -Value 0 | Out-Host
  Gate ($LASTEXITCODE -eq 0) 'DIAGFIX3_OFF_EXISTING_RC'
  $after=[IO.File]::ReadAllText($ini)
  Gate ($after -match 'Overlay=1' -and $after -match 'Diagnostics=1' -and $after -match 'VBlankDiagnostics=0') 'DIAGFIX3_PRESERVE_UNRELATED_KEYS'

  # 6) Ambiguous duplicate keys must still be refused without altering file.
  [IO.File]::WriteAllLines($ini,[string[]]@('[WIN81_NIS]','VBlankDiagnostics=0','VBlankDiagnostics=1'),[Text.Encoding]::ASCII)
  $before=[IO.File]::ReadAllText($ini)
  & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $helper -GameRoot $base -Value 1 | Out-Host
  Gate ($LASTEXITCODE -eq 14) 'DIAGFIX3_DUPLICATE_KEYS_REJECTED'
  Gate ([IO.File]::ReadAllText($ini) -eq $before) 'DIAGFIX3_DUPLICATE_KEYS_PRESERVED'

  Write-Host 'DIAGFIX3_ALL_GATES=PASS'
}
finally { Remove-Item -LiteralPath $base -Recurse -Force -ErrorAction SilentlyContinue }

param(
  [int]$DurationSeconds=20,
  [switch]$PreflightOnly
)
$ErrorActionPreference='Stop'
$GameRoot=$null
$err=Join-Path $env:TEMP 'PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt'
$tmpDir=$null

function CleanupTemp {
  param([string]$Path)
  if($Path -and (Test-Path -LiteralPath $Path -PathType Container)){
    try{[IO.Directory]::Delete($Path,$true)}catch{}
  }
}

try{
  # Windows 8.1 / PowerShell 4 contract:
  # game root travels only through an environment variable, never a quoted
  # command-line argument ending with a backslash.
  $rawRoot=[string]$env:PTAR_GAME_ROOT
  if([string]::IsNullOrWhiteSpace($rawRoot)){throw 'PTAR_GAME_ROOT absent.'}
  $rawRoot=$rawRoot.Trim().Trim('"')
  if(-not(Test-Path -LiteralPath $rawRoot -PathType Container)){throw ('Racine jeu introuvable : '+$rawRoot)}
  $GameRoot=[IO.Path]::GetFullPath($rawRoot)
  if(-not(Test-Path -LiteralPath (Join-Path $GameRoot 'Warhammer.exe') -PathType Leaf)){
    throw ('Warhammer.exe absent de la racine detectee : '+$GameRoot)
  }
  $err=Join-Path $GameRoot 'PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt'

  $src=Join-Path $PSScriptRoot 'PTARVisiblePacingVerifier.cs'
  if(-not(Test-Path -LiteralPath $src -PathType Leaf)){throw 'PTARVisiblePacingVerifier.cs absent.'}
  $code=[IO.File]::ReadAllText($src)
  if([string]::IsNullOrWhiteSpace($code)){throw 'Source moteur VBLANK3 vide.'}

  $csc64=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
  $csc32=Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
  $csc=$null
  if(Test-Path -LiteralPath $csc64 -PathType Leaf){$csc=$csc64}
  elseif(Test-Path -LiteralPath $csc32 -PathType Leaf){$csc=$csc32}
  if(-not $csc){throw 'Compilateur .NET Framework 4 csc.exe introuvable.'}

  $tmpDir=Join-Path $env:TEMP ('PTAR_VBLANK3_'+[Guid]::NewGuid().ToString('N'))
  [IO.Directory]::CreateDirectory($tmpDir) | Out-Null
  $tmpCs=Join-Path $tmpDir 'vblank3.cs'
  $tmpExe=Join-Path $tmpDir 'vblank3.exe'
  [IO.File]::WriteAllText($tmpCs,$code)

  # Dedicated EXE: unlike the former DLL hosted by powershell.exe, the capture
  # process can make itself DPI-aware before creating any GDI capture objects.
  $compilerOutput=@(& $csc '/nologo' '/target:exe' '/optimize+' '/platform:x64' (('/out:{0}' -f $tmpExe)) $tmpCs 2>&1)
  $compilerRc=$LASTEXITCODE
  if($compilerRc -ne 0 -or -not(Test-Path -LiteralPath $tmpExe -PathType Leaf)){
    $details=(($compilerOutput | ForEach-Object { $_.ToString() }) -join ' | ')
    throw ('Compilation VBLANK3 EXE impossible (csc='+$compilerRc+'). '+$details)
  }

  # Target-host execution preflight: actually run the freshly compiled EXE.
  # This catches compile/entrypoint/DPI/GDI failures before the 20-second test.
  if($PreflightOnly){
    $self=@(& $tmpExe '--selftest' 2>&1)
    $selfRc=$LASTEXITCODE
    foreach($line in $self){Write-Host $line}
    if($selfRc -ne 0){throw ('VBLANK3 EXE self-test failed, code '+$selfRc)}
    Write-Host ('[PASS] VBLANK3 PRE-FLIGHT Windows/PS4 : root='+$GameRoot)
    Write-Host ('[PASS] CSC='+$csc)
    Write-Host '[PASS] EXE compile + entrypoint + DPI-aware + GDI self-test.'
    CleanupTemp $tmpDir
    $tmpDir=$null
    exit 0
  }

  & $tmpExe $DurationSeconds
  $rc=$LASTEXITCODE
  if($rc -ne 0){
    throw ('VBLANK3 EXE returned '+$rc+'. See PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt.')
  }

  CleanupTemp $tmpDir
  $tmpDir=$null
  exit 0
}catch{
  CleanupTemp $tmpDir
  $tmpDir=$null
  $msg=($_ | Out-String)
  try{$msg | Set-Content -LiteralPath $err -Encoding UTF8}catch{}
  Write-Host ('[FAIL] '+$_.Exception.Message) -ForegroundColor Red
  exit 31
}

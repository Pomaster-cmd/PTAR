$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force artifact | Out-Null
$b64=(Get-Content .\lab\fullquality1\qstage4diag2_exact_dxbc.b64 -Raw) -replace '\s',''
$bytes=[Convert]::FromBase64String($b64)
[IO.File]::WriteAllBytes((Join-Path $PWD 'artifact\qstage4diag2_exact.cso'),$bytes)
$sha=(Get-FileHash artifact\qstage4diag2_exact.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($sha -ne 'a69d6a66fc3c8a472c6ec72e0cfe3246e2e6b9a3c81aee1208b047850d4c51db'){throw "QSTAGE4 exact DXBC SHA mismatch: $sha"}
$fxc=Get-Command fxc.exe -ErrorAction SilentlyContinue
if(-not $fxc){
  foreach($root in @("$env:ProgramFiles(x86)\Windows Kits\10\bin","$env:ProgramFiles(x86)\Windows Kits\8.1\bin")){
    if(Test-Path $root){$fxc=Get-ChildItem $root -Recurse -Filter fxc.exe -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1;if($fxc){break}}
  }
}
if(-not $fxc){throw 'fxc.exe not found'}
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
& $fxcPath /nologo /dumpbin /Fc artifact\QSTAGE4DIAG2_DISASM.txt artifact\qstage4diag2_exact.cso
if($LASTEXITCODE){throw 'fxc dumpbin failed'}
$d=Get-Content artifact\QSTAGE4DIAG2_DISASM.txt -Raw
foreach($m in @('ps_5_0','gSource','gLinearClamp')){if($d -notmatch [regex]::Escape($m)){throw "missing disasm marker $m"}}
('QSTAGE4_DXBC_SHA256='+$sha) | Set-Content artifact\QSTAGE4_EXTRACT_STATUS.txt -Encoding ascii
'QSTAGE4_DISASSEMBLY=PASS' | Add-Content artifact\QSTAGE4_EXTRACT_STATUS.txt -Encoding ascii
Write-Host 'QSTAGE4_EXACT_DXBC_DISASSEMBLY=PASS'

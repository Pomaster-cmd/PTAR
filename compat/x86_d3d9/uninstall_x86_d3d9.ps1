param([Parameter(Mandatory=$true)][string]$GameExe)
$ErrorActionPreference='Stop'
$GameExe=[IO.Path]::GetFullPath($GameExe)
$target=Split-Path -Parent $GameExe
$state=Join-Path $target 'PTAR_X86_D3D9_INSTALL_STATE.txt'
if(-not(Test-Path -LiteralPath $state -PathType Leaf)){throw "PTAR install state missing: $state"}

$kv=@{}
Get-Content -LiteralPath $state | ForEach-Object {
    $i=$_.IndexOf('=')
    if($i -gt 0){$kv[$_.Substring(0,$i)]=$_.Substring($i+1)}
}
if($kv['SCHEMA'] -ne '1'){throw 'Unknown PTAR x86 install-state schema'}

$dll=Join-Path $target 'd3d9.dll'
if(Test-Path -LiteralPath $dll -PathType Leaf){
    $cur=(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
    if($cur -ne $kv['INSTALLED_SHA256']){throw "Current d3d9.dll differs from installed PTAR bytes; refusing deletion"}
    Remove-Item -LiteralPath $dll -Force
}

$backup=$kv['BACKUP_PATH']
if($backup -and (Test-Path -LiteralPath $backup -PathType Leaf)){
    Move-Item -LiteralPath $backup -Destination $dll -Force
}
Remove-Item -LiteralPath $state -Force
Write-Host 'PTAR_X86_D3D9_UNINSTALL=PASS'

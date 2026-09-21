param(
    [Parameter(Mandatory=$true)][string]$GameExe,
    [string]$PackageRoot
)
$ErrorActionPreference='Stop'

if([string]::IsNullOrWhiteSpace($PackageRoot)){
    $PackageRoot=$PSScriptRoot
}
$PackageRoot=[IO.Path]::GetFullPath($PackageRoot)

function Get-PeMachine([string]$Path){
    $fs=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try{
        $br=New-Object IO.BinaryReader($fs)
        if($br.ReadUInt16() -ne 0x5A4D){throw 'Not an MZ executable'}
        $fs.Position=0x3C
        $pe=$br.ReadInt32()
        $fs.Position=$pe
        if($br.ReadUInt32() -ne 0x00004550){throw 'Invalid PE signature'}
        return $br.ReadUInt16()
    } finally { $fs.Dispose() }
}

$GameExe=[IO.Path]::GetFullPath($GameExe)
if(-not(Test-Path -LiteralPath $GameExe -PathType Leaf)){throw "Game EXE not found: $GameExe"}
$machine=Get-PeMachine $GameExe
if($machine -ne 0x014c){throw ("This compatibility runtime requires x86 PE machine 0x014C; got 0x{0:X4}" -f $machine)}

$target=Split-Path -Parent $GameExe
$src=Join-Path $PackageRoot 'd3d9.dll'
if(-not(Test-Path -LiteralPath $src -PathType Leaf)){throw "Package d3d9.dll missing: $src"}

$dst=Join-Path $target 'd3d9.dll'
$backup=Join-Path $target 'd3d9.dll.ptar_original'
$srcFull=[IO.Path]::GetFullPath($src)
$dstFull=[IO.Path]::GetFullPath($dst)
$inPlace=[string]::Equals($srcFull,$dstFull,[StringComparison]::OrdinalIgnoreCase)

if(-not $inPlace){
    if(Test-Path -LiteralPath $dst -PathType Leaf){
        if(-not(Test-Path -LiteralPath $backup -PathType Leaf)){
            Copy-Item -LiteralPath $dst -Destination $backup -Force
        } else {
            throw "Existing d3d9.dll and backup already present. Refusing destructive overwrite."
        }
    }
    Copy-Item -LiteralPath $src -Destination $dst -Force
} else {
    Write-Host "PTAR_X86_D3D9_INPLACE_PACKAGE=YES"
}

$hash=(Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash.ToLowerInvariant()
$state=Join-Path $target 'PTAR_X86_D3D9_INSTALL_STATE.txt'
$backupPath=''
if((-not $inPlace) -and (Test-Path -LiteralPath $backup)){
    $backupPath=$backup
}
$inPlaceValue='0'
if($inPlace){
    $inPlaceValue='1'
}
@(
    "SCHEMA=1"
    "GAME_EXE=$GameExe"
    "TARGET_DIR=$target"
    "INSTALLED_SHA256=$hash"
    "BACKUP_PATH=$backupPath"
    "INPLACE_PACKAGE=$inPlaceValue"
    "VARIANT=PTAR_X86_D3D9_SPATIAL1_VTABLEFIX2_CRASHLOG1"
) | Set-Content -LiteralPath $state -Encoding ASCII

Write-Host "PTAR_X86_D3D9_INSTALL=PASS"
Write-Host "GAME_EXE=$GameExe"
Write-Host "DLL_SHA256=$hash"
Write-Host "LOG_PATH=$(Join-Path $target 'PTAR_X86_D3D9.log')"

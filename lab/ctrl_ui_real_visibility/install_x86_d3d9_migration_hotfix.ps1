param(
    [Parameter(Mandatory=$true)][string]$GameExe,
    [string]$PackageRoot
)
$ErrorActionPreference='Stop'

if([string]::IsNullOrWhiteSpace($PackageRoot)){$PackageRoot=$PSScriptRoot}
$PackageRoot=[IO.Path]::GetFullPath($PackageRoot)
$ExpectedRuntime='2fb071918d75527d94597d03ffc116e4d91b904d9e5efe55ba020f0c127b3bfc'
$TrustedPreviousRuntimes=@(
    'c4a0668b313a2d6ca31c165eb2e5b216559bc3de8c5a1fe60b4aa9ac57c4a16d',
    '4fc269367cf519e512fb892bbc40479a55ecae6c223167e142c8e96fcc966b8d'
)

function Get-PeMachine([string]$Path){
    $fs=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try{
        $br=New-Object IO.BinaryReader($fs)
        if($br.ReadUInt16() -ne 0x5A4D){throw 'Not an MZ executable'}
        $fs.Position=0x3C;$pe=$br.ReadInt32();$fs.Position=$pe
        if($br.ReadUInt32() -ne 0x00004550){throw 'Invalid PE signature'}
        return $br.ReadUInt16()
    } finally { $fs.Dispose() }
}
function Sha([string]$p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}

$GameExe=[IO.Path]::GetFullPath($GameExe)
if(-not(Test-Path -LiteralPath $GameExe -PathType Leaf)){throw "Game EXE not found: $GameExe"}
$machine=Get-PeMachine $GameExe
if($machine -ne 0x014c){throw ("This compatibility runtime requires x86 PE machine 0x014C; got 0x{0:X4}" -f $machine)}

$target=Split-Path -Parent $GameExe
$src=Join-Path $PackageRoot 'd3d9.dll'
if(-not(Test-Path -LiteralPath $src -PathType Leaf)){throw "Package d3d9.dll missing: $src"}
$srcHash=Sha $src
if($srcHash -ne $ExpectedRuntime){throw "Package d3d9.dll hash mismatch: $srcHash"}

$dst=Join-Path $target 'd3d9.dll'
$backup=Join-Path $target 'd3d9.dll.ptar_original'
$srcFull=[IO.Path]::GetFullPath($src);$dstFull=[IO.Path]::GetFullPath($dst)
$inPlace=[string]::Equals($srcFull,$dstFull,[StringComparison]::OrdinalIgnoreCase)
$previousPtarHash=''
$previousPtarBackup=''

if(-not $inPlace){
    if(Test-Path -LiteralPath $dst -PathType Leaf){
        $cur=Sha $dst
        if($cur -eq $ExpectedRuntime){
            Write-Host 'PTAR_X86_D3D9_REINSTALL=YES'
        } elseif($TrustedPreviousRuntimes -contains $cur){
            # Safe migration from a previously validated PTAR D3D9 runtime.
            # Never overwrite d3d9.dll.ptar_original: it belongs to the pre-PTAR game state.
            $previousPtarHash=$cur
            Write-Host 'PTAR_X86_D3D9_UPGRADE=YES'
            Write-Host ('PTAR_X86_D3D9_PREVIOUS_SHA256='+$cur)
            if(Test-Path -LiteralPath $backup -PathType Leaf){
                Write-Host ('PTAR_X86_D3D9_ORIGINAL_BACKUP=PRESERVED '+$backup)
            } else {
                $previousPtarBackup=Join-Path $target ('d3d9.dll.ptar_previous_'+$cur.Substring(0,12))
                if(Test-Path -LiteralPath $previousPtarBackup -PathType Leaf){
                    if((Sha $previousPtarBackup) -ne $cur){
                        $previousPtarBackup=Join-Path $target ('d3d9.dll.ptar_previous_'+$cur.Substring(0,12)+'_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
                    }
                }
                if(-not(Test-Path -LiteralPath $previousPtarBackup -PathType Leaf)){
                    Copy-Item -LiteralPath $dst -Destination $previousPtarBackup
                    if((Sha $previousPtarBackup) -ne $cur){throw 'Previous PTAR d3d9.dll backup hash mismatch'}
                }
                Write-Host ('PTAR_X86_D3D9_PREVIOUS_RUNTIME_BACKUP='+$previousPtarBackup)
            }
        } elseif(-not(Test-Path -LiteralPath $backup -PathType Leaf)){
            Copy-Item -LiteralPath $dst -Destination $backup -Force
            if((Sha $backup) -ne $cur){throw 'Original d3d9.dll backup hash mismatch'}
        } else {
            throw "Existing unknown d3d9.dll and backup already present. Refusing destructive overwrite."
        }
    }
    Copy-Item -LiteralPath $src -Destination $dst -Force
} else {
    Write-Host "PTAR_X86_D3D9_INPLACE_PACKAGE=YES"
}

$hash=Sha $dst
if($hash -ne $ExpectedRuntime){throw "Installed d3d9.dll hash mismatch: $hash"}
$state=Join-Path $target 'PTAR_X86_D3D9_INSTALL_STATE.txt'
$backupPath=''
if((-not $inPlace) -and (Test-Path -LiteralPath $backup -PathType Leaf)){$backupPath=$backup}
$inPlaceValue=if($inPlace){'1'}else{'0'}
@(
    "SCHEMA=2"
    "GAME_EXE=$GameExe"
    "TARGET_DIR=$target"
    "INSTALLED_SHA256=$hash"
    "EXPECTED_SHA256=$ExpectedRuntime"
    "BACKUP_PATH=$backupPath"
    "PREVIOUS_PTAR_SHA256=$previousPtarHash"
    "PREVIOUS_PTAR_BACKUP=$previousPtarBackup"
    "INPLACE_PACKAGE=$inPlaceValue"
    "VARIANT=D3D9_LEGACY_REPAIR_V1_PLUS_DIAG_HUD_BRIDGE"
    "ARCH=REGULAR_D3D9_SAME_DEVICE_SAFE_SYNC"
) | Set-Content -LiteralPath $state -Encoding ASCII

[IO.File]::WriteAllText((Join-Path $PackageRoot 'PTAR_X86_D3D9_LAST_TARGET.txt'),$GameExe+[Environment]::NewLine,[Text.Encoding]::Default)

Write-Host "PTAR_X86_D3D9_INSTALL=PASS"
Write-Host "GAME_EXE=$GameExe"
Write-Host "DLL_SHA256=$hash"
Write-Host "LOG_PATH=$(Join-Path $target 'PTAR_X86_D3D9.log')"

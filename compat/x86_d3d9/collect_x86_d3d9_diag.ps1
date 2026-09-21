param([string]$GameExe)

$ErrorActionPreference='Stop'
$Here=$PSScriptRoot

function Get-PeMachine([string]$Path){
    $fs=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try{
        $br=New-Object IO.BinaryReader($fs)
        if($br.ReadUInt16() -ne 0x5A4D){return 'NOT_MZ'}
        $fs.Position=0x3C
        $pe=$br.ReadInt32()
        $fs.Position=$pe
        if($br.ReadUInt32() -ne 0x00004550){return 'BAD_PE'}
        return ('0x{0:X4}' -f $br.ReadUInt16())
    } finally { $fs.Dispose() }
}

if([string]::IsNullOrWhiteSpace($GameExe)){
    $localState=Join-Path $Here 'PTAR_X86_D3D9_INSTALL_STATE.txt'
    if(Test-Path -LiteralPath $localState -PathType Leaf){
        foreach($line in Get-Content -LiteralPath $localState){
            if($line -like 'GAME_EXE=*'){
                $candidate=$line.Substring('GAME_EXE='.Length)
                if(Test-Path -LiteralPath $candidate -PathType Leaf){
                    $GameExe=$candidate
                    break
                }
            }
        }
    }
}
if($GameExe){
    $GameExe=[IO.Path]::GetFullPath($GameExe)
}
$Target=if($GameExe -and (Test-Path -LiteralPath $GameExe -PathType Leaf)){
    Split-Path -Parent $GameExe
}else{
    $Here
}

$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$work=Join-Path $env:TEMP ("PTAR_X86_D3D9_DIAG_"+$stamp)
if(Test-Path -LiteralPath $work){Remove-Item -LiteralPath $work -Recurse -Force}
New-Item -ItemType Directory -Path $work | Out-Null

$summary=New-Object Collections.Generic.List[string]
$summary.Add('PTAR X86/D3D9 CRASH DIAGNOSTICS')
$summary.Add('TIMESTAMP='+(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff K'))
$summary.Add('PS_VERSION='+$PSVersionTable.PSVersion.ToString())
$summary.Add('OS_VERSION='+[Environment]::OSVersion.VersionString)
$summary.Add('PACKAGE_DIR='+$Here)
$summary.Add('TARGET_DIR='+$Target)

if($GameExe -and (Test-Path -LiteralPath $GameExe -PathType Leaf)){
    $summary.Add('GAME_EXE='+$GameExe)
    $summary.Add('GAME_PE_MACHINE='+(Get-PeMachine $GameExe))
    $summary.Add('GAME_SIZE='+((Get-Item -LiteralPath $GameExe).Length))
    try{$summary.Add('GAME_SHA256='+((Get-FileHash -LiteralPath $GameExe -Algorithm SHA256).Hash.ToLowerInvariant()))}catch{$summary.Add('GAME_SHA256_ERROR='+$_.Exception.Message)}
    try{$summary.Add('GAME_FILE_VERSION='+([Diagnostics.FileVersionInfo]::GetVersionInfo($GameExe).FileVersion))}catch{}
}else{
    $summary.Add('GAME_EXE=NOT_FOUND_BY_COLLECTOR')
}

$dll=Join-Path $Target 'd3d9.dll'
if(Test-Path -LiteralPath $dll -PathType Leaf){
    $summary.Add('D3D9_DLL='+$dll)
    $summary.Add('D3D9_PE_MACHINE='+(Get-PeMachine $dll))
    $summary.Add('D3D9_SIZE='+((Get-Item -LiteralPath $dll).Length))
    try{$summary.Add('D3D9_SHA256='+((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()))}catch{$summary.Add('D3D9_SHA256_ERROR='+$_.Exception.Message)}
}

$copyNames=@(
    'PTAR_X86_D3D9.log',
    'PTAR_X86_D3D9_INSTALL_STATE.txt'
)
foreach($name in $copyNames){
    $p=Join-Path $Target $name
    if(Test-Path -LiteralPath $p -PathType Leaf){
        Copy-Item -LiteralPath $p -Destination (Join-Path $work $name) -Force
        $summary.Add($name+'=COLLECTED')
    }else{
        $summary.Add($name+'=MISSING')
    }
}

try{
    $gpu=Get-WmiObject Win32_VideoController -ErrorAction Stop |
        Select-Object Name,AdapterCompatibility,DriverVersion,VideoProcessor,AdapterRAM,PNPDeviceID
    $gpu | Format-List | Out-File -LiteralPath (Join-Path $work 'GPU.txt') -Encoding UTF8 -Width 4096
    $summary.Add('GPU_INFO=COLLECTED')
}catch{
    $summary.Add('GPU_INFO_ERROR='+$_.Exception.Message)
}

try{
    $os=Get-WmiObject Win32_OperatingSystem -ErrorAction Stop |
        Select-Object Caption,Version,BuildNumber,OSArchitecture,ServicePackMajorVersion
    $os | Format-List | Out-File -LiteralPath (Join-Path $work 'OS.txt') -Encoding UTF8 -Width 4096
}catch{}

try{
    $start=(Get-Date).AddHours(-4)
    $exeName=''
    if($GameExe){$exeName=[IO.Path]::GetFileName($GameExe)}
    $exePattern=if($exeName){[regex]::Escape($exeName)}else{'$a'}
    $events=Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=$start} -ErrorAction Stop |
        Where-Object {
            $_.ProviderName -in @('Application Error','Windows Error Reporting') -or
            ($_.Message -match $exePattern) -or
            ($_.Message -match 'd3d9\.dll')
        } |
        Select-Object -First 80 TimeCreated,Id,LevelDisplayName,ProviderName,Message
    if($events){
        $events | Format-List | Out-File -LiteralPath (Join-Path $work 'WINDOWS_APPLICATION_EVENTS.txt') -Encoding UTF8 -Width 4096
        $summary.Add('WINDOWS_EVENTS=COLLECTED')
    }else{
        $summary.Add('WINDOWS_EVENTS=NONE_MATCHING_LAST_4H')
    }
}catch{
    $summary.Add('WINDOWS_EVENTS_ERROR='+$_.Exception.Message)
}

try{
    $werRoots=@(
        (Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\WER\ReportArchive'),
        (Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\WER\ReportQueue')
    )
    $werOut=Join-Path $work 'WER'
    New-Item -ItemType Directory -Path $werOut | Out-Null
    $copied=0
    foreach($root in $werRoots){
        if(Test-Path -LiteralPath $root){
            $baseName=''
            if($GameExe){$baseName=[IO.Path]::GetFileNameWithoutExtension($GameExe)}
            $werPattern=if($baseName){[regex]::Escape($baseName)+'|APPCRASH'}else{'APPCRASH'}
            Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.LastWriteTime -gt (Get-Date).AddHours(-4) -and $_.Name -match $werPattern } |
                ForEach-Object {
                    $wer=Join-Path $_.FullName 'Report.wer'
                    if(Test-Path -LiteralPath $wer -PathType Leaf){
                        $dest=Join-Path $werOut (('WER_'+$copied+'_'+$_.Name+'.txt') -replace '[^\w\.-]','_')
                        Copy-Item -LiteralPath $wer -Destination $dest -Force
                        $copied++
                    }
                }
        }
    }
    $summary.Add('WER_REPORTS_COLLECTED='+$copied)
}catch{
    $summary.Add('WER_ERROR='+$_.Exception.Message)
}

$summary | Set-Content -LiteralPath (Join-Path $work 'SUMMARY.txt') -Encoding UTF8

$outDir=$Here
try{
    $probe=Join-Path $outDir ('.ptar_write_probe_'+[Guid]::NewGuid().ToString('N'))
    [IO.File]::WriteAllText($probe,'x')
    Remove-Item -LiteralPath $probe -Force
}catch{
    $outDir=[Environment]::GetFolderPath('Desktop')
}
$zip=Join-Path $outDir ("PTAR_X86_D3D9_DIAG_"+$stamp+".zip")
if(Test-Path -LiteralPath $zip){Remove-Item -LiteralPath $zip -Force}

Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory(
    $work,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)

Remove-Item -LiteralPath $work -Recurse -Force
Write-Host 'PTAR_X86_D3D9_DIAG_COLLECT=PASS'
Write-Host "OUTPUT=$zip"

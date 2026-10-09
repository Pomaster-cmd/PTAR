param([string]$GameExe='')
$ErrorActionPreference='Stop'

$Root=Split-Path -Parent $PSScriptRoot
$D11=Join-Path $Root 'BACKENDS\D3D11_X64'
$D9=Join-Path $Root 'BACKENDS\D3D9_X86'
$PsExe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$Log=Join-Path $PSScriptRoot 'PTAR_COMPLETE_INSTALL_LAST.log'
$ExpectedD9='c4a0668b313a2d6ca31c165eb2e5b216559bc3de8c5a1fe60b4aa9ac57c4a16d'

function L([string]$s){
    $x='['+(Get-Date -Format 'HH:mm:ss')+'] '+$s
    Write-Host $x
    Add-Content -LiteralPath $Log -Value $x -Encoding UTF8
}
function F([string]$s,[int]$c=90){L ('FAIL: '+$s);exit $c}
function Full([string]$p){return [IO.Path]::GetFullPath($p.Trim().Trim('"'))}
function Sha([string]$p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}

function Get-PeMachine([string]$p){
    try{
        $fs=[IO.File]::Open($p,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        try{
            if($fs.Length -lt 256){return 0}
            $br=New-Object IO.BinaryReader($fs)
            if($br.ReadUInt16() -ne 0x5A4D){return 0}
            $fs.Position=0x3C
            $pe=$br.ReadInt32()
            if($pe -lt 0 -or $pe+6 -gt $fs.Length){return 0}
            $fs.Position=$pe
            if($br.ReadUInt32() -ne 0x00004550){return 0}
            return $br.ReadUInt16()
        } finally {$fs.Dispose()}
    } catch {return 0}
}

function Get-BackendLabel([int]$Machine){
    if($Machine -eq 0x8664){return 'D3D11 x64'}
    if($Machine -eq 0x014c){return 'D3D9 x86'}
    return 'UNSUPPORTED'
}

function Is-PackageHelper([IO.FileInfo]$f){
    $n=$f.Name.ToLowerInvariant()
    if($n -match '^(b18|ffmpeg|ptar|qsv|dxdiag|unins|setup|install|verify|collect|rollback|selftest)'){return $true}
    return $false
}

function Is-PackageOwnedDirectory([string]$Path){
    $p=(Full $Path).TrimEnd('\')+'\'
    foreach($rel in @('BACKENDS','diag','_PTAR_UNINSTALL')){
        $owned=(Full (Join-Path $Root $rel)).TrimEnd('\')+'\'
        if($p.StartsWith($owned,[StringComparison]::OrdinalIgnoreCase)){return $true}
    }
    return $false
}

function Get-SearchDirectories{
    $out=New-Object Collections.Generic.List[string]
    $seen=@{}
    function Add-Dir([string]$d){
        if([string]::IsNullOrWhiteSpace($d)){return}
        try{$p=Full $d}catch{return}
        if(-not(Test-Path -LiteralPath $p -PathType Container)){return}
        if(Is-PackageOwnedDirectory $p){return}
        $k=$p.TrimEnd('\').ToLowerInvariant()
        if(-not $seen.ContainsKey($k)){$seen[$k]=$true;$out.Add($p)}
    }

    $parent=Split-Path -Parent $Root
    Add-Dir $Root
    if($parent -and ($parent -ne $Root)){Add-Dir $parent}

    $common=@(
        'Binaries','Binaries\Win64','Binaries\Win32',
        'bin','bin\x64','bin\x86','Bin64','Bin32',
        'Win64','Win32','x64','x86','System','System32'
    )
    foreach($base in @($Root,$parent)){
        if([string]::IsNullOrWhiteSpace($base)){continue}
        foreach($rel in $common){Add-Dir (Join-Path $base $rel)}
    }

    if(Test-Path -LiteralPath $Root -PathType Container){
        foreach($d in @(Get-ChildItem -LiteralPath $Root -Directory -ErrorAction SilentlyContinue)){
            if(($d.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){continue}
            Add-Dir $d.FullName
        }
    }

    return @($out.ToArray())
}

function Get-InstallCandidates{
    $seen=@{}
    $out=New-Object Collections.Generic.List[object]
    $parent=Split-Path -Parent $Root

    foreach($d in @(Get-SearchDirectories)){
        foreach($f in @(Get-ChildItem -LiteralPath $d -Filter '*.exe' -File -ErrorAction SilentlyContinue)){
            if(Is-PackageHelper $f){continue}
            $m=Get-PeMachine $f.FullName
            if($m -ne 0x8664 -and $m -ne 0x014c){continue}
            $k=$f.FullName.ToLowerInvariant()
            if($seen.ContainsKey($k)){continue}
            $seen[$k]=$true

            $score=20
            $fd=(Split-Path -Parent $f.FullName).TrimEnd('\')
            if($fd -ieq $Root.TrimEnd('\')){$score=0}
            elseif($parent -and $fd -ieq $parent.TrimEnd('\')){$score=1}
            elseif($fd -match '\\Binaries\\Win(64|32)$'){$score=2}
            elseif($fd -match '\\(Win64|Win32|Bin64|Bin32|x64|x86|System|System32)$'){$score=3}
            elseif($fd -match '\\bin$'){$score=4}
            if($f.Name -match '(?i)(launcher|crash|report|config|updater|update)'){$score+=20}

            $out.Add([pscustomobject]@{
                Path=$f.FullName
                Machine=$m
                Backend=(Get-BackendLabel $m)
                Score=$score
            })
        }
    }

    return @($out.ToArray() | Sort-Object Score,Path)
}

function Resolve-RequestedExe([string]$raw){
    if([string]::IsNullOrWhiteSpace($raw)){return $null}
    $raw=$raw.Trim().Trim('"')
    if(-not [IO.Path]::IsPathRooted($raw)){$raw=Join-Path $Root $raw}
    $p=Full $raw
    if(-not(Test-Path -LiteralPath $p -PathType Leaf)){F ('Executable cible introuvable : '+$p) 2}
    if([IO.Path]::GetExtension($p) -ine '.exe'){F ('La cible doit etre un .exe : '+$p) 2}
    $m=Get-PeMachine $p
    if($m -ne 0x8664 -and $m -ne 0x014c){F ('Executable PE non supporte : '+$p+' PE_MACHINE=0x'+$m.ToString('X4')) 5}
    return $p
}

function Select-TargetExe{
    $p=Resolve-RequestedExe $GameExe
    if($p){return $p}

    $p=Resolve-RequestedExe ([string]$env:PTAR_GAME_EXE)
    if($p){L ('Cible fournie par PTAR_GAME_EXE : '+$p);return $p}

    foreach($hint in @(
        (Join-Path $Root 'PTAR_TARGET_EXE.txt'),
        (Join-Path $D11 'PTAR_TARGET_EXE.txt'),
        (Join-Path $D9 'PTAR_TARGET_EXE.txt')
    )){
        if(Test-Path -LiteralPath $hint -PathType Leaf){
            $v=(Get-Content -LiteralPath $hint -TotalCount 1).Trim()
            if(-not [string]::IsNullOrWhiteSpace($v)){
                $p=Resolve-RequestedExe $v
                if($p){L ('Cible fournie par '+$hint+' : '+$p);return $p}
            }
        }
    }

    $c=@(Get-InstallCandidates)
    if($c.Count -eq 1){
        L ('Cible detectee automatiquement ['+$c[0].Backend+'] : '+$c[0].Path)
        return $c[0].Path
    }

    if($c.Count -gt 1){
        Write-Host ''
        Write-Host 'Executables candidats detectes :'
        for($i=0;$i -lt $c.Count;$i++){
            Write-Host ('  ['+($i+1)+'] ['+$c[$i].Backend+'] '+$c[$i].Path)
        }
        while($true){
            $r=Read-Host 'Numero de l executable cible, ou chemin complet'
            $num=0
            if([int]::TryParse($r,[ref]$num) -and $num -ge 1 -and $num -le $c.Count){return $c[$num-1].Path}
            $p=Resolve-RequestedExe $r
            if($p){return $p}
        }
    }

    Write-Host ''
    Write-Host 'Aucun executable candidat x64/D3D11 ou x86/D3D9 detecte a proximite du pack.'
    Write-Host 'Indique le chemin complet de l executable de rendu du jeu.'
    while($true){
        $r=Read-Host 'Executable cible'
        $p=Resolve-RequestedExe $r
        if($p){return $p}
    }
}

function Normalize-D11-Handoff([string]$Target){
    $enc=[Text.Encoding]::Default
    $gameRoot=Split-Path -Parent $Target
    [IO.File]::WriteAllText((Join-Path $D11 'win81_nis_install_target.txt'),$gameRoot+[Environment]::NewLine,$enc)
    [IO.File]::WriteAllText((Join-Path $D11 'win81_nis_install_exe.txt'),$Target+[Environment]::NewLine,$enc)
}

# PTAR_INSTALLER_EXECUTION_START
Set-Content -LiteralPath $Log -Value ('START '+(Get-Date).ToString('o')) -Encoding UTF8
L 'Recherche des executables candidats PTAR...'
$Target=Select-TargetExe
$Machine=Get-PeMachine $Target
L ('TARGET='+$Target)
L ('PE_MACHINE=0x'+$Machine.ToString('X4'))

if($Machine -eq 0x8664){
    $Installer=Join-Path $D11 'diag\install.ps1'
    if(-not(Test-Path -LiteralPath $Installer -PathType Leaf)){F ('Installateur D3D11 absent : '+$Installer) 10}
    L 'BACKEND=D3D11_X64_HWTEST20_HQ900_MAILBOXCFLUSH1'
    & $PsExe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $Installer -GameExe $Target
    $rc=$LASTEXITCODE
    if($rc -ne 0){F ('Installation D3D11 interrompue, code '+$rc) $rc}
    Normalize-D11-Handoff $Target
    $Verify=Join-Path $D11 'diag\verify.ps1'
    & $PsExe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $Verify
    $rc=$LASTEXITCODE
    if($rc -ne 0){F ('Verification D3D11 en echec, code '+$rc) $rc}
    L 'INSTALL_D3D11_X64=PASS'
    exit 0
}

if($Machine -eq 0x014c){
    $Installer=Join-Path $D9 'install_x86_d3d9.ps1'
    if(-not(Test-Path -LiteralPath $Installer -PathType Leaf)){F ('Installateur D3D9 absent : '+$Installer) 10}
    L 'BACKEND=D3D9_X86_LEGACY_REPAIR_V1'
    & $PsExe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $Installer -GameExe $Target -PackageRoot $D9
    $rc=$LASTEXITCODE
    if($rc -ne 0){F ('Installation D3D9 interrompue, code '+$rc) $rc}
    $GameRoot=Split-Path -Parent $Target
    $Live=Join-Path $GameRoot 'd3d9.dll'
    if((Sha $Live) -ne $ExpectedD9){F 'Verification D3D9 post-install : hash runtime incorrect' 31}
    [IO.File]::WriteAllText((Join-Path $D9 'PTAR_X86_D3D9_LAST_TARGET.txt'),$Target+[Environment]::NewLine,[Text.Encoding]::UTF8)
    L 'INSTALL_D3D9_X86=PASS'
    exit 0
}

F ('Aucun backend PTAR pour PE_MACHINE=0x'+$Machine.ToString('X4')+'. Support actuel: x64/D3D11 et x86/D3D9.') 5

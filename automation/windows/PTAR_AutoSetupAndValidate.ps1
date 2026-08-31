param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectRoot = "",
    [switch]$VerifyOnly,
    [switch]$Elevated
)

$ErrorActionPreference = "Stop"

function Write-Stage([string]$Message) {
    Write-Host ""
    Write-Host ("=== " + $Message + " ===") -ForegroundColor Cyan
}

function Write-Info([string]$Message) {
    Write-Host ("[INFO] " + $Message)
}

function Write-Ok([string]$Message) {
    Write-Host ("[PASS] " + $Message) -ForegroundColor Green
}

function Write-Warn([string]$Message) {
    Write-Host ("[WARN] " + $Message) -ForegroundColor Yellow
}

function Write-Fail([string]$Message) {
    Write-Host ("[FAIL] " + $Message) -ForegroundColor Red
}

function Test-IsAdministrator {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-Sha256([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $stream = [IO.File]::OpenRead($Path)
        try {
            $hash = $sha.ComputeHash($stream)
        } finally {
            $stream.Dispose()
        }
    } finally {
        $sha.Dispose()
    }
    return ([BitConverter]::ToString($hash)).Replace("-", "").ToLowerInvariant()
}

function Assert-MicrosoftSignature([string]$Path) {
    $sig = Get-AuthenticodeSignature -FilePath $Path
    if ($sig.Status -ne "Valid") {
        throw ("Authenticode signature is not valid: " + $Path + " status=" + $sig.Status)
    }
    if ($sig.SignerCertificate -eq $null) {
        throw ("No signer certificate: " + $Path)
    }
    $subject = $sig.SignerCertificate.Subject
    if ($subject -notmatch "Microsoft") {
        throw ("Unexpected signer. Expected Microsoft. Subject=" + $subject)
    }
    Write-Ok ("Microsoft signature verified: " + [IO.Path]::GetFileName($Path))
}

function Get-DotNetFramework4Release {
    try {
        $key = Get-ItemProperty -LiteralPath "HKLM:\SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full" -ErrorAction Stop
        if ($key.Release -ne $null) {
            return [int]$key.Release
        }
    } catch {}
    return 0
}

function Save-TlsRegistryState([string]$Path) {
    $rows = @()
    $targets = @(
        @{ Path="HKLM:\SOFTWARE\Microsoft\.NETFramework\v4.0.30319"; Names=@("SchUseStrongCrypto","SystemDefaultTlsVersions") },
        @{ Path="HKLM:\SOFTWARE\Wow6432Node\Microsoft\.NETFramework\v4.0.30319"; Names=@("SchUseStrongCrypto","SystemDefaultTlsVersions") },
        @{ Path="HKLM:\SYSTEM\CurrentControlSet\Control\SecurityProviders\SCHANNEL\Protocols\TLS 1.2\Client"; Names=@("Enabled","DisabledByDefault") }
    )
    foreach ($target in $targets) {
        foreach ($name in $target.Names) {
            $exists = $false
            $value = ""
            try {
                $item = Get-ItemProperty -LiteralPath $target.Path -Name $name -ErrorAction Stop
                $value = $item.$name
                $exists = $true
            } catch {}
            $rows += New-Object PSObject -Property @{
                Path=$target.Path
                Name=$name
                Existed=$exists
                Value=$value
            }
        }
    }
    $rows | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Enable-PtarTls12Compatibility([string]$BackupPath) {
    Write-Stage "TLS 1.2 compatibility repair"
    Write-Warn "All non-invasive download methods failed."
    Write-Warn "PTAR will enable Microsoft's documented TLS 1.2/.NET strong-crypto settings."
    Write-Warn "Existing registry state is recorded first; no value is silently removed."

    Save-TlsRegistryState $BackupPath

    $dotnetPaths = @(
        "HKLM:\SOFTWARE\Microsoft\.NETFramework\v4.0.30319",
        "HKLM:\SOFTWARE\Wow6432Node\Microsoft\.NETFramework\v4.0.30319"
    )
    foreach ($path in $dotnetPaths) {
        if (-not (Test-Path -LiteralPath $path)) {
            New-Item -Path $path -Force | Out-Null
        }
        New-ItemProperty -LiteralPath $path -Name "SchUseStrongCrypto" `
            -PropertyType DWord -Value 1 -Force | Out-Null
        New-ItemProperty -LiteralPath $path -Name "SystemDefaultTlsVersions" `
            -PropertyType DWord -Value 1 -Force | Out-Null
    }

    $schannel = "HKLM:\SYSTEM\CurrentControlSet\Control\SecurityProviders\SCHANNEL\Protocols\TLS 1.2\Client"
    if (-not (Test-Path -LiteralPath $schannel)) {
        New-Item -Path $schannel -Force | Out-Null
    }
    New-ItemProperty -LiteralPath $schannel -Name "Enabled" `
        -PropertyType DWord -Value 1 -Force | Out-Null
    New-ItemProperty -LiteralPath $schannel -Name "DisabledByDefault" `
        -PropertyType DWord -Value 0 -Force | Out-Null

    $script:RebootRecommended = $true
    Write-Ok ("TLS compatibility values written. Previous state: " + $BackupPath)
    Write-Warn "Microsoft documents that strong-crypto changes can require a restart."
}

function Test-DownloadedMicrosoftFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }
    $size = (Get-Item -LiteralPath $Path).Length
    if ($size -lt 100000) {
        Write-Warn ("Candidate download is too small: " + $Path + " bytes=" + $size)
        return $false
    }
    try {
        Assert-MicrosoftSignature $Path
        return $true
    }
    catch {
        Write-Warn ("Candidate rejected: " + $_.Exception.Message)
        return $false
    }
}

function Try-DownloadWithBits([string]$Url,[string]$Candidate) {
    Write-Info "Download engine: BITS PowerShell"
    try {
        Import-Module BitsTransfer -ErrorAction Stop
        Start-BitsTransfer -Source $Url -Destination $Candidate `
            -DisplayName "PTAR Microsoft prerequisite" `
            -Description "PTAR Windows 8.1 prerequisite download" `
            -ErrorAction Stop
        return (Test-DownloadedMicrosoftFile $Candidate)
    }
    catch {
        Write-Warn ("BITS PowerShell failed: " + $_.Exception.Message)
        return $false
    }
}

function Try-DownloadWithBitsAdmin([string]$Url,[string]$Candidate) {
    Write-Info "Download engine: bitsadmin.exe"
    $bitsadmin = Join-Path $env:SystemRoot "System32\bitsadmin.exe"
    if (-not (Test-Path -LiteralPath $bitsadmin)) {
        Write-Warn "bitsadmin.exe not present."
        return $false
    }
    try {
        $job = "PTAR_" + [Guid]::NewGuid().ToString("N")
        & $bitsadmin /transfer $job /download /priority foreground $Url $Candidate | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Warn ("bitsadmin exit code: " + $LASTEXITCODE)
            return $false
        }
        return (Test-DownloadedMicrosoftFile $Candidate)
    }
    catch {
        Write-Warn ("bitsadmin failed: " + $_.Exception.Message)
        return $false
    }
}

function Try-DownloadWithWebClient([string]$Url,[string]$Candidate) {
    Write-Info "Download engine: .NET WebClient / TLS 1.2"
    try {
        try {
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        } catch {
            [Net.ServicePointManager]::SecurityProtocol = 3072
        }

        $wc = New-Object Net.WebClient
        try {
            $wc.Headers.Add("User-Agent", "PTAR-Win81-Automation/1.2")
            $wc.DownloadFile($Url, $Candidate)
        } finally {
            $wc.Dispose()
        }
        return (Test-DownloadedMicrosoftFile $Candidate)
    }
    catch {
        Write-Warn ("WebClient failed: " + $_.Exception.Message)
        return $false
    }
}

function Try-DownloadWithCertUtil([string]$Url,[string]$Candidate) {
    Write-Info "Download engine: certutil.exe URL cache"
    $certutil = Join-Path $env:SystemRoot "System32\certutil.exe"
    if (-not (Test-Path -LiteralPath $certutil)) {
        Write-Warn "certutil.exe not present."
        return $false
    }
    try {
        & $certutil -urlcache -split -f $Url $Candidate | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Warn ("certutil exit code: " + $LASTEXITCODE)
            return $false
        }
        return (Test-DownloadedMicrosoftFile $Candidate)
    }
    catch {
        Write-Warn ("certutil failed: " + $_.Exception.Message)
        return $false
    }
}

function Download-OfficialMicrosoftFile(
    [string]$Url,
    [string]$Destination
) {
    if (Test-Path -LiteralPath $Destination) {
        # Keep the historical log wording for regression compatibility.
        Write-Info ("Existing download preserved: " + $Destination)
        Assert-MicrosoftSignature $Destination
        return
    }

    Write-Info "Downloading from official Microsoft endpoint:"
    Write-Info ("  " + $Url)
    Write-Info ("Installed .NET Framework v4 release key: " + (Get-DotNetFramework4Release))

    $dir = Split-Path -Parent $Destination
    $base = [IO.Path]::GetFileNameWithoutExtension($Destination)
    $ext = [IO.Path]::GetExtension($Destination)

    $candidates = @(
        @{ Name="bits";      Path=(Join-Path $dir ($base + ".bits" + $ext)) },
        @{ Name="bitsadmin"; Path=(Join-Path $dir ($base + ".bitsadmin" + $ext)) },
        @{ Name="webclient"; Path=(Join-Path $dir ($base + ".webclient" + $ext)) },
        @{ Name="certutil";  Path=(Join-Path $dir ($base + ".certutil" + $ext)) }
    )

    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c.Path) {
            Write-Info ("Preserved candidate exists: " + $c.Path)
            if (Test-DownloadedMicrosoftFile $c.Path) {
                Copy-Item -LiteralPath $c.Path -Destination $Destination
                Assert-MicrosoftSignature $Destination
                Write-Ok ("Reused validated candidate: " + $c.Name)
                return
            }
        }
    }

    if (-not (Test-Path -LiteralPath $candidates[0].Path)) {
        if (Try-DownloadWithBits $Url $candidates[0].Path) {
            Copy-Item -LiteralPath $candidates[0].Path -Destination $Destination
            Assert-MicrosoftSignature $Destination
            Write-Ok "Download succeeded via BITS."
            return
        }
    }

    if (-not (Test-Path -LiteralPath $candidates[1].Path)) {
        if (Try-DownloadWithBitsAdmin $Url $candidates[1].Path) {
            Copy-Item -LiteralPath $candidates[1].Path -Destination $Destination
            Assert-MicrosoftSignature $Destination
            Write-Ok "Download succeeded via bitsadmin."
            return
        }
    }

    if (-not (Test-Path -LiteralPath $candidates[2].Path)) {
        if (Try-DownloadWithWebClient $Url $candidates[2].Path) {
            Copy-Item -LiteralPath $candidates[2].Path -Destination $Destination
            Assert-MicrosoftSignature $Destination
            Write-Ok "Download succeeded via WebClient."
            return
        }
    }

    if (-not (Test-Path -LiteralPath $candidates[3].Path)) {
        if (Try-DownloadWithCertUtil $Url $candidates[3].Path) {
            Copy-Item -LiteralPath $candidates[3].Path -Destination $Destination
            Assert-MicrosoftSignature $Destination
            Write-Ok "Download succeeded via certutil."
            return
        }
    }

    $tlsBackup = Join-Path $script:RunRoot "TLS_REGISTRY_STATE_BEFORE_REPAIR.json"
    Enable-PtarTls12Compatibility $tlsBackup

    throw (
        "All download engines failed. PTAR enabled TLS 1.2/.NET strong-crypto " +
        "compatibility settings. Restart Windows manually, then run RUN_PTAR_AUTO.bat again. " +
        "No automatic reboot was performed."
    )
}

function Find-Fxc {
    $cmd = Get-Command fxc.exe -ErrorAction SilentlyContinue
    if ($cmd -ne $null) {
        return $cmd.Source
    }

    $pf86 = ${env:ProgramFiles(x86)}
    $candidates = @(
        (Join-Path $pf86 "Windows Kits\8.1\bin\x64\fxc.exe"),
        (Join-Path $pf86 "Windows Kits\8.1\bin\x86\fxc.exe")
    )
    foreach ($p in $candidates) {
        if (Test-Path -LiteralPath $p) {
            return $p
        }
    }

    $kit10 = Join-Path $pf86 "Windows Kits\10\bin"
    if (Test-Path -LiteralPath $kit10) {
        $found = Get-ChildItem -Path $kit10 -Filter fxc.exe -Recurse -ErrorAction SilentlyContinue |
                 Where-Object { $_.FullName -match "\\x64\\fxc\.exe$" } |
                 Sort-Object FullName -Descending |
                 Select-Object -First 1
        if ($found -ne $null) {
            return $found.FullName
        }
    }
    return $null
}

function Find-VcVars64 {
    $custom = "C:\PTAR_Toolchain\VS2019BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if (Test-Path -LiteralPath $custom) {
        return $custom
    }

    $pf86 = ${env:ProgramFiles(x86)}
    $vswhere = Join-Path $pf86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        try {
            $install = & $vswhere -latest -products * `
                -version "[16.0,17.0)" `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath
            if ($LASTEXITCODE -eq 0 -and $install) {
                $candidate = Join-Path ($install | Select-Object -First 1) "VC\Auxiliary\Build\vcvars64.bat"
                if (Test-Path -LiteralPath $candidate) {
                    return $candidate
                }
            }
        } catch {}
    }

    $roots = @(
        (Join-Path $pf86 "Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"),
        (Join-Path $pf86 "Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"),
        (Join-Path $pf86 "Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"),
        (Join-Path $pf86 "Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat")
    )
    foreach ($p in $roots) {
        if (Test-Path -LiteralPath $p) {
            return $p
        }
    }
    return $null
}

function Import-VcVars([string]$VcVarsPath) {
    Write-Info ("Importing MSVC environment from: " + $VcVarsPath)
    $quoted = '"' + $VcVarsPath + '"'
    $lines = & $env:ComSpec /d /s /c ("call " + $quoted + " >nul && set")
    if ($LASTEXITCODE -ne 0) {
        throw "vcvars64.bat failed."
    }

    foreach ($line in $lines) {
        if ($line -match "^([^=]+)=(.*)$") {
            $name = $matches[1]
            $value = $matches[2]
            if ($name -and -not $name.StartsWith("=")) {
                [Environment]::SetEnvironmentVariable($name, $value, "Process")
            }
        }
    }

    $cl = Get-Command cl.exe -ErrorAction SilentlyContinue
    if ($cl -eq $null) {
        throw "MSVC environment imported but cl.exe is still unavailable."
    }
    Write-Ok ("cl.exe: " + $cl.Source)
}

function Get-SystemFacts {
    $os = Get-WmiObject Win32_OperatingSystem
    $gpus = Get-WmiObject Win32_VideoController
    return @{
        OS = $os
        GPUs = $gpus
    }
}

function Install-VS2019BuildTools([string]$RunDownloads) {
    Write-Stage "Installing Visual Studio 2019 C++ Build Tools"

    $url = "https://aka.ms/vs/16/release/vs_buildtools.exe"
    $installer = Join-Path $RunDownloads "vs_buildtools_2019.exe"
    Download-OfficialMicrosoftFile $url $installer

    $installPath = "C:\PTAR_Toolchain\VS2019BuildTools"
    if (-not (Test-Path -LiteralPath "C:\PTAR_Toolchain")) {
        New-Item -ItemType Directory -Path "C:\PTAR_Toolchain" | Out-Null
    }

    $args = @(
        "--quiet",
        "--wait",
        "--norestart",
        "--nocache",
        "--installPath", $installPath,
        "--add", "Microsoft.VisualStudio.Workload.VCTools",
        "--includeRecommended"
    )

    Write-Info "Starting Microsoft Visual Studio installer..."
    $p = Start-Process -FilePath $installer -ArgumentList $args -Wait -PassThru
    Write-Info ("Visual Studio installer exit code: " + $p.ExitCode)

    if ($p.ExitCode -eq 3010 -or $p.ExitCode -eq 1641) {
        $script:RebootRecommended = $true
        Write-Warn "Microsoft reports that a reboot is required/recommended."
    } elseif ($p.ExitCode -ne 0) {
        throw ("Visual Studio Build Tools installation failed. Exit code=" + $p.ExitCode)
    }
}

function Install-Windows81Sdk([string]$RunDownloads) {
    Write-Stage "Installing Windows 8.1 SDK / FXC"

    # Official Windows SDK archive installer link published by Microsoft.
    $url = "https://go.microsoft.com/fwlink/p/?LinkId=323507"
    $installer = Join-Path $RunDownloads "windows81_sdksetup.exe"
    Download-OfficialMicrosoftFile $url $installer

    Write-Info "Trying unattended Windows 8.1 SDK installation..."
    $p = Start-Process -FilePath $installer `
        -ArgumentList @("/quiet", "/norestart") `
        -Wait -PassThru

    Write-Info ("Windows 8.1 SDK installer exit code: " + $p.ExitCode)
    if ($p.ExitCode -eq 3010 -or $p.ExitCode -eq 1641) {
        $script:RebootRecommended = $true
    }

    $fxc = Find-Fxc
    if ($fxc -eq $null) {
        Write-Warn "Silent SDK installation did not expose fxc.exe."
        Write-Warn "Launching the official Microsoft SDK installer interactively as a fallback."
        Write-Warn "If a component selection page appears, keep the Windows Software Development Kit tools selected."
        $p2 = Start-Process -FilePath $installer -Wait -PassThru
        Write-Info ("Interactive SDK installer exit code: " + $p2.ExitCode)
        if ($p2.ExitCode -eq 3010 -or $p2.ExitCode -eq 1641) {
            $script:RebootRecommended = $true
        }
    }
}

function Add-FxcToProcessPath([string]$FxcPath) {
    $dir = Split-Path -Parent $FxcPath
    if (-not ($env:Path.Split(";") -contains $dir)) {
        $env:Path = $dir + ";" + $env:Path
    }
    Write-Ok ("fxc.exe: " + $FxcPath)
}

function Write-AutomationReport(
    [string]$Path,
    [hashtable]$Facts,
    [string]$VcVars,
    [string]$Fxc,
    [string]$TestRun,
    [int]$ExitCode
) {
    $lines = @()
    $lines += "PTAR AUTOMATION REPORT"
    $lines += "======================"
    $lines += ("Date: " + (Get-Date).ToString("s"))
    $lines += ("OS: " + $Facts.OS.Caption + " " + $Facts.OS.Version + " " + $Facts.OS.OSArchitecture)
    $lines += ("vcvars64: " + $VcVars)
    $lines += ("fxc_optional: " + $Fxc)
    $lines += "hlsl_compile_path: D3DCompileFromFile / D3DCompiler API"
    $lines += ("hardware_run: " + $TestRun)
    $lines += ("exit_code: " + $ExitCode)
    $lines += ("reboot_recommended: " + $script:RebootRecommended)
    $lines += ""
    $lines += "GPU inventory:"
    foreach ($gpu in $Facts.GPUs) {
        $lines += ("- " + $gpu.Name + " | Driver=" + $gpu.DriverVersion)
    }
    $lines | Set-Content -LiteralPath $Path -Encoding UTF8
}

# -------------------------------------------------------------------------
# Resolve project root before elevation.
#
# v0.10.1 FIX:
# The normal launcher does not pass -ProjectRoot. The script derives its
# project root from its own file location, which remains stable across UAC
# elevation on PowerShell 4 / Windows 8.1.
# -------------------------------------------------------------------------
try {
    if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
        $rootCandidate = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
        $rootItem = Get-Item -LiteralPath $rootCandidate -ErrorAction Stop
    } else {
        $rootItem = Get-Item -LiteralPath $ProjectRoot -ErrorAction Stop
    }

    if (-not $rootItem.PSIsContainer) {
        throw ("ProjectRoot is not a directory: " + $rootItem.FullName)
    }

    $ProjectRoot = $rootItem.FullName
}
catch {
    Write-Host ("[PTAR][FAIL] Cannot resolve project root: " + $_.Exception.Message) -ForegroundColor Red
    exit 91
}

# -------------------------------------------------------------------------
# UAC elevation.
#
# Do not relay ProjectRoot on the elevated command line. The elevated child
# derives the same project root again from $PSScriptRoot.
# -------------------------------------------------------------------------
if (-not (Test-IsAdministrator)) {
    Write-Host "[PTAR] Administrator rights are required for Microsoft toolchain installation."
    Write-Host "[PTAR] Windows will display a normal UAC approval prompt."

    $self = $MyInvocation.MyCommand.Path
    $elevatedArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", ('"' + $self + '"'),
        "-Elevated"
    )
    if ($VerifyOnly) {
        $elevatedArgs += "-VerifyOnly"
    }

    try {
        $elevatedProcess = Start-Process -FilePath "powershell.exe" `
            -Verb RunAs `
            -ArgumentList $elevatedArgs `
            -Wait `
            -PassThru

        if ($elevatedProcess -eq $null) {
            Write-Host "[PTAR][FAIL] Elevated PowerShell did not start." -ForegroundColor Red
            exit 92
        }

        exit [int]$elevatedProcess.ExitCode
    }
    catch {
        Write-Host ("[PTAR][FAIL] UAC/elevation failed or was cancelled: " + $_.Exception.Message) -ForegroundColor Red
        exit 93
    }
}

# -------------------------------------------------------------------------
# Unique run directory. Nothing from prior runs is deleted or overwritten.
# -------------------------------------------------------------------------
$script:RebootRecommended = $false
$stamp = (Get-Date).ToString("yyyyMMdd_HHmmss")
$rand = Get-Random -Minimum 10000 -Maximum 99999
$RunRoot = Join-Path $ProjectRoot ("automation\windows\runs\run_" + $stamp + "_" + $rand)
$script:RunRoot = $RunRoot
$RunDownloads = Join-Path $RunRoot "downloads"
New-Item -ItemType Directory -Path $RunRoot | Out-Null
New-Item -ItemType Directory -Path $RunDownloads | Out-Null

$TranscriptPath = Join-Path $RunRoot "PTAR_AUTOMATION_TRANSCRIPT.txt"
try {
    Start-Transcript -Path $TranscriptPath | Out-Null
} catch {}

$finalExit = 99
$latestHardwareRun = ""

try {
    Write-Stage "Machine preflight"
    $facts = Get-SystemFacts
    Write-Info ("OS: " + $facts.OS.Caption + " version " + $facts.OS.Version + " " + $facts.OS.OSArchitecture)

    if ($facts.OS.OSArchitecture -notmatch "64") {
        throw "PTAR hardware validation requires 64-bit Windows."
    }

    if ($facts.OS.Version -notmatch "^6\.3") {
        Write-Warn ("This project targets Windows 8.1 (6.3). Detected: " + $facts.OS.Version)
        Write-Warn "The automation will not silently claim Windows 8.1 validation on another OS."
    } else {
        Write-Ok "Windows 8.1 detected."
    }

    $nvidia = $false
    foreach ($gpu in $facts.GPUs) {
        Write-Info ("GPU: " + $gpu.Name + " | driver " + $gpu.DriverVersion)
        if ($gpu.Name -match "NVIDIA") {
            $nvidia = $true
        }
    }
    if (-not $nvidia) {
        throw "No NVIDIA GPU detected. Hardware benchmark would not validate the GTX path."
    }
    Write-Ok "NVIDIA GPU detected."

    $drive = Get-WmiObject Win32_LogicalDisk -Filter "DeviceID='C:'"
    if ($drive -ne $null) {
        $freeGB = [Math]::Round(($drive.FreeSpace / 1GB), 1)
        Write-Info ("C: free space: " + $freeGB + " GB")
        if ($freeGB -lt 4) {
            throw "Less than 4 GB free on C:. Toolchain installation is unsafe."
        }
        if ($freeGB -lt 12) {
            Write-Warn "Less than 12 GB free. Build Tools installation may require more space."
        }
    }

    Write-Stage "Toolchain detection"
    $vcvars = Find-VcVars64
    $fxc = Find-Fxc

    if ($vcvars -ne $null) {
        Write-Ok ("Existing MSVC toolchain found: " + $vcvars)
    } else {
        Write-Warn "MSVC C++ Build Tools not found."
    }

    if ($fxc -ne $null) {
        Write-Ok ("Existing FXC found: " + $fxc)
    } else {
        Write-Warn "FXC not found."
    }

    if ($VerifyOnly) {
        if ($vcvars -eq $null) {
            $finalExit = 20
            Write-Fail "Verify-only result: MSVC toolchain incomplete."
        } else {
            Import-VcVars $vcvars
            if ($fxc -ne $null) {
                Add-FxcToProcessPath $fxc
            }
            $finalExit = 0
            Write-Ok "Verify-only result: MSVC ready. FXC is optional in v0.10.3."
        }
    } else {
        if ($vcvars -eq $null) {
            Install-VS2019BuildTools $RunDownloads
            $vcvars = Find-VcVars64
            if ($vcvars -eq $null) {
                if ($script:RebootRecommended) {
                    throw "Build Tools installed but not yet usable. Restart Windows, then double-click RUN_PTAR_AUTO.bat again."
                }
                throw "Build Tools installation completed but vcvars64.bat was not found."
            }
        }

        Import-VcVars $vcvars

        # v0.10.4: FXC is optional.
        # Preserve a valid path found during preflight. Only rescan if that
        # path is empty or no longer exists after vcvars import.
        if ($fxc -eq $null -or -not (Test-Path -LiteralPath $fxc)) {
            $fxc = Find-Fxc
        }
        if ($fxc -ne $null -and (Test-Path -LiteralPath $fxc)) {
            Add-FxcToProcessPath $fxc
            Write-Info ("FXC available (optional): " + $fxc)
        } else {
            $fxc = $null
            Write-Info "FXC unavailable; continuing with the D3DCompiler API path."
        }

        Write-Stage "Final compiler verification"
        $clCmd = Get-Command cl.exe -ErrorAction SilentlyContinue
        if ($clCmd -eq $null) {
            throw "MSVC toolchain environment is incomplete after installation."
        }
        Write-Ok ("cl.exe: " + $clCmd.Definition)
        Write-Info "HLSL compiler path: D3DCompileFromFile / D3DCompiler API (FXC optional)."

        Write-Stage "PTAR-NG MoE v01 SF5 hardware build + GPU validation"
        $buildBat = Join-Path $ProjectRoot "build\windows\BUILD_AND_RUN_MOE_NG_V01_HARDWARE_VALIDATION.bat"
        if (-not (Test-Path -LiteralPath $buildBat)) {
            throw ("Missing PTAR build launcher: " + $buildBat)
        }

        $hardwareRoot = Join-Path $ProjectRoot "build\windows\hardware_builds"
        $before = @()
        if (Test-Path -LiteralPath $hardwareRoot) {
            $before = @(Get-ChildItem -LiteralPath $hardwareRoot -Directory -ErrorAction SilentlyContinue |
                        Select-Object -ExpandProperty FullName)
        }

        $buildLog = Join-Path $RunRoot "PTAR_BUILD_AND_HARDWARE_TEST.txt"
        & $env:ComSpec /d /s /c ('call "' + $buildBat + '"') 2>&1 |
            Tee-Object -FilePath $buildLog
        $buildRc = $LASTEXITCODE

        $after = @()
        if (Test-Path -LiteralPath $hardwareRoot) {
            $after = @(Get-ChildItem -LiteralPath $hardwareRoot -Directory -ErrorAction SilentlyContinue |
                       Sort-Object LastWriteTime -Descending)
        }
        foreach ($dir in $after) {
            if ($before -notcontains $dir.FullName) {
                $latestHardwareRun = $dir.FullName
                break
            }
        }
        if ([string]::IsNullOrWhiteSpace($latestHardwareRun) -and $after.Count -gt 0) {
            $latestHardwareRun = $after[0].FullName
        }

        if ($buildRc -ne 0) {
            throw ("PTAR hardware validation failed. Exit code=" + $buildRc)
        }

        Write-Ok "PTAR-NG MoE build + DXBC audit + parity + GPU timing completed."
        if (-not [string]::IsNullOrWhiteSpace($latestHardwareRun)) {
            Write-Info ("Hardware run: " + $latestHardwareRun)
        }
        $finalExit = 0
    }
}
catch {
    if ($script:RebootRecommended) {
        $finalExit = 61
    } else {
        $finalExit = 50
    }
    Write-Fail $_.Exception.Message
    Write-Warn ("Full log: " + $TranscriptPath)
}
finally {
    try {
        if (-not (Get-Variable facts -ErrorAction SilentlyContinue)) {
            $facts = Get-SystemFacts
        }
        if (-not (Get-Variable vcvars -ErrorAction SilentlyContinue)) {
            $vcvars = ""
        }
        if (-not (Get-Variable fxc -ErrorAction SilentlyContinue)) {
            $fxc = ""
        }
        $report = Join-Path $RunRoot "PTAR_AUTOMATION_REPORT.txt"
        Write-AutomationReport $report $facts $vcvars $fxc $latestHardwareRun $finalExit
        Write-Info ("Automation report: " + $report)
    } catch {}

    try { Stop-Transcript | Out-Null } catch {}
}

if ($script:RebootRecommended) {
    Write-Warn "A Microsoft installer reported that a Windows restart is required/recommended."
    Write-Warn "PTAR intentionally NEVER restarts the PC automatically."
    Write-Warn "Save your work, restart Windows, then double-click RUN_PTAR_AUTO.bat again."
}

if ($finalExit -eq 0) {
    if (-not [string]::IsNullOrWhiteSpace($latestHardwareRun)) {
        $results = Join-Path $latestHardwareRun "results"
        if (Test-Path -LiteralPath $results) {
            Start-Process explorer.exe -ArgumentList ('"' + $results + '"')
        } else {
            Start-Process explorer.exe -ArgumentList ('"' + $latestHardwareRun + '"')
        }
    } else {
        Start-Process explorer.exe -ArgumentList ('"' + $RunRoot + '"')
    }
} else {
    Start-Process explorer.exe -ArgumentList ('"' + $RunRoot + '"')
}

exit $finalExit

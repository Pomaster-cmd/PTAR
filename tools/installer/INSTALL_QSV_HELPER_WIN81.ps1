$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootCandidate = Join-Path $ScriptDir '..\..'
$Root = (Resolve-Path -LiteralPath $RootCandidate -ErrorAction Stop).Path

$Tools       = Join-Path $Root 'tools\qsv'
$LocalFF     = Join-Path $Tools 'b18k17_ffmpeg.exe'
$DownloadDir = Join-Path $Root '_download_qsv'
$Log         = Join-Path $Root 'INSTALL_QSV_HELPER_WIN81.log'
$TargetFile  = Join-Path $Root 'win81_nis_install_target.txt'

if (Test-Path -LiteralPath $Log) {
    Remove-Item -LiteralPath $Log -Force -ErrorAction SilentlyContinue
}

function Write-Log([string]$Text) {
    $Text | Tee-Object -FilePath $Log -Append | Write-Host
}

function Resolve-GameTarget {
    if (Test-Path -LiteralPath $TargetFile -PathType Leaf) {
        $line = Get-Content -LiteralPath $TargetFile -TotalCount 1
        if ($line -ne $null) {
            $t = ([string]$line).Trim().Trim('"')
            if ($t -and (Test-Path -LiteralPath $t -PathType Container)) {
                return (Resolve-Path -LiteralPath $t -ErrorAction Stop).Path
            }
        }
    }

    $renderer = Get-ChildItem -LiteralPath $Root -Filter '*-Win64-Shipping.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($renderer -and (Test-Path -LiteralPath (Join-Path $Root 'd3d11.dll') -PathType Leaf)) {
        return $Root
    }
    return $null
}

function Test-QsvEncoder([string]$FFmpeg) {
    if (-not (Test-Path -LiteralPath $FFmpeg -PathType Leaf)) { return $false }

    $stdout = Join-Path $DownloadDir 'qsv_encoders.stdout.txt'
    $stderr = Join-Path $DownloadDir 'qsv_encoders.stderr.txt'
    Remove-Item -LiteralPath $stdout,$stderr -Force -ErrorAction SilentlyContinue

    $p = Start-Process -FilePath $FFmpeg `
        -ArgumentList @('-hide_banner','-encoders') `
        -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr

    if ($p.ExitCode -ne 0) { return $false }

    $text = ''
    if (Test-Path -LiteralPath $stdout) { $text += [IO.File]::ReadAllText($stdout) }
    if (Test-Path -LiteralPath $stderr) { $text += [IO.File]::ReadAllText($stderr) }
    return ($text -match 'h264_qsv')
}

function Download-CompatibleFFmpeg {
    $zip = Join-Path $DownloadDir 'ffmpeg-4.4-essentials_build.zip'
    $urls = @(
        'https://github.com/GyanD/codexffmpeg/releases/download/4.4/ffmpeg-4.4-essentials_build.zip',
        'https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-4.4-essentials_build.zip'
    )

    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    } catch {
        Write-Log ('[WARN] TLS12 explicite indisponible: ' + $_.Exception.Message)
    }

    foreach ($url in $urls) {
        try {
            Write-Log ('[DL] ' + $url)
            if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }

            $wc = New-Object Net.WebClient
            $wc.Headers.Add('User-Agent','PTAR-FULLSTACK1-QSV-WIN81')
            $wc.DownloadFile($url, $zip)

            if (-not (Test-Path -LiteralPath $zip -PathType Leaf)) { throw 'Archive non creee.' }
            if ((Get-Item -LiteralPath $zip).Length -lt 1000000) { throw 'Archive telechargee anormalement petite.' }

            Add-Type -AssemblyName System.IO.Compression.FileSystem
            $extract = Join-Path $DownloadDir ('extract_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
            New-Item -ItemType Directory -Force -Path $extract | Out-Null
            [IO.Compression.ZipFile]::ExtractToDirectory($zip, $extract)

            $candidate = Get-ChildItem -LiteralPath $extract -Recurse -Filter 'ffmpeg.exe' -ErrorAction Stop | Select-Object -First 1
            if (-not $candidate) { throw 'ffmpeg.exe absent de l archive.' }

            Copy-Item -LiteralPath $candidate.FullName -Destination $LocalFF -Force
            if (-not (Test-QsvEncoder $LocalFF)) {
                Remove-Item -LiteralPath $LocalFF -Force -ErrorAction SilentlyContinue
                throw 'Le binaire extrait ne declare pas h264_qsv.'
            }

            Write-Log ('[OK] Helper compatible prepare localement: ' + $LocalFF)
            return
        }
        catch {
            Write-Log ('[WARN] Echec source: ' + $_.Exception.Message)
        }
    }

    throw 'Impossible de telecharger et valider un helper FFmpeg/QSV compatible.'
}

try {
    Write-Log ('=== FULLSTACK1 RECORDERFIX2 QSVINSTALLFIX1 ' + (Get-Date -Format s) + ' ===')
    Write-Log ('[ROOT] ' + $Root)

    New-Item -ItemType Directory -Force -Path $Tools,$DownloadDir | Out-Null

    $Target = Resolve-GameTarget
    if (-not $Target) { throw 'Cible du jeu introuvable. Lance d abord 01-INSTALL_FULLSTACK1.bat.' }

    Write-Log ('[TARGET] ' + $Target)

    $TargetTools = Join-Path $Target 'tools\qsv'
    $TargetFF    = Join-Path $TargetTools 'b18k17_ffmpeg.exe'
    $Ready       = Join-Path $TargetTools 'B18K18_QSV_READY.txt'

    New-Item -ItemType Directory -Force -Path $TargetTools | Out-Null

    if (Test-Path -LiteralPath $TargetFF -PathType Leaf) {
        Write-Log ('[INFO] Helper deja present dans la cible: ' + $TargetFF)
        if (-not (Test-QsvEncoder $TargetFF)) {
            throw 'Le helper cible existe mais ne declare pas h264_qsv. Aucun ecrasement automatique.'
        }
    }
    else {
        if (-not (Test-Path -LiteralPath $LocalFF -PathType Leaf)) { Download-CompatibleFFmpeg }
        if (-not (Test-QsvEncoder $LocalFF)) { throw 'Le helper local ne passe pas le controle h264_qsv.' }

        $localFull=[IO.Path]::GetFullPath($LocalFF)
        $targetFull=[IO.Path]::GetFullPath($TargetFF)
        if ($localFull -ieq $targetFull) {
            Write-Log ('[OK] Helper local deja a son chemin cible exact: ' + $TargetFF)
        } else {
            Copy-Item -LiteralPath $LocalFF -Destination $TargetFF -Force
            Write-Log ('[OK] Helper copie vers la cible exacte: ' + $TargetFF)
        }
    }

    if (-not (Test-Path -LiteralPath $TargetFF -PathType Leaf)) { throw 'Postcondition impossible: helper absent apres copie.' }
    if (-not (Test-QsvEncoder $TargetFF)) { throw 'Postcondition impossible: helper cible sans h264_qsv.' }

    $size = (Get-Item -LiteralPath $TargetFF).Length
    if ($size -lt 1000000) { throw ('Postcondition impossible: helper trop petit (' + $size + ' octets).') }

    @(
        'READY=YES',
        ('PATH=' + $TargetFF),
        ('BYTES=' + $size),
        ('DATE=' + (Get-Date -Format s)),
        'ENCODER=h264_qsv',
        'INSTALLER=QSVINSTALLFIX1'
    ) | Set-Content -LiteralPath $Ready -Encoding ASCII

    if (-not (Test-Path -LiteralPath $Ready -PathType Leaf)) { throw 'Echec d ecriture du marqueur READY.' }

    Write-Log ('[PASS] QSV helper READY: ' + $TargetFF)
    Write-Log ('[PASS] Taille: ' + $size + ' octets')
    Write-Log ('[PASS] h264_qsv detecte')
    exit 0
}
catch {
    try { Write-Log ('[FAIL] ' + $_.Exception.Message) }
    catch { Write-Host ('[FAIL] ' + $_.Exception.Message) }
    exit 90
}

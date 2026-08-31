$ErrorActionPreference = 'Stop'
$script:FailCount = 0

function Pass([string]$Message) { Write-Output ('[PASS] ' + $Message) }
function Fail([string]$Message) { Write-Output ('[FAIL] ' + $Message); $script:FailCount++ }
function Warn([string]$Message) { Write-Output ('[WARN] ' + $Message) }

function Clean-PathText([string]$Value) {
    if ($null -eq $Value) { return $null }
    $s = [string]$Value
    $s = $s.Replace([char]0x00A0, [char]0x0020)
    $s = $s.Replace([char]0x0000, [char]0x0020)
    return $s.Trim().Trim('"')
}

function First-Line([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $v = Get-Content -LiteralPath $Path -ErrorAction Stop | Select-Object -First 1
    return (Clean-PathText $v)
}

function Sha256([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
        finally { $stream.Dispose() }
    }
    finally { $sha.Dispose() }
}

function Ini-Value([string]$Path,[string]$Key) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $pattern = '^' + [regex]::Escape($Key) + '=(.*)$'
    foreach ($line in Get-Content -LiteralPath $Path -ErrorAction Stop) {
        if ([string]$line -match $pattern) { return $Matches[1].Trim() }
    }
    return $null
}

function Check-Ini([string]$Path,[string]$Key,[string]$Expected) {
    $v = Ini-Value $Path $Key
    if ($v -eq $Expected) { Pass ($Key + '=' + $v) }
    else {
        if ($null -eq $v) { $shown='<absent>' } else { $shown=$v }
        Fail ($Key + '=' + $shown + ' ; attendu ' + $Expected)
    }
}

function Has-Exact([string]$Path,[string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    foreach ($line in Get-Content -LiteralPath $Path -ErrorAction Stop) {
        if ([string]$line -eq $Expected) { return $true }
    }
    return $false
}

try {
    # VERIFYFIX5: root comes only from PSScriptRoot; target/exe lines are sanitized for NBSP/NUL/quotes.
    # No path canonicalization and no package path is passed through CMD.
    $Root = Split-Path -Parent $PSScriptRoot
    $TargetFile = Join-Path $Root 'win81_nis_install_target.txt'
    $ExeFile = Join-Path $Root 'win81_nis_install_exe.txt'
    $ExpectedDll = '3d4d777c943ced0f475df1371d3a2f9eeb5eeb80c66e9fb217c4d91057f32453'
    $ExpectedVisible = '67b163bf3203366562f066c10ac971992b5846991f19c68f61cbd975f9ef8305'

    Write-Output '============================================================'
    Write-Output 'PTAR DISPLAY DELIVERY LAB DWMPHASE2 AUTOCOLLECT1 RC18 - FULLSTACK1 VERIFY - VERIFYFIX5'
    Write-Output 'Lecture seule. Aucun fichier du jeu n est modifie.'
    Write-Output ('[INFO] Package: ' + $Root)
    Write-Output '============================================================'

    $Target = First-Line $TargetFile
    if ([string]::IsNullOrWhiteSpace($Target)) { Fail 'Cible PTAR absente ou vide.'; $Target=$null }
    elseif (-not (Test-Path -LiteralPath $Target -PathType Container)) { Fail ('Dossier cible absent: ' + $Target); $Target=$null }
    else { Pass ('Cible PTAR: ' + $Target) }

    $Exe = First-Line $ExeFile
    if ([string]::IsNullOrWhiteSpace($Exe)) { Fail 'Renderer cible absent ou vide dans win81_nis_install_exe.txt.' }
    elseif (Test-Path -LiteralPath $Exe -PathType Leaf) { Pass ('Renderer cible present: ' + $Exe) }
    else { Fail ('Renderer cible absent: ' + $Exe) }

    if ($Target) {
        $Dll = Join-Path $Target 'd3d11.dll'
        $Ini = Join-Path $Target 'win81_nis.ini'
        $Ver = Join-Path $Target 'win81_nis_version.txt'

        if (Test-Path -LiteralPath $Dll -PathType Leaf) {
            $hash=Sha256 $Dll; Write-Output ('[INFO] DLL SHA-256: ' + $hash)
            if ($hash -eq $ExpectedDll) { Pass 'DLL LAB DWMPHASE2 AUTOCOLLECT1 RC18 exacte.' } else { Fail ('DLL inattendue. Attendu ' + $ExpectedDll) }
        } else { Fail 'd3d11.dll absente.' }

        if (Test-Path -LiteralPath $Ini -PathType Leaf) {
            Pass 'win81_nis.ini present.'
            Check-Ini $Ini 'VideoRecordProfile' '3'
            Check-Ini $Ini 'FrameGeneration' '0'
            Check-Ini $Ini 'FrameGenerationSharedFlush' '1'
            Check-Ini $Ini 'FrameGenerationRequireVSync' '0'
            Check-Ini $Ini 'FrameGenerationPresentSync' '1'
            Check-Ini $Ini 'FrameGenerationMEBudgetUs' '6000'
            Check-Ini $Ini 'FrameGenerationAdaptiveDeadline' '1'
            $q=Ini-Value $Ini 'FrameGenerationQuality'; $g=Ini-Value $Ini 'FrameGenerationBlendGuard'; $m=Ini-Value $Ini 'FrameGenerationMEScalePercent'
            if (@('0','1','2','3') -contains $q) { Pass ('FrameGenerationQuality=' + $q) } else { Fail ('FrameGenerationQuality invalide: ' + $q) }
            $profiles=@{'0'='65/33';'1'='50/33';'2'='35/50';'3'='25/50'}
            if ($profiles.ContainsKey([string]$q)) {
                $actual=([string]$g + '/' + [string]$m)
                if ($actual -eq $profiles[[string]$q]) { Pass ('Profil qualite coherent ' + $q + ' -> ' + $actual) }
                else { Fail ('Profil qualite incoherent ' + $q + ' -> ' + $actual) }
            }
        } else { Fail 'win81_nis.ini absent.' }

        if (Test-Path -LiteralPath $Ver -PathType Leaf) {
            Pass 'win81_nis_version.txt present.'
            $required=@(
                'WIN81_NIS_VERSION=P1FG7N-PRESENTDELIVERY1-DWMPHASE2-AUTOCOLLECT1-PTARFG1B18K18-FULLSTACK1',
                'FG_VISIBLE_DELIVERY=PRESENTDELIVERY1_PREWAIT_REMOVED_SYNC1',
                'FG_DWM_PHASE_DIAG=DWMPHASE2_SAMPLE_EVERY_8_G_AND_R_AUTOCOLLECT1',
                'FG_DWM_PHASE_HOTKEY=CTRL_F4_SNAPSHOT_HUD_DWM_LAB',
                'FG_DWM_PHASE_AUTODUMP=F8_STATUS_EDGE_TO_LOG',
                'FG_VISIBLE_RESULT_CAPTURE=PTAR_VISIBLE_VERIFIER_LAST_OUTPUT_TXT',
                'FG_VISIBLE_TEST_HUD_HOTKEY=CTRL_F5_HUD_VISIB_TEST',
                'DWM_TIMING_INFO_SIZE=292_PACK4',
                'DWM_TIMING_HWND=NULL_WINDOWS_8_1',
                'FG_PRESENT_CALL=IDXGISWAPCHAIN_PRESENT_SYNCINTERVAL_1_FLAGS_0',
                'FRAME_GENERATION_PRESENT=SYNC1_FLAGS0_ISOLATED_DISPLAY_THREAD',
                'FG_PRE_PRESENT_WAITFORVBLANK=DISABLED_BY_RC18_PATCH',
                'RC18_PRESENTATION_POLICY=PRESENTDELIVERY1_SYNC1_NO_PREWAIT',
                'FG_SHARED_TRANSPORT=KEYED_PRIMARY_LEGACY_SHARED_END_TO_END_FALLBACK',
                'FG_RESIZE_GUARD=RESIZEGUARD1_LEGACY_ACTIVE_AND_SCHEDULER_ONLY',
                'FG_QUALITY_ENGINE=FGQUALITY3_Q3_MOTION_DISCONTINUITY_STABILIZER',
                ('DLL_SHA256=' + $ExpectedDll)
            )
            foreach($line in $required) {
                if (Has-Exact $Ver $line) { Pass $line } else { Fail ('Marqueur absent: ' + $line) }
            }
        } else { Fail 'win81_nis_version.txt absent.' }
    }

    $Visible=Join-Path $Root 'diag\visible_verifier\win81_vblank2_visible_marker_x64.exe'
    if (Test-Path -LiteralPath $Visible -PathType Leaf) {
        $vh=Sha256 $Visible; Write-Output ('[INFO] Visible verifier SHA-256: ' + $vh)
        if ($vh -eq $ExpectedVisible) { Pass 'Visible verifier HOTKEYFIX1 ESCSAFE1 exact.' } else { Fail 'Visible verifier non conforme.' }
    } else { Fail 'Visible verifier absent.' }
    if (Test-Path -LiteralPath (Join-Path $Root 'diag\04-ARM_VISIBLE_FRAME_VERIFIER.bat') -PathType Leaf) { Pass 'Lanceur visible verifier present.' } else { Fail 'Lanceur visible verifier absent.' }
    if (Test-Path -LiteralPath (Join-Path $Root 'diag\set_vblank_diagnostics.ps1') -PathType Leaf) { Pass 'Helper VBlank present.' } else { Fail 'Helper VBlank absent.' }
}
catch { Fail ('Erreur interne du verificateur: ' + $_.Exception.Message) }

Write-Output '============================================================'
if ($script:FailCount -eq 0) { Write-Output '[PASS] PTAR LAB DWMPHASE2 AUTOCOLLECT1 RC18 VERIFIE.'; Write-Output '[PASS] Aucun ecart bloquant detecte.'; exit 0 }
Write-Output ('[FAIL] Verification incomplete ou non conforme. Echecs=' + $script:FailCount)
exit 1

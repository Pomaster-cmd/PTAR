$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
$ExpectedRuntime='bc291f0f91013df7a28630ffef44983856fce6eb71d79aca597ab292012165e0'
$SessionRoot=Join-Path $PackRoot '_CLICK_INPUT_DIAG'
$Latest=Join-Path $SessionRoot 'LATEST_SESSION.txt'

function Fail([string]$Message,[int]$Code=90){Write-Host ('[FAIL] '+$Message);exit $Code}
function Full([string]$Path){return [IO.Path]::GetFullPath($Path.Trim().Trim('"'))}
function Sha([string]$Path){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Is-TargetRunning([string]$ExePath){
  $want=Full $ExePath
  foreach($p in @(Get-Process -ErrorAction SilentlyContinue)){
    try{if((Full $p.MainModule.FileName) -ieq $want){return $true}}catch{}
  }
  return $false
}
function Last-Int([string]$Text,[string]$Label){
  $escaped=[Regex]::Escape($Label)
  $m=[Regex]::Matches($Text,'(?im)^.*?'+$escaped+'\s*[:=]?\s*([0-9]+)\s*$')
  if($m.Count -eq 0){return $null}
  return [int64]$m[$m.Count-1].Groups[1].Value
}
function Has-Line([string]$Text,[string]$Value){return $Text.IndexOf($Value,[StringComparison]::OrdinalIgnoreCase) -ge 0}

if(-not(Test-Path -LiteralPath $Latest -PathType Leaf)){Fail 'Aucune session CLICK_INPUT_DIAG active.' 2}
$Session=(Get-Content -LiteralPath $Latest -TotalCount 1).Trim()
$MetaPath=Join-Path $Session 'session.json'
if(-not(Test-Path -LiteralPath $MetaPath -PathType Leaf)){Fail 'session.json introuvable.' 3}
$meta=Get-Content -LiteralPath $MetaPath -Raw|ConvertFrom-Json
$TargetExe=[string]$meta.target_exe
$GameRoot=[string]$meta.game_root
if(Is-TargetRunning $TargetExe){Fail 'Ferme le jeu avant collecte/restauration.' 4}
$DllA=Join-Path $GameRoot 'd3d11.dll';$DllB=Join-Path $GameRoot 'win81_nis_dx11_x64.dll';$Ini=Join-Path $GameRoot 'win81_nis.ini';$Log=Join-Path $GameRoot 'win81_nis.log'
$runtimeOk=((Sha $DllA)-eq $ExpectedRuntime -and (Sha $DllB)-eq $ExpectedRuntime)
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$Out=Join-Path $Session ('RESULT_'+$stamp)
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$restoreStatus='NOT_ATTEMPTED'
try{
  if(-not $runtimeOk){throw 'Runtime change pendant la session ; preuves non interpretees.'}
  Copy-Item -LiteralPath $MetaPath -Destination (Join-Path $Out 'session.json') -Force
  Copy-Item -LiteralPath $Ini -Destination (Join-Path $Out 'win81_nis.ini.diag') -Force
  if(Test-Path -LiteralPath $Log -PathType Leaf){Copy-Item -LiteralPath $Log -Destination (Join-Path $Out 'win81_nis.log.full') -Force}
  $tailText=''
  $baseline=[int64]$meta.baseline_log_bytes
  $rotation='NO'
  if(Test-Path -LiteralPath $Log -PathType Leaf){
    $bytes=[IO.File]::ReadAllBytes($Log)
    $start=$baseline
    if($bytes.LongLength -lt $baseline){$start=0L;$rotation='YES'}
    $count=[int]($bytes.LongLength-$start)
    if($count -gt 0){$tailText=[Text.Encoding]::UTF8.GetString($bytes,[int]$start,$count)}
  }
  [IO.File]::WriteAllText((Join-Path $Out 'CLICK_INPUT_DIAG_RUNTIME_TAIL.log'),$tailText,[Text.Encoding]::UTF8)

  $labels=@(
    'P1U46 GAME MOUSE MESSAGES',
    'P1U46 GAME MOUSE MOVES',
    'P1U46 GAME MOUSE BUTTON MESSAGES',
    'P1U46 PRESENTER MOUSE MESSAGES',
    'P1U46 PRESENTER MOUSE BUTTONS',
    'P1U46 WM_MOUSEACTIVATE MESSAGES',
    'P1U46 WM_CAPTURECHANGED MESSAGES',
    'P1U46 SETCAPTURE CALLS',
    'P1U46 RELEASECAPTURE CALLS',
    'P1U46 INPUT API IAT PATCHES'
  )
  $vals=@{}
  foreach($label in $labels){$vals[$label]=Last-Int $tailText $label}
  $button=$vals['P1U46 GAME MOUSE BUTTON MESSAGES']
  $setcap=$vals['P1U46 SETCAPTURE CALLS']
  $relcap=$vals['P1U46 RELEASECAPTURE CALLS']
  $capchg=$vals['P1U46 WM_CAPTURECHANGED MESSAGES']
  $presbtn=$vals['P1U46 PRESENTER MOUSE BUTTONS']
  $focusYes=Has-Line $tailText 'P1U46 CLICK FOCUS GAME YES'
  $focusNo=Has-Line $tailText 'P1U46 CLICK FOCUS GAME NO'
  $captureYes=Has-Line $tailText 'P1U46 CLICK CAPTURE GAME YES'
  $captureNo=Has-Line $tailText 'P1U46 CLICK CAPTURE GAME NO'
  $foregroundYes=Has-Line $tailText 'P1U46 CLICK FOREGROUND GAME YES'
  $foregroundNo=Has-Line $tailText 'P1U46 CLICK FOREGROUND GAME NO'
  $diagEnabled=(Has-Line $tailText 'OPTIONAL PASSIVE TRACE ENABLED') -or (Has-Line $tailText 'P1U46 INPUT DIAGNOSTICS MODE')
  $f8Flush=Has-Line $tailText 'HOTKEY STATUS: passive transition trace flush'

  # CLICK CAPTURE GAME is sampled after the original WndProc returns.
  # For a normal DOWN+UP cycle Unreal/Slate may already have called ReleaseCapture,
  # so CLICK CAPTURE GAME NO is expected after a completed click and must not, by
  # itself, be classified as a broken capture contract. Prefer cumulative symmetry.
  $expectedClicks=$null
  $buttonPairsComplete=$false
  $captureCycleBalanced=$false
  if($button -ne $null -and $button -gt 0 -and (($button % 2) -eq 0)){
    $expectedClicks=[int64]($button / 2)
    $buttonPairsComplete=$true
    if($setcap -ne $null -and $relcap -ne $null -and $capchg -ne $null){
      $captureCycleBalanced=($setcap -eq $expectedClicks -and $relcap -eq $expectedClicks -and $capchg -ge $expectedClicks)
    }
  }

  $verdict='INCONCLUSIVE'
  $breakpoint='UNRESOLVED'
  if(-not $diagEnabled){$breakpoint='DIAGNOSTICS_MARKER_NOT_SEEN'}
  elseif(-not $f8Flush){$breakpoint='F8_TRACE_FLUSH_NOT_SEEN'}
  elseif($button -eq $null -or $button -le 0){$breakpoint='NO_GAME_MOUSE_BUTTON_EVIDENCE'}
  elseif($presbtn -ne $null -and $presbtn -gt 0){$verdict='BROKEN';$breakpoint='PRESENTER_RECEIVED_MOUSE_BUTTONS'}
  elseif($focusNo){$verdict='BROKEN';$breakpoint='CLICK_FOCUS_NOT_GAME'}
  elseif($foregroundNo){$verdict='BROKEN';$breakpoint='CLICK_FOREGROUND_NOT_GAME'}
  elseif(-not $buttonPairsComplete){$verdict='BROKEN';$breakpoint='INCOMPLETE_MOUSE_BUTTON_PAIR'}
  elseif($setcap -eq $null -or $setcap -lt $expectedClicks){$verdict='BROKEN';$breakpoint='SETCAPTURE_MISSING_FOR_ONE_OR_MORE_CLICKS'}
  elseif($relcap -eq $null -or $relcap -lt $expectedClicks){$verdict='BROKEN';$breakpoint='RELEASECAPTURE_MISSING_FOR_ONE_OR_MORE_CLICKS'}
  elseif($capchg -eq $null -or $capchg -lt $expectedClicks){$verdict='BROKEN';$breakpoint='WM_CAPTURECHANGED_MISSING_FOR_ONE_OR_MORE_CLICKS'}
  elseif($captureCycleBalanced -and $focusYes -and $foregroundYes){$verdict='INTACT';$breakpoint='WIN32_NATIVE_CLICK_CONTRACT_INTACT_LOOK_INSIDE_SLATE_MOUSEUP_CLICK_METHOD'}

  $report=New-Object Collections.Generic.List[string]
  $report.Add('PTAR CLICK_INPUT_DIAG - PASSIVE INPUT CONTRACT REPORT')
  $report.Add('===================================================')
  $report.Add('RUNTIME_SHA256='+$ExpectedRuntime)
  $report.Add('TARGET_EXE='+$TargetExe)
  $report.Add('LOG_ROTATED_DURING_SESSION='+$rotation)
  $report.Add('DIAGNOSTICS_MARKER_SEEN='+$(if($diagEnabled){'YES'}else{'NO'}))
  $report.Add('F8_TRACE_FLUSH_SEEN='+$(if($f8Flush){'YES'}else{'NO'}))
  foreach($label in $labels){$v=$vals[$label];$report.Add($label+'='+$(if($v -eq $null){'UNAVAILABLE'}else{[string]$v}))}
  $report.Add('CLICK_FOCUS_GAME='+$(if($focusYes){'YES'}elseif($focusNo){'NO'}else{'UNSEEN'}))
  $report.Add('CLICK_CAPTURE_GAME='+$(if($captureYes){'YES'}elseif($captureNo){'NO'}else{'UNSEEN'}))
  $report.Add('CLICK_FOREGROUND_GAME='+$(if($foregroundYes){'YES'}elseif($foregroundNo){'NO'}else{'UNSEEN'}))
  $report.Add('EXPECTED_COMPLETE_CLICKS='+$(if($expectedClicks -eq $null){'UNAVAILABLE'}else{[string]$expectedClicks}))
  $report.Add('CAPTURE_CYCLE_BALANCED='+$(if($captureCycleBalanced){'YES'}else{'NO'}))
  $report.Add('CLICK_CAPTURE_SAMPLE_SEMANTICS=POST_WNDPROC_RETURN')
  $report.Add('WIN32_NATIVE_CLICK_CONTRACT='+$verdict)
  $report.Add('BREAKPOINT='+$breakpoint)
  $report.Add('')
  $report.Add('TRACE_FILTERED:')
  $patterns='CAPTRACE|WM_LBUTTON|WM_CAPTURECHANGED|WM_MOUSEACTIVATE|WM_ACTIVATEAPP|WM_ACTIVATE|WM_SETFOCUS|WM_KILLFOCUS|SETCAPTURE CALLS|RELEASECAPTURE CALLS|CLICK FOCUS|CLICK CAPTURE|CLICK FOREGROUND|GAME MOUSE|PRESENTER MOUSE|FOCUS WINDOW|CAPTURE WINDOW|FOREGROUND WINDOW|passive transition trace flush'
  foreach($line in @($tailText -split "`r?`n")){if($line -match $patterns){$report.Add($line)}}
  $ReportPath=Join-Path $Out 'CLICK_INPUT_DIAG_REPORT.txt'
  $report|Set-Content -LiteralPath $ReportPath -Encoding UTF8

  $identity=@('d3d11.dll SHA256='+(Sha $DllA),'win81_nis_dx11_x64.dll SHA256='+(Sha $DllB))
  $identity|Set-Content -LiteralPath (Join-Path $Out 'INSTALLED_RUNTIME_IDENTITY.txt') -Encoding UTF8
}
finally{
  try{
    $Backup=[string]$meta.backup_ini
    if(Test-Path -LiteralPath $Backup -PathType Leaf){
      Copy-Item -LiteralPath $Backup -Destination $Ini -Force
      if((Sha $Ini) -eq [string]$meta.original_ini_sha256){$restoreStatus='PASS'}else{$restoreStatus='HASH_MISMATCH'}
    }else{$restoreStatus='BACKUP_MISSING'}
  }catch{$restoreStatus='ERROR: '+$_.Exception.Message}
}
Set-Content -LiteralPath (Join-Path $Out 'RESTORE_STATUS.txt') -Value ('INI_RESTORE='+$restoreStatus) -Encoding UTF8
if($restoreStatus -ne 'PASS'){Fail ('Collecte terminee mais restauration INI non confirmee : '+$restoreStatus) 20}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Zip=Join-Path $GameRoot ('PTAR_CLICK_INPUT_DIAG_RESULTS_'+$stamp+'.zip')
if(Test-Path -LiteralPath $Zip){Remove-Item -LiteralPath $Zip -Force}
[IO.Compression.ZipFile]::CreateFromDirectory($Out,$Zip,[IO.Compression.CompressionLevel]::Optimal,$false)
Write-Host '[PASS] Collecte terminee et INI original restaure bit-a-bit.'
Write-Host ('RESULTAT='+$Zip)
exit 0

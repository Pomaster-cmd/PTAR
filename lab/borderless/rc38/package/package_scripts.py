from __future__ import annotations
OVERLAY='BORDERLESS_AUTHORITY_RC38'

def _fill(s,values):
    for k,v in values.items():s=s.replace('@@'+k+'@@',v)
    if '@@' in s:raise ValueError('unexpanded template token')
    return s.replace('\n','\r\n')

def bat_scripts():
    def simple(action,title):return f'@echo off\r\nsetlocal EnableExtensions\r\ncd /d "%~dp0"\r\ntitle {title}\r\n"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0{OVERLAY}\\{action}.ps1"\r\nset "RC=%ERRORLEVEL%"\r\necho.\r\npause\r\nexit /b %RC%\r\n'
    uninstall=f'''@echo off\nsetlocal EnableExtensions\ncd /d "%~dp0"\ntitle PTAR RC38 BORDERLESS AUTHORITY - DESINSTALLATION COMPLETE SECURISEE\nfor %%I in ("%~dp0.") do set "ROOT=%%~fI"\nset "TMP=%TEMP%\\PTAR_RC38_UNINSTALL_%RANDOM%_%RANDOM%"\nmkdir "%TMP%" >nul 2>&1\ncopy /y "%ROOT%\\_PTAR_UNINSTALL\\PTAR_SAFE_UNINSTALL.ps1" "%TMP%\\PTAR_SAFE_UNINSTALL.ps1" >nul || exit /b 20\ncopy /y "%ROOT%\\{OVERLAY}\\uninstall_overlay.ps1" "%TMP%\\uninstall_overlay.ps1" >nul || exit /b 21\n"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%TMP%\\uninstall_overlay.ps1" -Root "%ROOT%" -Engine "%TMP%\\PTAR_SAFE_UNINSTALL.ps1"\nset "RC=%ERRORLEVEL%"\necho.\nif "%RC%"=="0" (echo [PASS] Desinstallation PTAR + RC38 terminee.) else (echo [FAIL] Desinstallation interrompue - code %RC%.)\necho Appuyez sur une touche pour fermer.\npause >nul\nif "%RC%"=="0" (start "" /b "%ComSpec%" /d /c "ping -n 3 127.0.0.1 >nul & del /f /q ^"%ROOT%\\00-DESINSTALLER_BORDERLESS_AUTHORITY_RC38.bat^" >nul 2>&1 & rd /s /q ^"%TMP%^" >nul 2>&1") else (rd /s /q "%TMP%" >nul 2>&1)\nexit /b %RC%\n'''.replace('\n','\r\n')
    return {'00-INSTALL_BORDERLESS_AUTHORITY_RC38.bat':simple('install','PTAR RC38 BORDERLESS AUTHORITY - INSTALL'),'00-VERIFY_BORDERLESS_AUTHORITY_RC38.bat':simple('verify','PTAR RC38 BORDERLESS AUTHORITY - VERIFY'),'00-ROLLBACK_BORDERLESS_AUTHORITY_RC38.bat':simple('rollback','PTAR RC38 BORDERLESS AUTHORITY - ROLLBACK GITHUB MAIN'),'00-DESINSTALLER_BORDERLESS_AUTHORITY_RC38.bat':uninstall}

INSTALL=r'''$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot;$Payload=Join-Path $PSScriptRoot 'payload';$StateRoot=Join-Path $PackRoot '_PTAR_UNINSTALL\state';$Log=Join-Path $PSScriptRoot 'PTAR_RC38_INSTALL_LAST.log'
$ExpectedRuntime='@@RUNTIME@@';$ExpectedIni='@@INI@@';$ExpectedVersion='@@VERSION@@';$ExpectedSidecar='@@SIDECAR@@';$KnownPtar=@(@@KNOWN@@)
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
function L([string]$s){$x='['+(Get-Date -Format 'HH:mm:ss')+'] '+$s;Write-Host $x;Add-Content -LiteralPath $Log -Value $x -Encoding UTF8}
function F([string]$s,[int]$c=90){L ('FAIL: '+$s);exit $c}
function Resolve-GameRoot{if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){return $PackRoot};$p=Split-Path -Parent $PackRoot;if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){return $p};return $null}
function VersionMatches([string]$dllHash,[string]$vp){if(-not(Test-Path -LiteralPath $vp -PathType Leaf)){return $false};$t=Get-Content -LiteralPath $vp -Raw;if($t -notmatch '(?m)^WIN81_NIS_VERSION='){return $false};return ($t -match ('(?m)^DLL_SHA256='+[regex]::Escape($dllHash)+'\s*$'))}
Set-Content -LiteralPath $Log -Value ('START '+(Get-Date).ToString('o')) -Encoding UTF8
$g=Resolve-GameRoot;if(-not $g){F 'Warhammer.exe introuvable : placer le pack dans le dossier du jeu ou dans un sous-dossier direct.' 2};if(Get-Process Warhammer -ErrorAction SilentlyContinue){F 'Fermer Warhammer avant installation.' 3}
$pr=Join-Path $Payload 'd3d11.dll';$pc=Join-Path $Payload 'win81_nis_dx11_x64.dll';$pi=Join-Path $Payload 'win81_nis.ini';$pv=Join-Path $Payload 'win81_nis_version.txt';$ps=Join-Path $Payload 'ptar_borderless.dll'
if((Sha $pr)-ne $ExpectedRuntime -or (Sha $pc)-ne $ExpectedRuntime -or (Sha $pi)-ne $ExpectedIni -or (Sha $pv)-ne $ExpectedVersion -or (Sha $ps)-ne $ExpectedSidecar){F 'Payload RC38 hash mismatch.' 10}
$a=Join-Path $g 'd3d11.dll';$c=Join-Path $g 'win81_nis_dx11_x64.dll';$i=Join-Path $g 'win81_nis.ini';$v=Join-Path $g 'win81_nis_version.txt';$s=Join-Path $g 'ptar_borderless.dll';$PreexistingPtar=$false
foreach($p in @($a,$c)){if(Test-Path -LiteralPath $p -PathType Leaf){$h=Sha $p;if(($KnownPtar -notcontains $h) -and -not(VersionMatches $h $v)){F ('DLL locale inconnue, installation refusee : '+$p+' '+$h) 20};$PreexistingPtar=$true}}
if(Test-Path -LiteralPath $s -PathType Leaf){$hs=Sha $s;if($hs -ne $ExpectedSidecar){F ('ptar_borderless.dll existante inconnue, installation refusee : '+$hs) 21}}
New-Item -ItemType Directory -Path $StateRoot -Force|Out-Null;$state=Join-Path $StateRoot ('INSTALL_RC38_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'));New-Item -ItemType Directory -Path $state -Force|Out-Null
$m=[ordered]@{schema=3;package='PTAR_RC38_BORDERLESS_AUTHORITY';game_root=$g;pack_root=$PackRoot;installed=@{};original=@{};windowstyle=@{};known_ptar=$KnownPtar;rc38_sidecar=@{sha=$ExpectedSidecar;preexisting=(Test-Path -LiteralPath $s -PathType Leaf)}}
foreach($r in @(@('d3d11.dll',$a,$ExpectedRuntime),@('win81_nis_dx11_x64.dll',$c,$ExpectedRuntime),@('win81_nis.ini',$i,$ExpectedIni),@('win81_nis_version.txt',$v,$ExpectedVersion))){$n=$r[0];$p=$r[1];$installSha=$r[2];$e=Test-Path -LiteralPath $p -PathType Leaf;$x=[ordered]@{exists=$e;sha=$null;backup=$null;ptar=$false};if($e){$x.sha=Sha $p;if(($n -eq 'd3d11.dll') -or ($n -eq 'win81_nis_dx11_x64.dll')){$x.ptar=(($KnownPtar -contains $x.sha) -or (VersionMatches $x.sha $v))}else{$x.ptar=$PreexistingPtar};$bk=Join-Path $state ('original_'+$n);Copy-Item -LiteralPath $p -Destination $bk -Force;if((Sha $bk)-ne $x.sha){F ('Backup incoherent '+$n) 22};$x.backup=$bk};$m.original[$n]=$x;$m.installed[$n]=$installSha}
$key='HKCU:\Software\NeoCore Games\Warhammer Martyr\Options';$m.windowstyle.key=$key;$m.windowstyle.exists=$false;$m.windowstyle.original=$null;$m.windowstyle.applied=$false
if(Test-Path -LiteralPath $key){$r=Get-ItemProperty -LiteralPath $key;if($r.PSObject.Properties.Name -contains 'WindowStyle'){$m.windowstyle.exists=$true;$m.windowstyle.original=[int]$r.WindowStyle;if([int]$r.WindowStyle -ne 1){Set-ItemProperty -LiteralPath $key -Name WindowStyle -Value 1;$m.windowstyle.applied=$true}}}
$m|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $state 'install_state.json') -Encoding UTF8;Set-Content -LiteralPath (Join-Path $StateRoot 'LATEST_STATE.txt') -Value $state -Encoding UTF8;Set-Content -LiteralPath (Join-Path $PackRoot 'win81_nis_install_target.txt') -Value $g -Encoding ASCII
Copy-Item -LiteralPath $pr -Destination $a -Force;Copy-Item -LiteralPath $pc -Destination $c -Force;Copy-Item -LiteralPath $pi -Destination $i -Force;Copy-Item -LiteralPath $pv -Destination $v -Force;Copy-Item -LiteralPath $ps -Destination $s -Force
if((Sha $a)-ne $ExpectedRuntime -or (Sha $c)-ne $ExpectedRuntime -or (Sha $i)-ne $ExpectedIni -or (Sha $v)-ne $ExpectedVersion -or (Sha $s)-ne $ExpectedSidecar){F 'Post-install RC38 hash mismatch.' 30}
L 'INSTALL_RC38=PASS';L ('GAME_ROOT='+$g);L ('RUNTIME_SHA256='+$ExpectedRuntime);L ('SIDECAR_SHA256='+$ExpectedSidecar);exit 0
'''

VERIFY=r'''$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
function Sha([string]$p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()};function F([string]$s,[int]$c=1){Write-Host ('[FAIL] '+$s);exit $c}
$g=$null;if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){$g=$PackRoot}else{$p=Split-Path -Parent $PackRoot;if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){$g=$p}};if(-not $g){F 'Warhammer.exe introuvable.' 2}
$E=@{'d3d11.dll'='@@RUNTIME@@';'win81_nis_dx11_x64.dll'='@@RUNTIME@@';'win81_nis.ini'='@@INI@@';'win81_nis_version.txt'='@@VERSION@@';'ptar_borderless.dll'='@@SIDECAR@@'}
foreach($n in $E.Keys){$p=Join-Path $g $n;if(-not(Test-Path -LiteralPath $p -PathType Leaf)){F ('Absent: '+$n) 10};$h=Sha $p;if($h -ne $E[$n]){F ('Hash incorrect '+$n+' '+$h) 11};Write-Host ('[PASS] '+$n+' '+$h)}
$ini=Get-Content -LiteralPath (Join-Path $g 'win81_nis.ini') -Raw
foreach($tok in @('UniversalSpatialPresenter=1','PresenterExclusive=0','GraphicsCartographer=0','RootAuthority=0','OutputWidth=1920','OutputHeight=1080','RenderWidth=1280','RenderHeight=720')){if($ini -notmatch ('(?m)^'+[regex]::Escape($tok)+'\s*$')){F ('Config attendue absente: '+$tok) 20}}
Write-Host '[PASS] VERIFY_RC38=PASS - fichiers, hashes et contrat 1280x720 -> 1920x1080 coherents.';exit 0
'''

ROLLBACK=r'''$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot;$S=Join-Path $PackRoot '_PTAR_UNINSTALL\state';function Sha([string]$p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
$l=Join-Path $S 'LATEST_STATE.txt';if(-not(Test-Path -LiteralPath $l -PathType Leaf)){Write-Host '[FAIL] Etat installation absent';exit 2};$state=(Get-Content -LiteralPath $l -TotalCount 1).Trim();$sp=Join-Path $state 'install_state.json';$m=Get-Content -LiteralPath $sp -Raw|ConvertFrom-Json;$g=[string]$m.game_root
if(Get-Process Warhammer -ErrorAction SilentlyContinue){Write-Host '[FAIL] Fermer Warhammer.';exit 3}
$base=Join-Path $PackRoot 'payload';$E=@{'d3d11.dll'='@@BASE_RUNTIME@@';'win81_nis_dx11_x64.dll'='@@BASE_RUNTIME@@';'win81_nis.ini'='@@BASE_INI@@';'win81_nis_version.txt'='@@BASE_VERSION@@'}
foreach($n in $E.Keys){$src=Join-Path $base $n;if((Sha $src)-ne $E[$n]){Write-Host ('[FAIL] Base GitHub alteree: '+$n);exit 10};Copy-Item -LiteralPath $src -Destination (Join-Path $g $n) -Force;if((Sha (Join-Path $g $n))-ne $E[$n]){Write-Host ('[FAIL] Rollback '+$n);exit 11};$m.installed.$n=$E[$n]}
$side=Join-Path $g 'ptar_borderless.dll';if(Test-Path -LiteralPath $side -PathType Leaf){$h=Sha $side;if($h -eq '@@SIDECAR@@'){Remove-Item -LiteralPath $side -Force}else{Write-Host ('[KEEP] ptar_borderless.dll modifiee: '+$h)}}
$m.package='GW16H_SAFEPOINT11_FUSEDDETAIL1_ROLLBACK_GITHUB_MAIN';$m|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $sp -Encoding UTF8;Write-Host '[PASS] RC38 retire; runtime/config GitHub main exacts restaures.';exit 0
'''

UNINSTALL=r'''param([Parameter(Mandatory=$true)][string]$Root,[Parameter(Mandatory=$true)][string]$Engine)
$ErrorActionPreference='Stop';$Root=[IO.Path]::GetFullPath($Root);$ov=Join-Path $Root 'BORDERLESS_AUTHORITY_RC38';$own=Join-Path $ov 'OVERLAY_OWNERSHIP.tsv';$stateRoot=Join-Path $Root '_PTAR_UNINSTALL\state';$game=$null
function Sha([string]$p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
$latest=Join-Path $stateRoot 'LATEST_STATE.txt';if(Test-Path -LiteralPath $latest -PathType Leaf){$st=(Get-Content -LiteralPath $latest -TotalCount 1).Trim();$js=Join-Path $st 'install_state.json';if(Test-Path -LiteralPath $js -PathType Leaf){$m=Get-Content -LiteralPath $js -Raw|ConvertFrom-Json;$game=[string]$m.game_root}}
& $Engine -Root $Root;$rc=$LASTEXITCODE;if($rc -ne 0){exit $rc}
if($game){$side=Join-Path $game 'ptar_borderless.dll';if(Test-Path -LiteralPath $side -PathType Leaf){$h=Sha $side;if($h -eq '@@SIDECAR@@'){Remove-Item -LiteralPath $side -Force;Write-Host '[OK] ptar_borderless.dll RC38 retiree.'}else{Write-Host ('[KEEP] ptar_borderless.dll modifiee: '+$h)}}}
if(Test-Path -LiteralPath $own -PathType Leaf){$rows=@(Get-Content -LiteralPath $own);foreach($row in $rows){if($row -notmatch '^\d+\|'){continue};$a=$row.Split('|');$p=Join-Path $Root ($a[1]-replace '/','\');if(Test-Path -LiteralPath $p -PathType Leaf){if((Sha $p)-eq $a[2]){Remove-Item -LiteralPath $p -Force}else{Write-Host ('[KEEP] Overlay modifie: '+$a[1])}}};Remove-Item -LiteralPath $own -Force -ErrorAction SilentlyContinue}
foreach($n in @('PTAR_RC38_INSTALL_LAST.log')){$p=Join-Path $ov $n;if(Test-Path -LiteralPath $p -PathType Leaf){Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue}}
if(Test-Path -LiteralPath $ov -PathType Container){try{Remove-Item -LiteralPath $ov -Force -ErrorAction Stop}catch{}}
Write-Host '[PASS] Base PTAR + overlay RC38 desinstalles par ownership/SHA.';exit 0
'''

def ps_scripts(vals):return {'install.ps1':_fill(INSTALL,vals),'verify.ps1':_fill(VERIFY,vals),'rollback.ps1':_fill(ROLLBACK,vals),'uninstall_overlay.ps1':_fill(UNINSTALL,vals)}

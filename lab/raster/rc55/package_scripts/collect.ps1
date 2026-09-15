$ErrorActionPreference='Stop'
$Here=$PSScriptRoot;$PackRoot=Split-Path -Parent $Here
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return 'ABSENT'}
function Root{if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){return $PackRoot};$p=Split-Path -Parent $PackRoot;if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){return $p};return $null}
$g=Root;if(-not $g){Write-Host '[FAIL] Warhammer.exe introuvable.';exit 2}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss';$out=Join-Path $g ('PTAR_RC55_RESULTS_'+$stamp+'.zip')
$tmp=Join-Path $env:TEMP ('PTAR_RC55_'+[guid]::NewGuid().ToString('N'));[System.IO.Directory]::CreateDirectory($tmp)|Out-Null
try{
 $key='HKCU:\Software\NeoCore Games\Warhammer Martyr\Options';$gui='UNAVAILABLE';try{$gui=[string]([int](Get-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution)}catch{$gui='ABSENT'}
 @('RC55_FIELD_RESULT=1',('RASTER_SHA256='+(Sha (Join-Path $g 'ptar_rc41.dll'))),('BORDERLESS_SHA256='+(Sha (Join-Path $g 'ptar_borderless.dll'))),('RUNTIME_SHA256='+(Sha (Join-Path $g 'd3d11.dll'))),('AFFECT_GUI_RESOLUTION='+$gui),'EXPECTED_LOGICAL=1920x1080','EXPECTED_PHYSICAL=1280x720','TARGET_FIX=BOUND_FULL_PHYSICAL_1280x720_IDEMPOTENCE') | Set-Content -LiteralPath (Join-Path $tmp 'RC55_ACTIVE_STATE.txt') -Encoding ASCII
 foreach($n in @('ptar_rc41.log','ptar_rc41_bootstrap.log','ptar_borderless_rc38.log')){$s=Join-Path $g $n;if(Test-Path -LiteralPath $s -PathType Leaf){Copy-Item -LiteralPath $s -Destination (Join-Path $tmp $n) -Force}else{('ABSENT: '+$s)|Set-Content -LiteralPath (Join-Path $tmp ($n+'.ABSENT.txt')) -Encoding ASCII}}
 $state=Join-Path $Here 'state\RC55_STATE.json';if(Test-Path -LiteralPath $state -PathType Leaf){Copy-Item -LiteralPath $state -Destination (Join-Path $tmp 'RC55_STATE.json') -Force}
 $oldProbe=Join-Path $g 'ptar_rc54_gui_probe.log';if(Test-Path -LiteralPath $oldProbe -PathType Leaf){@('NOTE=ptar_rc54_gui_probe.log existe encore dans le dossier du jeu mais RC55 ne l alimente pas.','RC55 collector ne le recopie pas afin d eviter de confondre des traces RC54 anciennes avec ce test.')|Set-Content -LiteralPath (Join-Path $tmp 'RC54_STALE_PROBE_NOTE.txt') -Encoding ASCII}
 if(Test-Path -LiteralPath $out){Remove-Item -LiteralPath $out -Force}
 [System.IO.Compression.ZipFile]::CreateFromDirectory($tmp,$out,[System.IO.Compression.CompressionLevel]::Optimal,$false)
 Write-Host ('[PASS] RESULTAT_RC55='+$out)
}finally{if(Test-Path -LiteralPath $tmp){Remove-Item -LiteralPath $tmp -Recurse -Force}}
exit 0
